# GPU Driver Development: Learning Project

A hands-on learning project for understanding GPU driver development, starting from basic framebuffer drivers and progressing toward modern graphics stack integration.

## What We Have Built

### 🖥️ QEMU Virtual GPU Device (simple-gpu.c)
- **Complete virtual PCI GPU device** (1122:1122)
- 16MB VRAM allocation with memory-mapped I/O
- **Hardware cursor support** with 64x64 ARGB pixels
- **Page flipping registers** for smooth animation
- **Multiple framebuffer management** (up to 4 buffers)
- VBlank synchronization and tear-free rendering
- Integrated into QEMU build system

### 🔧 Simple GPU Kernel Driver (simple-gpu-drv.c)
- **Production-quality Linux kernel driver** (1122:1122)
- Character device interface (`/dev/simple-gpu`)
- **Complete IOCTL interface**:
  - `0x1000`: Setup framebuffer (resolution, color depth)
  - `0x1001`: Enable/disable display
  - `0x1002`: Get VRAM size
  - `0x1003-0x1006`: Hardware cursor control
  - `0x1007`: Setup multiple framebuffers
  - `0x1008`: Page flip for smooth animation
  - `0x1009`: Wait for flip completion
  - `0x100A`: Get framebuffer information
- **Page flipping support** for tear-free rendering
- **Hardware cursor implementation** with alpha blending
- PCI device probe and resource management
- VRAM mapping for userspace access via `mmap()`
- C89 compatibility and proper error handling

### 🎮 Test Applications
- **test-app.c**: Animated validation program
- Real-time color-cycling rectangle animation
- Direct framebuffer manipulation
- Device detection and error reporting

## Current Capabilities

✅ **Virtual Hardware**: QEMU device emulates real GPU hardware with page flipping  
✅ **Kernel Driver**: Production-quality driver with complete feature set  
✅ **Memory Management**: VRAM allocation and userspace mapping  
✅ **Display Output**: Tear-free rendering with hardware cursor support  
✅ **Page Flipping**: Smooth animation with double/quad buffering  
✅ **Hardware Cursor**: 64x64 ARGB cursor with hotspot control  
✅ **Development Workflow**: Complete build, load, test cycle established

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
2. Driver sets up multiple framebuffers via IOCTL
3. App maps VRAM to userspace via mmap()
4. App draws to back buffer while front buffer displays
5. Page flip atomically switches buffers for tear-free rendering
6. Hardware cursor composited in QEMU device

**Page Flipping Pipeline**:
```
Frame N:   Draw to FB0 → Page flip → Display FB0
Frame N+1: Draw to FB1 → Page flip → Display FB1
Frame N+2: Draw to FB0 → Page flip → Display FB0
```

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
