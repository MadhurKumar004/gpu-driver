#include "qemu/osdep.h"
#include "hw/pci/pci.h"
#include "qemu/units.h"
#include "qemu/log.h"
#include "qom/object.h"
#include "hw/pci/pci_device.h"
#include "ui/console.h"
#include <stdint.h>

#define TYPE_GRAY_GPU "gray-gpu"
OBJECT_DECLARE_SIMPLE_TYPE(GrayGPUState, GRAY_GPU);

#define GRAY_GPU_VENDOR_ID 0x1122
#define GRAY_GPU_DEVICE_ID 0x1122

#define GRAY_GPU_VRAM_SIZE      (16 * MiB) //16 MB VRAM
#define GRAY_GPU_REG_SIZE       (4 * KiB)  // 4 KB registers
                                           
#define CURSOR_SIZE         64
#define CURSOR_DATA_SIZE    (CURSOR_SIZE * CURSOR_SIZE * 4)

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
#define REG_CURSOR_X        0x24    /* Cursor X position */
#define REG_CURSOR_Y        0x28    /* Cursor Y position */
#define REG_CURSOR_ENABLE   0x2C    /* Cursor enable */
#define REG_CURSOR_HOTSPOT_X 0x30   /* Cursor hotspot X */
#define REG_CURSOR_HOTSPOT_Y 0x34   /* Cursor hotspot Y */
#define REG_CURSOR_UPLOAD   0x38    /* Cursor data upload */

//Multiple framebuffer and page flipping registers
#define REG_FB_COUNT        0x3C    //Number of framebuffer
#define REG_FB_CURRENT      0x40    //Currently displayed framebuffer
#define REG_FB_NEXT         0x44    //Next framebuffer to display
#define REG_PAGE_FLIP       0x48    //Trigger page flip
#define REG_FLIP_PENDING    0x4C    //Page flip in progress
#define REG_VBLANK_COUNT    0x50    //Vblank counter


//Contorl register bit
#define CTRL_RESET      (1 << 0)
#define CTRL_ENABLE     (1 << 1)

//Status register bit 
#define STATUS_READY    (1 << 0)
#define STATUS_VBLANK   (1 << 1)
#define STATUS_CURSOR_LOADED    (1 << 2) //cursor image loaded


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

    //Mutliple framebuffer state
    uint32_t fb_count;
    uint32_t fb_current;
    uint32_t fb_next;
    uint32_t flip_pending;
    uint32_t vblank_count;
    uint32_t fb_addresses[4];

    //Cursor state
    uint32_t cursor_x;
    uint32_t cursor_y;
    uint32_t cursor_enabled;
    uint32_t cursor_hotspot_x;
    uint32_t cursor_hotspot_y;
    uint32_t cursor_data[CURSOR_SIZE * CURSOR_SIZE]; //Argb format
    uint32_t cursor_upload_offset;

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
        case REG_CURSOR_X:
            val = g->cursor_x;
            break;
        case REG_CURSOR_Y:
            val = g->cursor_y;
            break;
        case REG_CURSOR_ENABLE:
            val = g->cursor_enabled;
            break;
        case REG_CURSOR_HOTSPOT_X:
            val = g->cursor_hotspot_x;
            break;
        case REG_CURSOR_HOTSPOT_Y:
            val = g->cursor_hotspot_y;
            break;
        case REG_FB_COUNT:
            val = g->fb_count;
            break;
        case REG_FB_CURRENT:
            val = g->fb_current;
            break;
        case REG_FB_NEXT:
            val = g->fb_next;
            break;
        case REG_FLIP_PENDING:
            val = g->flip_pending;
            break;
        case REG_VBLANK_COUNT:
            val = g->vblank_count;
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR, "Invalid register read at 0x%lx\n", addr);
            break;    }

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
        case REG_CURSOR_X:
            g->cursor_x = val;
            g->dirty = true;
            break;
        case REG_CURSOR_Y:
            g->cursor_y = val;
            g->dirty = true;
            break;
        case REG_CURSOR_ENABLE:
            g->cursor_enabled = val;
            g->dirty = true;
            break;
        case REG_CURSOR_HOTSPOT_X:
            g->cursor_hotspot_x = val;
            break;
        case REG_CURSOR_HOTSPOT_Y:
            g->cursor_hotspot_y = val;
            break;
        case REG_CURSOR_UPLOAD:
             if (g->cursor_upload_offset < CURSOR_SIZE * CURSOR_SIZE) {
                g->cursor_data[g->cursor_upload_offset] = val;
                g->cursor_upload_offset++;
                if (g->cursor_upload_offset >= CURSOR_SIZE * CURSOR_SIZE) {
                    g->cursor_upload_offset = 0; /* Reset for next upload */
                    g->status |= STATUS_CURSOR_LOADED;
                    g->dirty = true;
                }
            }
            break;
        case REG_FB_COUNT:
            if(val <= 4){

                int i;
                uint32_t fb_size;

                g->fb_count = val;
                fb_size = g->fb_width * g->fb_height * (g->fb_bpp / 8);
                for( i = 0; i < g->fb_count; i++){
                    g->fb_addresses[i] = i * fb_size;
                }
                g->fb_current = 0;
                g->fb_next = 0;
                g->dirty = true;
            }
            break;
        case REG_FB_NEXT:
            if(val < g->fb_count){
                g->fb_next = val;
            }
            break;
        case REG_PAGE_FLIP:
            if(val && g->fb_next < g->fb_count && !g->flip_pending){
                g->flip_pending = 1;
                g->fb_current = g->fb_next;
                g->fb_addr = g->fb_addresses[g->fb_current];
                g->flip_pending = 0;
                g->vblank_count++;
                g->dirty = true;
            }
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

static void init_default_cursor(GrayGPUState *g)
{
    memset(g->cursor_data, 0, sizeof(g->cursor_data));
    
    /* Simple white arrow cursor with black outline */
    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 10; x++) {
            if ((x == 0 && y < 16) ||  /* Left edge */
                (y == 0 && x < 10) ||  /* Top edge */
                (x == y && x < 10) ||  /* Diagonal */
                (x == 5 && y > 5 && y < 12)) { /* Stem */
                
                /* Black outline */
                g->cursor_data[y * CURSOR_SIZE + x] = 0xFF000000;
                
                /* White fill inside */
                if (x > 0 && y > 0 && x < 9) {
                    g->cursor_data[y * CURSOR_SIZE + x + 1] = 0xFFFFFFFF;
                }
            }
        }
    }
    
    g->cursor_hotspot_x = 0;
    g->cursor_hotspot_y = 0;
}

static void composite_cursor(GrayGPUState *g, uint32_t *fb)
{
    if(!g->cursor_enabled || !g->fb_enable){
        return;
    }

    int cursor_screen_x = g->cursor_x - g->cursor_hotspot_x;
    int cursor_screen_y = g->cursor_y - g->cursor_hotspot_y;

    for(int cy = 0; cy < CURSOR_SIZE; cy++){
        for(int cx = 0; cx < CURSOR_SIZE; cx++){
            int screen_x = cursor_screen_x + cx;
            int screen_y = cursor_screen_y + cy;

            if(screen_x < 0 || screen_x >= (int)g->fb_width || screen_y < 0 || screen_y >= (int)g->fb_height){
                continue;
            }

            uint32_t cursor_pixel = g->cursor_data[cy * CURSOR_SIZE + cx];
            uint32_t alpha = (cursor_pixel >> 24) & 0xFF;

            if(alpha > 0){
                int fb_offset = screen_y * g->fb_width + screen_x;

                if(alpha == 0xFF){
                    fb[fb_offset] = cursor_pixel;
                }else{
                    uint32_t bg = fb[fb_offset];
                    uint32_t bg_r = (bg >> 16) & 0xFF;
                    uint32_t bg_g = (bg >> 8) & 0xFF;
                    uint32_t bg_b = bg & 0xFF;

                    uint32_t fg_r = (cursor_pixel >> 16) & 0xFF;
                    uint32_t fg_g = (cursor_pixel >> 8) & 0xFF;
                    uint32_t fg_b = cursor_pixel & 0xFF;
                    
                    uint32_t r_t = (fg_r * alpha + bg_r * (255 - alpha)) / 255;
                    uint32_t g_t = (fg_g * alpha + bg_g * (255 - alpha)) / 255;
                    uint32_t b_t = (fg_b * alpha + bg_b * (255 - alpha)) / 255;
                    
                    fb[fb_offset] = 0xFF000000 | (r_t << 16) | (g_t << 8) | b_t;
                }
            }
        }
    }
}

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

        //Create displaysimple-gpu-drv.c surface from framebuffer data
        if(g->fb_bpp == 32){

            //Create a temporary buffer for compositing cursor
            uint32_t *temp_buffer = g_malloc(g->fb_width * g->fb_height * 4);
            memcpy(temp_buffer, fb_data, g->fb_width * g->fb_height * 4);

            //Comosite cursor onto the framebuffer
            composite_cursor(g, temp_buffer);

            DisplaySurface *fb_surface = qemu_create_displaysurface_from(
                    g->fb_width, g->fb_height, PIXMAN_a8r8g8b8,
                    g->fb_pitch, (uint8_t*)temp_buffer);
            dpy_gfx_replace_surface(g->console, fb_surface);

            g_free(temp_buffer);
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

    //Initialize cursor 
    g->cursor_enabled = 0;
    g->cursor_x = 0;
    g->cursor_y = 0;
    g->cursor_upload_offset = 0;
    init_default_cursor(g);
    
    //Initialize Mutliple framebuffer state
    g->fb_count = 1;        //start with single buffer
    g->fb_current = 0;
    g->fb_next = 0;
    g->flip_pending = 0;
    g->vblank_count = 0;
    g->fb_addresses[0] = 0; //first framebuffer at offset 0;

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
