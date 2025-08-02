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

struct fb_params {
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
};


int main(int argc, char *argv[])
{
    int fd;
    struct fb_params params = {800, 600, 32};
    uint32_t vram_size;
    uint32_t *framebuffer;
    const char *device_name; 
    
    /* Check for command line argument */
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
    
    if (ioctl(fd, IOCTL_SETUP_FB, &params) < 0) {
        perror("Failed to setup framebuffer");
        close(fd);
        return 1;
    }
    printf("Framebuffer setup: %dx%d@%dbpp\n", params.width, params.height, params.bpp);
    
    framebuffer = mmap(NULL, vram_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (framebuffer == MAP_FAILED) {
        perror("Failed to map VRAM");
        close(fd);
        return 1;
    }
    printf("VRAM mapped successfully\n");
    
    
    if (ioctl(fd, IOCTL_ENABLE_DISP, 1) < 0) {
        perror("Failed to enable display");
    } else {
        printf("Display enabled!\n");
    }

    for (int frame = 0; ; frame++) { 
        
        memset(framebuffer, 0, params.width * params.height * 4);
        
        int rect_x = (frame * 2) % (params.width - 100);
        int rect_y = 250;
        
 
        uint32_t color = 0xFF000000 | 
                        ((frame * 4) % 256) << 16 |   
                        ((frame * 2) % 256) << 8 |    
                        ((frame * 1) % 256);       
        
        for (int y = rect_y; y < rect_y + 100 && y < params.height; y++) {
            for (int x = rect_x; x < rect_x + 100 && x < params.width; x++) {
                framebuffer[y * params.width + x] = color;
            }
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
