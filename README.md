# ⚡ Vulkan Renderer

A handcrafted real-time PBR renderer built from scratch in C++ and Vulkan. No engine. No shortcuts. Just raw GPU metal.

---

## What it is

This is a personal rendering engine built to understand how modern renderers actually work under the hood — every system written by hand, every bug hunted down to the instruction level. It runs on Vulkan 1.3 with dynamic rendering, bindless resources, and a fully custom asset pipeline.

It currently chews through scenes like Sponza and San Miguel at 60fps locked on mid-range hardware without frustum culling — purely brute forcing 7+ million triangles a frame while the real work gets built out underneath.

---

## What's in it right now

- **Vulkan 1.3** — dynamic rendering, synchronization2, timeline semaphores
- **Bindless descriptor system** — single global descriptor set, all textures in one array
- **PBR shading** — Cook-Torrance BRDF, GGX distribution, Schlick fresnel, Smith geometry
- **PCF shadow mapping** — 2048×2048 depth map, 3×3 kernel soft shadows, configurable bias
- **Normal mapping** — TBN matrix, Gram-Schmidt re-orthogonalization, per-material strength
- **glTF 2.0 loader** — full scene graph traversal, PBR material binding, tangent generation
- **ACES tone mapping** — filmic curve with configurable exposure
- **Atmospheric sky** — compute shader background with turbidity and sun direction controls
- **VMA memory management** — GPU-only resources, persistent mapped upload buffers
- **Dear ImGui** — debug UI with live pipeline switching and performance counters
- **Alpha cutout** — foliage and masked materials via discard threshold

---

## The vision

The goal is a renderer that can stand next to modern production engines in visual quality while staying small enough that one person can understand every line of it. No magic. No black boxes.

The north star is physically correct light — where what you see on screen is what light actually does in the real world. Not approximations stacked on approximations but a genuine simulation of how photons interact with surfaces.

---

## The road ahead

### Next — Image Based Lighting
Replace the hardcoded hemisphere ambient with real environment lighting. HDR cubemap capture, diffuse irradiance convolution, GGX prefiltered specular, BRDF LUT. The jump in visual quality from this alone is enormous — proper sky reflections on metal, coloured bounce light in archways, the full split-sum approximation the split-sum approximation from Epic's 2013 paper.

### GPU-driven rendering
Move draw call generation entirely to the GPU. `vkCmdDrawIndexedIndirect` with a compute shader culling pass. The CPU currently submits every draw call manually — that's fine for now but it's the ceiling. GPU-driven removes that ceiling entirely and opens the door to massive scene complexity.

### Frustum + occlusion culling
Bounding volume hierarchy on the CPU for frustum culling, GPU occlusion queries for the rest. Currently everything in the scene gets drawn unconditionally. This is the next big performance unlock.

### Mipmap generation
Full mip chain generation via `vkCmdBlitImage` at load time. Currently all textures are single level which means aliasing and shimmer at distance and worse GPU cache utilisation. This is a 30-line fix with a visible quality impact.

### Screen Space Ambient Occlusion
Sample the depth buffer in a hemisphere around each fragment, estimate how occluded it is. Darkens corners, crevices, contact shadows. Makes scenes feel grounded and real without the cost of ray tracing.

### Bloom
Downsample the HDR framebuffer, threshold bright pixels, gaussian blur, composite back. Makes emissive surfaces and specular highlights glow naturally. A huge part of why modern games look the way they do.

### Temporal Anti-Aliasing
Jitter the projection matrix sub-pixel each frame, accumulate history with exponential blending, reject disoccluded samples with velocity buffer reprojection. Best quality-to-cost ratio of any AA technique.

### Ray Traced Ambient Occlusion
Use Vulkan ray tracing extensions to trace short AO rays directly from the GBuffer. The RTX 3060 has dedicated RT cores that make this viable at full resolution. Real geometric occlusion instead of the screen-space approximation.

### Cascaded Shadow Maps
Replace the single shadow map with 3-4 cascades covering different distance ranges. The current single frustum has to cover the entire scene which wastes resolution on distant shadows. Cascades give sharp shadows up close and smooth coverage at distance.

### Point and spot lights
A light list, tiled or clustered light assignment, multiple shadow-casting lights. The current single directional sun is a stepping stone.

### Lumen-style dynamic GI
The endgame. Radiance caching, world-space probes, screen-space irradiance. This is where it gets genuinely hard and genuinely interesting.

---

## Built with

- C++23
- Vulkan 1.3
- VMA (Vulkan Memory Allocator)
- vk-bootstrap
- cgltf
- glm
- stb_image
- Dear ImGui
- GLFW

---

*Built to understand rendering. Not to ship a product.*

YES IT IS WRITTEN BY AI BUT WHY DO EXTRA WORK FOR THIS TYPE OF THING
