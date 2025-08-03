#include <linux/init.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/vmalloc.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/mm.h>

#define DRIVER_NAME "Gray-gpu"
#define DRIVER_DESC "Gray GPU Driver for Learning purpose"

#define GRAY_GPU_VENDOR_ID    0x1122
#define GRAY_GPU_DEVICE_ID    0x1122

//Register offset
#define REG_DEVICE_ID       0x00
#define REG_STATUS          0x04
#define REG_CONTROL         0x08
#define REG_FB_ADDR         0x0C
#define REG_FB_WIDTH        0x10
#define REG_FB_HEIGHT       0x14
#define REG_FB_BPP          0x18
#define REG_FB_ENABLE       0x1C
#define REG_FB_PITCH        0x20
#define REG_CURSOR_X        0x24
#define REG_CURSOR_Y        0x28
#define REG_CURSOR_ENABLE   0x2C
#define REG_CURSOR_HOTSPOT_X 0x30
#define REG_CURSOR_HOTSPOT_Y 0x34
#define REG_CURSOR_UPLOAD   0x38
#define REG_FB_COUNT        0x3C   
#define REG_FB_CURRENT      0x40  
#define REG_FB_NEXT         0x44 
#define REG_PAGE_FLIP       0x48  
#define REG_FLIP_PENDING    0x4C  
#define REG_VBLANK_COUNT    0x50 

//Control register bits
#define CTRL_RESET	(1<<0)
#define CTRL_ENABLE	(1<<1)

//Status register bits
#define STATUS_READY	(1<<0)
#define STATUS_VBLANK	(1<<1)
#define STATUS_CURSOR_LOADED (1 << 2)

//Character device
#define GRAY_GPU_MINOR		0
#define GRAY_GPU_NAME		"gray-gpu"

struct gray_gpu_device {
	struct pci_dev *pdev;
	void __iomem *registers;
	void __iomem *vram;
	size_t vram_size;

	//Framebuffer info
	uint32_t fb_width;
	uint32_t fb_height;
	uint32_t fb_bpp;
	uint32_t fb_pitch;
	uint32_t fb_size;
 
	/* Cursor info */
	uint32_t cursor_x;
	uint32_t cursor_y;
	uint32_t cursor_enabled;
	uint32_t cursor_hotspot_x;
	uint32_t cursor_hotspot_y;

	//Multiple Framebuffer state
	uint32_t fb_count;
	uint32_t fb_current;
	uint32_t fb_next;
	uint32_t flip_pending;
	uint32_t vblank_count;
	uint32_t fb_addresses[4];

	//Character device
	struct cdev cdev;
	dev_t devt;
	struct class *class;
	struct device *device;
};

static struct gray_gpu_device *gray_gpu_dev = NULL;

static inline void gray_gpu_write_reg(struct gray_gpu_device *gpu, u32 offset, u32 value)
{
	iowrite32(value, gpu->registers + offset);
}

static inline u32 gray_gpu_read_reg(struct gray_gpu_device *gpu, u32 offset)
{
	return ioread32(gpu->registers + offset);
}

static int gray_gpu_setup_framebuffer(struct gray_gpu_device *gpu, uint32_t width, uint32_t height, uint32_t bpp)
{
	gpu->fb_width = width;
	gpu->fb_height = height;
	gpu->fb_bpp = bpp;
	gpu->fb_pitch = width * (bpp /  8);
	gpu->fb_size = gpu->fb_pitch * height;

	if(gpu->fb_size > gpu->vram_size){
		dev_err(&gpu->pdev->dev, "Framebuffer too large for VRAM\n");
		return -EINVAL;
	}

	//Configure device 
	gray_gpu_write_reg(gpu, REG_FB_WIDTH, width);
	gray_gpu_write_reg(gpu, REG_FB_HEIGHT, height);
	gray_gpu_write_reg(gpu, REG_FB_BPP, bpp);
	gray_gpu_write_reg(gpu, REG_FB_PITCH, gpu->fb_pitch);
	gray_gpu_write_reg(gpu, REG_FB_ADDR, 0) ;//Framebuffer at vram offset 0
	
	dev_info(&gpu->pdev->dev, "Framebuffer: %dx%d@%dbpp, pitch=%d, size=%d\n", width, height, bpp, gpu->fb_pitch, gpu->fb_size);

	return 0;
}


static void gray_gpu_enable_display(struct gray_gpu_device *gpu, bool enable)
{
	gray_gpu_write_reg(gpu, REG_FB_ENABLE, enable? 1:0);
	dev_info(&gpu->pdev->dev, "Display %s\n", enable? "enabled" : "disabled");
}

static void gray_gpu_set_cursor_position(struct gray_gpu_device *gpu, uint32_t x, uint32_t y)
{
	gpu->cursor_x = x;
	gpu->cursor_y = y;
	gray_gpu_write_reg(gpu, REG_CURSOR_X, x);
	gray_gpu_write_reg(gpu, REG_CURSOR_Y, y);
}

static void gray_gpu_enable_cursor(struct gray_gpu_device *gpu, bool enable)
{
	gpu->cursor_enabled = enable ? 1 : 0;
	gray_gpu_write_reg(gpu, REG_CURSOR_ENABLE, gpu->cursor_enabled);
	dev_info(&gpu->pdev->dev, "Cursor %s\n", enable ? "enabled" : "disabled");
}

static void gray_gpu_set_cursor_hotspot(struct gray_gpu_device *gpu, uint32_t x, uint32_t y)
{
	gpu->cursor_hotspot_x = x;
	gpu->cursor_hotspot_y = y;
	gray_gpu_write_reg(gpu, REG_CURSOR_HOTSPOT_X, x);
	gray_gpu_write_reg(gpu, REG_CURSOR_HOTSPOT_Y, y);
}

static int gray_gpu_upload_cursor(struct gray_gpu_device *gpu, const uint32_t *cursor_data, size_t size)
{
	size_t i;

	if(size > 64 * 64){
		dev_err(&gpu->pdev->dev, "Cursor data too large (max 64x64 pixels)\n");
		return -EINVAL;
	}

	/* Upload cursor data pixel by pixel */
	for (i = 0; i < size; i++) {
		gray_gpu_write_reg(gpu, REG_CURSOR_UPLOAD, cursor_data[i]);
	}
	return 0;
}

static int gray_gpu_setup_multi_framebuffer(struct gray_gpu_device *gpu, uint32_t fb_count, uint32_t width, uint32_t height, uint32_t bpp)
{
	uint32_t fb_size;
	if(fb_count > 4){
		dev_err(&gpu->pdev->dev, "Maximum 4 framebuffer supported\n");
		return -EINVAL;
	}

	fb_size = width * height * (bpp / 8);

	if(fb_size * fb_count > gpu->vram_size){
		dev_err(&gpu->pdev->dev, "Not enough VRAM for %d framebuffer\n", fb_count);
		return -EINVAL;
	}

	gpu->fb_width = width;
	gpu->fb_height = height;
	gpu->fb_bpp = bpp;
	gpu->fb_size = fb_size;
	gpu->fb_count = fb_count;
	gpu->fb_current = 0;
	gpu->fb_next = 0;
	gpu->flip_pending = 0;

	{
		int i;
		for( i = 0; i < fb_count; i++){
			gpu->fb_addresses[i] = i * fb_size;
		}
	}
    
    /* Configure hardware */
	gray_gpu_write_reg(gpu, REG_FB_WIDTH, width);
	gray_gpu_write_reg(gpu, REG_FB_HEIGHT, height);
	gray_gpu_write_reg(gpu, REG_FB_BPP, bpp);
	gray_gpu_write_reg(gpu, REG_FB_PITCH, gpu->fb_pitch);
	gray_gpu_write_reg(gpu, REG_FB_COUNT, fb_count);
	gray_gpu_write_reg(gpu, REG_FB_ADDR, gpu->fb_addresses[0]); /* Start with first buffer */
    
	 dev_info(&gpu->pdev->dev, "Setup %d framebuffers: %dx%d@%dbpp, each %d bytes\n",
		fb_count, width, height, bpp, fb_size);
             
	return 0;
}

static int gray_gpu_page_flip(struct gray_gpu_device *gpu, uint32_t fb_index, uint32_t wait_vblank)
{
	if (fb_index >= gpu->fb_count) {
		dev_err(&gpu->pdev->dev, "Invalid framebuffer index: %d\n", fb_index);
		return -EINVAL;
	}
    
	 if (gpu->flip_pending) {
		dev_warn(&gpu->pdev->dev, "Page flip already pending\n");
		return -EBUSY;
	}
    
	/* Set next framebuffer */
	gpu->fb_next = fb_index;
	gray_gpu_write_reg(gpu, REG_FB_NEXT, fb_index);
    
	/* Trigger page flip */
	 gray_gpu_write_reg(gpu, REG_PAGE_FLIP, 1);
    
	/* Check if flip completed immediately */
	gpu->flip_pending = gray_gpu_read_reg(gpu, REG_FLIP_PENDING);
	 if (!gpu->flip_pending) {
		gpu->fb_current = fb_index;
		gpu->vblank_count = gray_gpu_read_reg(gpu, REG_VBLANK_COUNT);
	}
    
	dev_dbg(&gpu->pdev->dev, "Page flip to framebuffer %d %s\n", 
		fb_index, gpu->flip_pending ? "pending" : "completed");
            
	return 0;
}

static int gray_gpu_wait_flip(struct gray_gpu_device *gpu)
{
	int timeout = 100; /* 100ms timeout */
    
	while (gpu->flip_pending && timeout > 0) {
		gpu->flip_pending = gray_gpu_read_reg(gpu, REG_FLIP_PENDING);
		if (!gpu->flip_pending) {
			gpu->fb_current = gray_gpu_read_reg(gpu, REG_FB_CURRENT);
			gpu->vblank_count = gray_gpu_read_reg(gpu, REG_VBLANK_COUNT);
			break;
		}
		msleep(1);
		timeout--;
	}
    
	if (gpu->flip_pending) {
		dev_err(&gpu->pdev->dev, "Page flip timeout\n");
		return -ETIMEDOUT;
	}
    
	return 0;
}

static void gray_gpu_get_fb_info(struct gray_gpu_device *gpu, void *info_struct)
{
	struct {
		uint32_t fb_count;
		uint32_t current_fb;
		uint32_t fb_size;
		uint32_t fb_offsets[4];
	} *fb_info = info_struct;
	    
	fb_info->fb_count = gpu->fb_count;
	fb_info->current_fb = gpu->fb_current;
	fb_info->fb_size = gpu->fb_size;
	    
	for (int i = 0; i < 4; i++) {
		fb_info->fb_offsets[i] = (i < gpu->fb_count) ? gpu->fb_addresses[i] : 0;
	}
}

static int gray_gpu_open(struct inode *inode, struct file *file)
{
	file->private_data = gray_gpu_dev;
	return 0;
}

static int gray_gpu_release(struct inode *inode, struct file *file)
{
	return 0;
}

static long gray_gpu_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct gray_gpu_device *gpu = file->private_data;
    
    switch (cmd) {
    case 0x1000: /* Setup framebuffer */
        {
            uint32_t params[3];
            if (copy_from_user(params, (void __user *)arg, sizeof(params))) {
                return -EFAULT;
            }

            return gray_gpu_setup_framebuffer(gpu, params[0], params[1], params[2]);
        }
    case 0x1001: /* Enable display */
        gray_gpu_enable_display(gpu, arg != 0);
        return 0;
    case 0x1002: /* Get VRAM size */
        return put_user(gpu->vram_size, (uint32_t __user *)arg);
    case 0x1003: //Set cursor position
	{
		uint32_t params[2];
		if(copy_from_user(params, (void __user *)arg, sizeof(params))){
			return -EFAULT;
		}
		gray_gpu_set_cursor_position(gpu, params[0], params[1]);
		return 0;
	}
    case 0x1004: //Enable/Disable cursor 
	gray_gpu_enable_cursor(gpu, arg != 0);
	return 0;
    case 0x1005: //Set cursor hotspot
	{
		uint32_t params[2];
		if(copy_from_user(params, (void __user *)arg, sizeof(params))){
			return -EFAULT;
		}
		gray_gpu_set_cursor_hotspot(gpu, params[0], params[1]);
		return 0;
	}
    case 0x1006:
	{
		struct{
			uint32_t *data;
			size_t size;
		} cursor_upload;
		uint32_t *cursor_data;
		int ret;

		if(copy_from_user(&cursor_upload, (void __user *)arg, sizeof(cursor_upload))){
			return -EFAULT;
		}

		if(cursor_upload.size > 64 * 64){
			return -EINVAL;
		}

		cursor_data = kmalloc(cursor_upload.size * sizeof(uint32_t), GFP_KERNEL);
		if(!cursor_data){
			return -ENOMEM;
		}

		if(copy_from_user(cursor_data, cursor_upload.data, cursor_upload.size * sizeof(uint32_t))){
			kfree(cursor_data);
			return -EFAULT;
		}

		ret = gray_gpu_upload_cursor(gpu, cursor_data, cursor_upload.size);
		kfree(cursor_data);
		return ret;
	}
    case 0x1007: //Setup Multiple framebuffer
	{
		struct {
			uint32_t fb_count;
			uint32_t width;
			uint32_t height;
			uint32_t bpp;
		} multi_setup;
		if(copy_from_user(&multi_setup, (void __user *)arg, sizeof(multi_setup))){
			return -EFAULT;
		}
		return gray_gpu_setup_multi_framebuffer(gpu, multi_setup.fb_count, multi_setup.width,multi_setup.height, multi_setup.bpp);
	}
    case 0x1008:
	{
		struct {
			uint32_t fb_index;
			uint32_t wait_vblank;
		} flip_req;
		if(copy_from_user(&flip_req, (void __user *)arg, sizeof(flip_req))){
			return -EFAULT;
		}
		return gray_gpu_page_flip(gpu, flip_req.fb_index, flip_req.wait_vblank);
	}
    case 0x1009: //wait fror flip completion
	return gray_gpu_wait_flip(gpu);
    case 0x100A: //Get framebuffer into
	{
		struct {
			uint32_t fb_count;
			uint32_t current_fb;
			uint32_t fb_size;
			uint32_t fb_offset[4];
		} fb_info;

		gray_gpu_get_fb_info(gpu, &fb_info);

		if(copy_from_user((void __user *)arg, &fb_info, sizeof(fb_info))){
			return -EFAULT;
		}
		return 0;
	}
    default:
        return -ENOTTY;
    }
}

static int gray_gpu_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct gray_gpu_device *gpu = file->private_data;
	unsigned long size = vma->vm_end - vma->vm_start;
	unsigned long pfn;

	if(size > gpu->vram_size){
		return -EINVAL;
	}
	
	pfn = pci_resource_start(gpu->pdev, 1)>>PAGE_SHIFT;
	vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);

	return io_remap_pfn_range(vma, vma->vm_start, pfn, size, vma->vm_page_prot);
}

static const struct file_operations gray_gpu_fops = {
	.owner = THIS_MODULE,
	.open = gray_gpu_open,
	.release = gray_gpu_release,
	.mmap = gray_gpu_mmap,
	.unlocked_ioctl = gray_gpu_ioctl,
};

static int gray_gpu_init_device(struct gray_gpu_device *gpu)
{
	struct pci_dev *pdev = gpu->pdev;
	int ret;

	ret = pcim_enable_device(pdev);
	if(ret){
		dev_err(&pdev->dev, "Failed to enable PCI device\n");
		return ret;
	}

	dev_info(&pdev->dev, "BAR0: start=0x%llx, len=0x%llu\n",
			(unsigned long long)pci_resource_start(pdev, 0),
			(unsigned long long)pci_resource_len(pdev, 0));
	dev_info(&pdev->dev, "BAR1: start=0x%llx, len=0x%llu\n",
			(unsigned long long)pci_resource_start(pdev, 1),
			(unsigned long long)pci_resource_len(pdev, 1));

	//Map bar0 register
	gpu->registers = pcim_iomap(pdev, 0, pci_resource_len(pdev, 0));
	if(!gpu->registers){
		dev_err(&pdev->dev, "Failed to map BAR0 register\n");
		return -ENOMEM;
	}

	
	if (pci_resource_len(pdev, 1) == 0) {
		dev_err(&pdev->dev, "BAR1 not available or has zero length\n");
		dev_err(&pdev->dev, "QEMU device may not be properly configured with VRAM\n");
		return -ENODEV;
	 } else {
		dev_info(&pdev->dev, "BAR1 available: start=0x%llx, len=%llu, flags=0x%lx\n",
			(unsigned long long)pci_resource_start(pdev, 1),
			(unsigned long long)pci_resource_len(pdev, 1),
			pci_resource_flags(pdev, 1));
	
		/* Check if BAR1 is memory mapped */
		if (!(pci_resource_flags(pdev, 1) & IORESOURCE_MEM)) {
			dev_err(&pdev->dev, "BAR1 is not a memory resource\n");
			return -ENODEV;
		}
	
		/* Request BAR1 region */
		if (pci_request_region(pdev, 1, DRIVER_NAME)) {
			dev_err(&pdev->dev, "Failed to request BAR1 region\n");
			return -ENODEV;
		}
	
		/* Map BAR1 (VRAM) */
		gpu->vram = pcim_iomap(pdev, 1, pci_resource_len(pdev, 1));
		gpu->vram_size = pci_resource_len(pdev, 1);
		if (!gpu->vram) {
		dev_err(&pdev->dev, "Failed to map BAR1 (VRAM) - pcim_iomap returned NULL\n");
		 pci_release_region(pdev, 1);
		 return -ENOMEM;
		} else {
			dev_info(&pdev->dev, "VRAM mapped successfully: %zu bytes at virtual address %p\n", gpu->vram_size, gpu->vram);
		}
	}

	u32 device_id = gray_gpu_read_reg(gpu, REG_DEVICE_ID);
	u32 status = gray_gpu_read_reg(gpu, REG_STATUS);

	dev_info(&pdev->dev, "Gray GPU found: device_id=0x%x, status=0x%x, VRAM=%zuMB\n",
		    device_id, status, gpu->vram_size/(1024*1024));

	//Reset device
	gray_gpu_write_reg(gpu, REG_CONTROL, CTRL_RESET);
	msleep(1);

	return 0;
}
			

static int gray_gpu_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct gray_gpu_device *gpu;
	int ret;

	dev_info(&pdev->dev, "Probing GRAY GPU device\n");

	gpu = devm_kzalloc(&pdev->dev, sizeof(*gpu), GFP_KERNEL);
	if(!gpu){
		return -ENOMEM;
	}

	gpu->pdev = pdev;
	pci_set_drvdata(pdev, gpu);
	gray_gpu_dev = gpu;

	ret = gray_gpu_init_device(gpu);
	if(ret){
		return ret;
	}

	ret = alloc_chrdev_region(&gpu->devt, 0, 1, GRAY_GPU_NAME);
	if(ret){
		dev_err(&pdev->dev, "Failed to allocate char device region\n");
		return ret;
	}

	cdev_init(&gpu->cdev, &gray_gpu_fops);
	gpu->cdev.owner = THIS_MODULE;

	ret = cdev_add(&gpu->cdev, gpu->devt, 1);
	if(ret){
		dev_err(&pdev->dev, "Failed to add char device\n");
		goto err_unregister_chrdev;
	}

	gpu->class = class_create(GRAY_GPU_NAME);
	if(IS_ERR(gpu->class)){
		ret = PTR_ERR(gpu->class);
		goto err_cdev_del;
	}

	gpu->device = device_create(gpu->class, &pdev->dev, gpu->devt, NULL, GRAY_GPU_NAME);
	if(IS_ERR(gpu->device)){
		ret = PTR_ERR(gpu->device);
		goto err_class_destroy;
	}

	//setup default Framebuffer
	ret = gray_gpu_setup_framebuffer(gpu, 800, 600, 32);
	if(ret){
		goto err_device_destroy;
	}

	gpu->cursor_x = 0;
	gpu->cursor_y = 0;
	gpu->cursor_enabled = 0;
	gpu->cursor_hotspot_x = 0;
	gpu->cursor_hotspot_y = 0;

	gray_gpu_set_cursor_position(gpu, 0, 0);
	gray_gpu_set_cursor_hotspot(gpu, 0, 0);
	gray_gpu_enable_cursor(gpu, false);

	gpu->fb_count = 1;
	gpu->fb_current = 0;
	gpu->fb_next = 0;
	gpu->flip_pending = 0;
	gpu->vblank_count = 0;
	gpu->fb_addresses[0] = 0; 

	dev_info(&pdev->dev, "Gray gpu loaded successfully\n");
	dev_info(&pdev->dev, "Character device: /dev/%s\n", GRAY_GPU_NAME);

	return 0;

err_device_destroy:
	device_destroy(gpu->class, gpu->devt);
err_class_destroy:
	class_destroy(gpu->class);
err_cdev_del:
	cdev_del(&gpu->cdev);
err_unregister_chrdev:
	unregister_chrdev_region(gpu->devt, 1);
	return ret;
}

static void gray_gpu_pci_remove(struct pci_dev *pdev)
{
	struct gray_gpu_device *gpu = pci_get_drvdata(pdev);
	dev_info(&pdev->dev, "Removing gray GPU device\n");

	//Disable display
	gray_gpu_enable_display(gpu, false);

	//Clean up character device
	device_destroy(gpu->class, gpu->devt);
	class_destroy(gpu->class);
	cdev_del(&gpu->cdev);
	unregister_chrdev_region(gpu->devt, 1);

	gray_gpu_dev = NULL;
}

static const struct pci_device_id gray_gpu_pci_ids[] = {
	{PCI_DEVICE(GRAY_GPU_VENDOR_ID, GRAY_GPU_DEVICE_ID) },
	{ },
};
MODULE_DEVICE_TABLE(pci, gray_gpu_pci_ids);

static struct pci_driver gray_gpu_pci_driver = {
	.name = DRIVER_NAME,
	.id_table = gray_gpu_pci_ids,
	.probe = gray_gpu_pci_probe,
	.remove = gray_gpu_pci_remove,
};

module_pci_driver(gray_gpu_pci_driver);

MODULE_AUTHOR("Madhur Kumar");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
