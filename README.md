![Uploading b.png…]()
![Uploading a.png…]()

# Synchrona Graphics Engine
A high-performance C++ rendering engine utilizing the Vulkan API, designed for low-overhead graphics processing and efficient resource management.

---

### Project Architecture
The project is architected as a modular system split into two distinct components:

*   **The Engine**: This component contains the core rendering logic and system abstractions. It compiles into a library file (`.lib` or `.a`) to be linked by the client application.
*   **The Game**: This component serves as the executable frontend. It contains the `main` entry point, application-specific logic, and all scene assets including models, textures, and shaders.

---

### Core Technical Features
*   **Physically Based Rendering (PBR)**
*   **Image Based Lighting (IBL)**
*   **Multisample Anti-Aliasing (MSAA)**
*   **Dynamic Rendering**
*   **Bindless Descriptors**
*   **Compute Shader Integration**
*   **Skybox Rendering**
*   **Acceleration Structures**
*   **HDRI Skybox Support (8k)**
*   **GLTF and GLB Asset Loading**
*   **ImGui Diagnostic Interface**

---

### Performance Benchmarks

**Test Environment A (Discrete GPU)**
*   **GPU**: NVIDIA GeForce RTX 3060
*   **Resolution**: 3840 x 2160 (4K)
*   **Anti-Aliasing**: 2x MSAA
*   **Lighting**: 8k HDRI

**Results (RTX 3060)**
*   **Intel Sponza (with curtains)**: 40 FPS
*   **Damaged Helmet**: 100 FPS

---

**Test Environment B (Apple Silicon)**
*   **SoC**: Apple M4
*   **OS**: macOS
*   **Resolution**: 2560 x 1440 (1440p)
*   **Lighting**: 8k HDRI

**Results (M4)**
*   **Damaged Helmet**: 40 FPS

---

### Comparative Performance Analysis
Synchrona is optimized for high-throughput graphics with minimal system tax compared to commercial general-purpose engines.

*   **CPU Utilization**: Synchrona maintains a consistent 4% CPU overhead during high-load 4K rendering. 
*   **Commercial Baseline (Unity)**: Standard Unity implementations (URP/HDRP) typically exhibit significantly higher CPU overhead—often ranging from 15% to 25%—due to additional abstraction layers and managed code overhead in similar 5.7M triangle scenes.
*   **Optimization Strategy**: By utilizing bindless descriptors and custom memory management, Synchrona reduces draw call overhead and bypasses traditional engine abstraction layers to maximize GPU saturation.

---

### Compilation and Deployment
The engine utilizes CMake for cross-platform build automation.

**Manual Build from Source**
*   Clone the repository to the local environment.
*   Initialize CMake from the root directory.
*   Compile the Engine library first, then link it with the Game executable.

**Automated Build Scripts**
*   **Windows**: Execute `build.bat` to initiate the compilation process.
*   **Linux/macOS**: Execute `build.sh` to initiate the compilation process.

---

### Cross-Platform Support
The codebase maintains separate builds and configurations to ensure functional parity across multiple operating systems:
*   **Windows**
*   **Linux**
*   **macOS**

---

### Current Development Objectives
*   **Bare-Metal Porting**: Transitioning the rendering pipeline to a custom ARM64 kernel to eliminate operating system overhead.
*   **Hardware Acceleration**: Implementation of Vulkan Ray Queries for real-time intersection testing.
*   **Gaussian Splatting**: Integration of volumetric point cloud rendering for high-fidelity environmental reconstruction.

---

**Lead Developer**: Manan Bhardwaj
