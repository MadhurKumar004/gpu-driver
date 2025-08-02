# GPU Driver Development: Learning Project

A hands-on learning project for understanding GPU driver development, starting from basic framebuffer drivers and progressing toward modern graphics stack integration.

## What We Have Built

### 🖥️ QEMU Virtual GPU Device
- **gray-gpu.c**: Complete virtual PCI GPU device (1122:1122)
- 16MB VRAM allocation with memory-mapped I/O
- Register interface for device control
- Display refresh and framebuffer management
- Integrated into QEMU build system

### 🔧 Gray GPU Kernel Driver
- **gray_drv.c**: Working Linux kernel driver (1122:1122)
- Character device interface (`/dev/gray-gpu`)
- Custom IOCTL commands:
  - `0x1000`: Setup framebuffer (resolution, color depth)
  - `0x1001`: Enable/disable display
  - `0x1002`: Get VRAM size
- PCI device probe and resource management
- VRAM mapping for userspace access via `mmap()`
- Proper error handling and cleanup

### 🎮 Test Applications
- **test-app.c**: Animated validation program
- Real-time color-cycling rectangle animation
- Direct framebuffer manipulation
- Device detection and error reporting

## Current Capabilities

 **Virtual Hardware**: QEMU device emulates real GPU hardware  
 **Kernel Driver**: Functional driver with PCI integration  
 **Memory Management**: VRAM allocation and userspace mapping  
 **Display Output**: Working framebuffer with smooth animation  
 **Development Workflow**: Build, load, test cycle established

## Project Structure
```
gpu-driver/
├── qemu-device/          # Virtual GPU hardware (gray-gpu.c)
├── gray-gpu-driver/      # Kernel driver (gray_drv.c, Kconfig, Makefile)
├── userspace-apps/       # Test applications (test-app.c)
└── README.md             # This file
```

### QEMU Setup
The virtual GPU device needs to be compiled into QEMU with PCI ID 1122:1122 to work with our driver.

## Technical Architecture

**Current Implementation**:
```
test-app.c → /dev/gray-gpu → gray_drv.c → QEMU Virtual GPU
```

**Data Flow**:
1. Test app opens character device
2. Driver maps VRAM to userspace via mmap()
3. App draws directly to framebuffer memory
4. QEMU device displays the framebuffer

## What We Are Approaching

### 🎯 Next Major Milestone: DRM Framework Integration

**Goal**: Convert from simple character device to modern DRM subsystem

**Why This Matters**: DRM (Direct Rendering Manager) is the foundation of all modern Linux graphics drivers. Real GPU drivers don't use custom character devices - they integrate with the DRM framework.


### Learning Objectives

**Short Term**: Understand the transition from simple drivers to DRM framework  
**Medium Term**: Master modern GPU driver architecture (KMS + GEM)  
**Long Term**: Build production-ready graphics driver with full 3D support

**End Goal**: A complete understanding of how real GPU drivers work in production Linux systems, from hardware abstraction to userspace graphics libraries.

## License

GPL v2 (Linux kernel compatibility)
