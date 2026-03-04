# ⚡ Vulkan Game Engine

A handcrafted game engine built from scratch in C++ and Vulkan. No Unity. No Unreal. No shortcuts. Just raw GPU metal and the ambition to build something that can compete with both.

---

## The Vision

This engine exists because modern game engines are black boxes. You press buttons, something happens, you don't know why. This is the opposite of that — every system written by hand, every bug hunted down to the instruction level, full ownership of everything from the GPU command buffer to the game loop.

The goal is an engine that matches Unity in graphical capability, surpasses it in rendering quality, and gets out of your way when you're making a game. Minimal UI code burden on the developer. Pure C++. No scripting layer tax. No bloat.

The graphics target is path traced, physically correct light — where what you see on screen is what light actually does in the real world. RTX hardware acceleration. The kind of visuals that make you stop and look.

The gameplay target is simplicity. A clean ECS, physics, audio, input — everything you need to ship a real game and nothing you don't.

---

## Current State

The rendering foundation is solid and running. It currently chews through scenes like Sponza and San Miguel at 60fps locked on mid-range hardware — 7+ million triangles, full PBR materials, shadows, atmospheric sky, all running on a raw Vulkan renderer with no engine scaffolding yet.

- **Vulkan 1.3** — dynamic rendering, synchronization2, timeline semaphores
- **Bindless descriptor system** — single global descriptor set, all textures in one array
- **PBR shading** — Cook-Torrance BRDF, GGX distribution, Schlick fresnel, Smith geometry
- **PCF shadow mapping** — 2048×2048 depth map, 3×3 kernel soft shadows
- **Normal mapping** — TBN matrix, Gram-Schmidt re-orthogonalization, per-material strength
- **glTF 2.0 loader** — full scene graph traversal, PBR material binding, tangent generation
- **ACES tone mapping** — filmic curve with configurable exposure
- **Atmospheric sky** — compute shader with turbidity, sun direction and intensity controls
- **Alpha cutout** — foliage and masked materials
- **VMA memory management** — GPU-only resources, persistent mapped upload buffers
- **Dear ImGui debug UI** — live pipeline switching, performance counters

---

## Rendering Roadmap

### Image Based Lighting — next
HDR environment capture, diffuse irradiance convolution, GGX prefiltered specular map, BRDF LUT. The single biggest visual quality jump available right now. Proper sky reflections on metal, coloured bounce light, the full split-sum approximation.

### Mipmap generation
Full mip chain via `vkCmdBlitImage` at load time. Eliminates aliasing and shimmer at distance.

### Screen Space Ambient Occlusion
Hemisphere depth sampling, contact shadows, makes scenes feel grounded. Huge perceived quality jump at low cost.

### Cascaded Shadow Maps
3-4 depth cascades covering different distance ranges. Sharp shadows up close, smooth coverage at distance.

### Temporal Anti-Aliasing
Sub-pixel projection jitter, history accumulation, velocity buffer reprojection. Best quality-to-cost AA available.

### Bloom
HDR downsample, bright pixel threshold, gaussian blur, composite. A large part of why modern games look the way they do.

### GPU-driven rendering
`vkCmdDrawIndexedIndirect` with a GPU-side compute culling pass. Removes the CPU submission bottleneck entirely and opens the door to massive scene complexity.

### Ray Traced Ambient Occlusion
Vulkan ray tracing extensions, short AO rays from the GBuffer, real geometric occlusion. The RTX 3060 has dedicated RT cores — might as well use them.

### Full Path Tracing
The endgame for the renderer. Hardware RTX acceleration, unbiased light transport, caustics, interreflections, the works. The kind of image quality that is currently only possible offline — made realtime.

---

## Engine Roadmap

### Entity Component System
Clean data-oriented ECS. Fast iteration, cache-friendly component storage, no inheritance hell. The backbone everything else plugs into.

### Physics
Rigid body simulation, collision detection, raycasts. Enough to make real games. Probably integrating a proven library rather than writing a physics engine from scratch — that's a different project entirely.

### Audio
3D positional audio, streaming, effects. The part everyone forgets until the game feels completely empty without it.

### Input system
Keyboard, mouse, gamepad. Event driven and polling both. Simple.

### Minimal developer UI
The engine provides a thin layer for in-game UI — enough to build menus, HUDs, debug overlays. Pure C++, no markup language, no retained mode nightmare. You write code, UI appears.

### Scene management
Scene graph, serialization, asset hot-reloading. Load a scene, change a file, see it update without restarting.

### Developer tooling
An editor — eventually. Not soon. The renderer has to be worth editing things in first.

---

## Built With

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

*Built to understand how everything works. Built to eventually beat the engines that don't let you.*

---

THIS README WAS WRITTEN BY AI. THE HUMAN WAS BUSY ACTUALLY BUILDING THE ENGINE.