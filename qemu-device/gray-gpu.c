#include "qemu/osdep.h"
#include "hw/pci/pci.h"
#include "qemu/units.h"
#include "qemu/log.h"
#include "qom/object.h"
#include "hw/pci/pci_device.h"
#include "ui/console.h"

#define TYPE_GRAY_GPU "gray-gpu"
OBJECT_DECLARE_SIMPLE_TYPE(GrayGPUState, GRAY_GPU);

#define GRAY_GPU_VENDOR_ID 0x1122
#define GRAY_GPU_DEVICE_ID 0x1122

#define GRAY_GPU_VRAM_SIZE      (16 * MiB) //16 MB VRAM
#define GRAY_GPU_REG_SIZE       (4 * KiB)  // 4 KB registers

//Register offset
#define REG_DEVICE_ID       0x00    /* Device identification */
#define REG_STATUS          0x04    /* Device status */
#define REG_CONTROL         0x08    /* Control register */
#define REG_FB_ADDR         0x0C    /* Framebuffer address in VRAM */
#define REG_FB_WIDTH        0x10    /* Framebuffer width */
#define REG_FB_HEIGHT       0x14    /* Framebuffer height */
#define REG_FB_BPP          0x18    /* Bits per pixel */
#define REG_FB_ENABLE       0x1C    /* Framebuffer enable */
#define REG_FB_PITCH        0x20    /* Framebuffer pitch/stride */

//Contorl register bit
#define CTRL_RESET      (1 << 0)
#define CTRL_ENABLE     (1 << 1)

//Status register bit 
#define STATUS_READY    (1 << 0)
#define STATUS_VBLANK   (1 << 1)

typedef struct GrayGPUState
{
    PCIDevice parent_obj;

    //Memory Region
    MemoryRegion registers;
    MemoryRegion vram;

    //Device status
    uint32_t device_id;
    uint32_t status;
    uint32_t control;
    uint32_t fb_addr;
    uint32_t fb_width;
    uint32_t fb_height;
    uint32_t fb_bpp;
    uint32_t fb_enable;
    uint32_t fb_pitch;

    //Display
    QemuConsole *console;
    uint8_t *vram_ptr;
    bool dirty;
}GrayGPUState;

static uint64_t gray_gpu_vram_read(void *opaque, hwaddr addr, unsigned size)
{
    GrayGPUState *g = GRAY_GPU(opaque);
    if(addr + size > GRAY_GPU_VRAM_SIZE){
        return 0;
    }

    switch(size){
        case 1:
            return ldub_p(g->vram_ptr + addr);
        case 2:
            return lduw_p(g->vram_ptr + addr);
        case 4:
            return ldl_p(g->vram_ptr + addr);
        case 8:
            return ldq_p(g->vram_ptr + addr);
    }
    return 0;
}

static void gray_gpu_vram_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    GrayGPUState *g = GRAY_GPU(opaque);
    if(addr + size > GRAY_GPU_VRAM_SIZE){
        return;
    }

    switch(size){
        case 1:
            stb_p(g->vram_ptr + addr, val);
            break;
        case 2:
            stw_p(g->vram_ptr + addr, val);
            break;
        case 4:
            stl_p(g->vram_ptr + addr, val);
            break;
        case 8:
            stl_p(g->vram_ptr + addr, val);
            break;
    }

    g->dirty = true;
}


static const MemoryRegionOps gray_gpu_vram_ops = {
    .read = gray_gpu_vram_read,
    .write = gray_gpu_vram_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

//Register read handler
static uint64_t gray_gpu_reg_read(void *opaque, hwaddr addr, unsigned size)
{
    GrayGPUState *g = GRAY_GPU(opaque);
    uint64_t val = 0;

    switch(addr){
        case REG_DEVICE_ID:
            val = g->device_id;
            break;
        case REG_STATUS:
            val = g->status | STATUS_READY;
            break;
        case REG_CONTROL:
            val = g->control;
            break;
        case REG_FB_ADDR:
            val = g->fb_addr;
            break;
        case REG_FB_WIDTH:
            val = g->fb_width;
            break;
        case REG_FB_HEIGHT:
            val = g->fb_height;
            break;
        case REG_FB_BPP:
            val = g->fb_bpp;
            break;
        case REG_FB_ENABLE:
            val = g->fb_enable;
            break;
        case REG_FB_PITCH:
            val = g->fb_pitch;
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR, "Invalid register read at 0x%lx\n", addr);
            break;
    }

    return val;
}

static void gray_gpu_reg_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    GrayGPUState *g = GRAY_GPU(opaque);
    
    switch(addr){
        case REG_DEVICE_ID:
            //Read only
            break;
        case REG_STATUS:
            //read only
            break;
        case REG_CONTROL:
            g->control = val;
            if(val & CTRL_RESET){
                //Reset device
                g->fb_width = 800;
                g->fb_height = 600;
                g->fb_pitch = g->fb_width * 4;
                g->fb_enable = 0;
                g->fb_addr = 0;
                g->control &= ~CTRL_RESET;
                g->dirty = true;
            }
            break;
        case REG_FB_ADDR:
            g->fb_addr = val;
            g->dirty = true;
            break;
        case REG_FB_WIDTH:
            g->fb_width = val;
            g->fb_pitch = g->fb_width * (g->fb_pitch/8);
            g->dirty = true;
            break;
        case REG_FB_HEIGHT:
            g->fb_height = val;
            g->dirty = true;
            break;
        case REG_FB_BPP:
            g->fb_bpp = val;
            g->fb_pitch = g->fb_width * (g->fb_bpp/8);
            g->dirty = true;
            break;
        case REG_FB_ENABLE:
            g->fb_enable = val;
            if(val){
                //if framebuffer is enabledd update the display
                qemu_console_resize(g->console, g->fb_width, g->fb_height);
                g->dirty = true;
            }
            break;
        case REG_FB_PITCH:
            g->fb_pitch = val;
            g->dirty = true;
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR, "Invalid regiseter write at 0x%lx = 0x%lx\n", addr, val);
            break;
    }
}

static const MemoryRegionOps gray_gpu_reg_ops = {
    .read = gray_gpu_reg_read,
    .write = gray_gpu_reg_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void gray_gpu_update_display(void *opaque)
{
    GrayGPUState *g = GRAY_GPU(opaque);
    DisplaySurface *surface = qemu_console_surface(g->console);

    if(!g->fb_enable || !surface || !g->vram_ptr){
        return;
    }

    //Simple framebuffer copy - copy from vram offset to display
    if(g->fb_width > 0 && g->fb_height > 0 && g->dirty){
        uint8_t *fb_data = g->vram_ptr + g->fb_addr;

        //Create display surface from framebuffer data
        if(g->fb_bpp == 32){
            DisplaySurface *fb_surface = qemu_create_displaysurface_from(
                    g->fb_width, g->fb_height, PIXMAN_a8r8g8b8,
                    g->fb_pitch, fb_data);
            dpy_gfx_replace_surface(g->console, fb_surface);
        }

        dpy_gfx_update(g->console, 0, 0, g->fb_width, g->fb_height);
        g->dirty = false;
    }
}

static void gray_gpu_invalidate_display(void *opaque)
{
    GrayGPUState *g = GRAY_GPU(opaque);
    g->dirty = true;
}

static const GraphicHwOps gray_gpu_ops = { 
    .invalidate = gray_gpu_invalidate_display,
    .gfx_update = gray_gpu_update_display,
};

static void gray_gpu_realize(PCIDevice *pci_dev, Error **errp){
    GrayGPUState *g = GRAY_GPU(pci_dev);

    g->device_id = GRAY_GPU_DEVICE_ID;
    g->status = STATUS_READY;
    g->control = 0;
    g->fb_width = 800;
    g->fb_height = 600;
    g->fb_bpp = 32;
    g->fb_pitch = g->fb_width * 4;
    g->fb_enable = 0;
    g->fb_addr = 0;
    g->dirty = false;
    
    memory_region_init_io(&g->registers, OBJECT(g), &gray_gpu_reg_ops, g,
            "gray-gpu-registers", GRAY_GPU_REG_SIZE);
    g->vram_ptr = g_malloc0(GRAY_GPU_VRAM_SIZE);
    memory_region_init_io(&g->vram, OBJECT(g), &gray_gpu_vram_ops, g,
            "gray-gpu-vram", GRAY_GPU_VRAM_SIZE);

    pci_dev->config[PCI_INTERRUPT_PIN] = 1;

    pci_register_bar(pci_dev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &g->registers);
    pci_register_bar(pci_dev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY | PCI_BASE_ADDRESS_MEM_PREFETCH, &g->vram);

    g->console = graphic_console_init(DEVICE(pci_dev), 0, &gray_gpu_ops, g);
    qemu_console_resize(g->console, g->fb_width, g->fb_height);
}


static void gray_gpu_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass* dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = gray_gpu_realize;
    //k->exit = gray_gpu_exit;
    k->vendor_id = GRAY_GPU_VENDOR_ID;
    k->device_id = GRAY_GPU_DEVICE_ID;
    k->class_id = PCI_CLASS_DISPLAY_VGA;
    k->subsystem_vendor_id = GRAY_GPU_VENDOR_ID;
    k->subsystem_id = GRAY_GPU_DEVICE_ID;
    
    dc->desc = "Gray GPU Device for Learning";
    set_bit(DEVICE_CATEGORY_DISPLAY, dc->categories);
}

static const TypeInfo gray_gpu_info = {
    .name = TYPE_GRAY_GPU,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(GrayGPUState),
    .class_init = gray_gpu_class_init,
    .interfaces = (InterfaceInfo[]){
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void gray_gpu_register_types(void)
{
    type_register_static(&gray_gpu_info);
}

type_init(gray_gpu_register_types);
