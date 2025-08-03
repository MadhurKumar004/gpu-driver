#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <string.h>

#define IOCTL_SETUP_FB      0x1000
#define IOCTL_ENABLE_DISP   0x1001
#define IOCTL_GET_VRAM_SIZE 0x1002
#define IOCTL_SETUP_MULTI_FB 0x1007
#define IOCTL_PAGE_FLIP     0x1008

struct fb_params {
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
};

struct multi_fb_setup {
    uint32_t fb_count;
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
};

struct flip_request {
    uint32_t fb_index;
    uint32_t wait_vblank;
};


int main(int argc, char *argv[])
{
    int fd;
    struct multi_fb_setup setup = {2, 800, 600, 32}; /* 2 framebuffers for double buffering */
    struct flip_request flip;
    uint32_t vram_size;
    uint32_t *framebuffer;
    uint32_t *fb0, *fb1;
    const char *device_name; 
    
    if (argc > 1) {
        device_name = argv[1];
        printf("Using device: %s\n", device_name);
    } else {
        printf("Usage: %s [device_name]\n", argv[0]);
        return 0;
    }
    
    printf("Simple GPU Test Application\n");
    
    fd = open(device_name, O_RDWR);
    if (fd < 0) {
        perror("Failed to open device");
        printf("Make sure the device exists and you have permissions\n");
        printf("Try: sudo %s %s\n", argv[0], device_name);
        return 1;
    }
    
    if (ioctl(fd, IOCTL_GET_VRAM_SIZE, &vram_size) < 0) {
        perror("Failed to get VRAM size");
        close(fd);
        return 1;
    }
    printf("VRAM size: %u bytes (%u MB)\n", vram_size, vram_size / (1024 * 1024));
    
    /* Setup double buffering */
    if (ioctl(fd, IOCTL_SETUP_MULTI_FB, &setup) < 0) {
        perror("Failed to setup multiple framebuffers");
        close(fd);
        return 1;
    }
    printf("Double buffering setup: %dx%d@%dbpp\n", setup.width, setup.height, setup.bpp);
    
    framebuffer = mmap(NULL, vram_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (framebuffer == MAP_FAILED) {
        perror("Failed to map VRAM");
        close(fd);
        return 1;
    }
    printf("VRAM mapped successfully\n");
    
   
    fb0 = framebuffer;                                    /* First framebuffer at offset 0 */
    fb1 = framebuffer + (setup.width * setup.height);    /* Second framebuffer after first */
    
    if (ioctl(fd, IOCTL_ENABLE_DISP, 1) < 0) {
        perror("Failed to enable display");
    } else {
        printf("Display enabled!\n");
        printf("Starting smooth animation with page flipping...\n");
        printf("Press Ctrl+C to stop\n");
    }

    for (int frame = 0; ; frame++) { 
        /* Use double buffering - alternate between fb0 and fb1 */
        int current_fb = frame % 2;
        uint32_t *back_buffer = (current_fb == 0) ? fb0 : fb1;
        
        /* Clear back buffer */
        memset(back_buffer, 0, setup.width * setup.height * 4);
        
        /* Calculate rectangle position */
        int rect_x = (frame * 2) % (setup.width - 100);
        int rect_y = 250;
        
        /* Draw colorful rectangle to back buffer */
        uint32_t color = 0xFF000000 | 
                        ((frame * 4) % 256) << 16 |   
                        ((frame * 2) % 256) << 8 |    
                        ((frame * 1) % 256);       
        
        for (int y = rect_y; y < rect_y + 100 && y < setup.height; y++) {
            for (int x = rect_x; x < rect_x + 100 && x < setup.width; x++) {
                back_buffer[y * setup.width + x] = color;
            }
        }
        
        /* Page flip to display the new frame */
        flip.fb_index = current_fb;
        flip.wait_vblank = 0;
        
        if (ioctl(fd, IOCTL_PAGE_FLIP, &flip) < 0) {
            perror("Page flip failed");
            break;
        }
        
        usleep(16667);
    }
    
    ioctl(fd, IOCTL_ENABLE_DISP, 0);
    printf("Display disabled\n");
    
    munmap(framebuffer, vram_size);
    close(fd);
    
    printf("Test completed successfully!\n");
    return 0;
}
