# Engine Refactoring - Function Extraction Guide

## Safety First
- Original engine.cpp backed up as `engine_original.cpp`
- All line numbers refer to the original engine.cpp
- Copy code exactly as shown - no changes
- Each subsystem file needs the includes shown

---

## FILE: vulkan_core.cpp

**Purpose**: Vulkan instance, device, physical device selection, window creation

**Copy these includes at the top:**
```cpp
#include "engine.h"
#include "VkBootstrap.h"
#include <cstdlib>
#include <iostream>

Engine* engine = nullptr;  // IMPORTANT: Define global engine pointer HERE
```

**Copy this function (lines 53-185):**
```
void init_vulkan(Engine* e)
```
- Starts: Line 53 - `void init_vulkan(Engine* e) {`
- Ends: Line 185 - closing brace before `void init_pipelines`
- **Total**: 133 lines

---

## FILE: swapchain.cpp

**Purpose**: Swapchain creation, destruction, resizing

**Copy these includes at the top:**
```cpp
#include "engine.h"
#include "VkBootstrap.h"
#include <iostream>
#include <algorithm>
#include <cstdio>
```

**Copy these 4 functions:**

1. `destroy_swapchain()` - Lines 592-622
   - Starts: Line 592 - `void destroy_swapchain(Engine* e) {`
   - Ends: Line 622 - closing brace
   - **Total**: 31 lines

2. `create_swapchain()` - Lines 624-690
   - Starts: Line 624 - `void create_swapchain(Engine* e, uint32_t width, uint32_t height) {`
   - Ends: Line 690 - `std::printf("✅ Swapchain created...`
   - **Total**: 67 lines

3. `init_swapchain()` - Lines 691-783
   - Starts: Line 691 - `void init_swapchain(Engine* e, uint32_t width, uint32_t height) {`
   - Ends: Line 783 - `std::printf("Swapchain initialized...`
   - **Total**: 93 lines

4. `resize_swapchain()` - Lines 785-833
   - Starts: Line 785 - `void resize_swapchain(Engine* e) {`
   - Ends: Line 833 - closing brace before `void init_commands`
   - **Total**: 49 lines

---

## FILE: commands_and_sync.cpp

**Purpose**: Command pools, synchronization primitives, helper functions

**Copy these includes at the top:**
```cpp
#include "engine.h"
#include <cstdio>
#include <iostream>
```

**Copy these functions:**

1. `init_commands()` - Lines 835-871
   - **Total**: 37 lines

2. `fence_create_info()` - Lines 873-878
   - **Total**: 6 lines

3. `semaphore_create_info()` - Lines 880-885
   - **Total**: 6 lines

4. `init_sync_structures()` - Lines 887-901
   - **Total**: 15 lines

5. `command_buffer_info()` - Lines 1150-1155
   - **Total**: 6 lines

6. `semaphore_submit_info()` - Lines 1157-1165
   - **Total**: 9 lines

7. `command_buffer_submit_info()` - Lines 1167-1173
   - **Total**: 7 lines

8. `get_current_frame()` - Lines 1189-1192
   - **Total**: 4 lines

9. `submit_info()` - Lines 1175-1187
   - **Total**: 13 lines

---

## FILE: descriptors.cpp

**Purpose**: Descriptor allocator setup and management

**Copy these includes at the top:**
```cpp
#include "engine.h"
#include <cstdio>
```

**Copy this function (lines 903-938):**
```
void init_descriptors(Engine* engine)
```
- **Total**: 36 lines

---

## FILE: pipelines.cpp

**Purpose**: Pipeline creation and shader compilation

**Copy these includes at the top:**
```cpp
#include "engine.h"
#include "graphics_pipeline.h"
#include <cstdio>
```

**Copy these functions:**

1. `init_pipelines()` - Lines 186-188
   - **Total**: 3 lines

2. `init_background_pipelines()` - Lines 940-1081
   - **Total**: 142 lines (LARGE)

3. `init_mesh_pipelines()` - Lines 1083-1148
   - **Total**: 66 lines

---

## FILE: memory.cpp

**Purpose**: Buffer and image allocation/destruction using VMA

**Copy these includes at the top:**
```cpp
#include "engine.h"
#include <cstdio>
#include <iostream>
```

**Copy these functions:**

1. `create_buffer()` - Lines 248-294
   - **Total**: 47 lines

2. `destroy_buffer()` - Lines 296-299
   - **Total**: 4 lines

3. `create_image()` - Two overloads:
   a. Lines 421-455 (no data)
   b. Lines 457-505 (with data)
   - **Total**: ~85 lines

4. `destroy_image()` - Lines 507-515
   - **Total**: 9 lines

---

## FILE: mesh.cpp

**Purpose**: Mesh loading and GPU buffer uploads

**Copy these includes at the top:**
```cpp
#include "engine.h"
#include <span>
#include <iostream>
```

**Copy this function (lines 301-398):**
```
GPUMeshBuffers uploadMesh(Engine* e, std::span<uint32_t> indices, std::span<Vertex> vertices)
```
- **Total**: 98 lines

---

## FILE: textures.cpp

**Purpose**: Texture loading and bindless descriptor management

**Copy these includes at the top:**
```cpp
#include "engine.h"
```

**Copy this function (lines 1281-1411):**
```
void upload_texture_to_bindless(Engine* e, AllocatedImage img, VkSampler sampler, uint32_t index)
```
- **Total**: 131 lines

---

## FILE: rendering.cpp

**Purpose**: Frame rendering, geometry drawing, ImGui

**Copy these includes at the top:**
```cpp
#include "engine.h"
#include "graphics_pipeline.h"
#include "imgui.h"
#include <glm/ext/matrix_transform.hpp>
```

**Copy these functions:**

1. `draw_geometry()` - Lines 1217-1279
   - **Total**: 63 lines

2. `draw_background()` - Lines 1413-1433
   - **Total**: 21 lines

3. `draw_imgui()` - Lines 1434-1451
   - **Total**: 18 lines

4. `engine_draw_frame()` - Lines 1454-1567
   - **Total**: 114 lines

5. `attachment_info()` - Lines 819-833
   - **Total**: 15 lines

---

## FILE: immediate_submit.cpp

**Purpose**: Immediate GPU command execution

**Copy these includes at the top:**
```cpp
#include "engine.h"
#include <functional>
```

**Copy this function (lines 1193-1215):**
```
void immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function, Engine* e)
```
- **Total**: 23 lines

---

## FILE: imgui_integration.cpp

**Purpose**: ImGui initialization

**Copy these includes at the top:**
```cpp
#include "engine.h"
#include "imgui.h"
#include <GLFW/glfw3.h>
#include <cstdint>
```

**Copy this function (lines 190-246):**
```
void init_imgui(Engine* e)
```
- **Total**: 57 lines

---

## FILE: engine.cpp (CORE/MAIN)

**Purpose**: Coordinator functions, initialization order, cleanup

**Keep all original includes from lines 1-18:**
```cpp
#include "engine.h"
#include "VkBootstrap.h"
#include "graphics_pipeline.h"
#include "imgui.h"
#include <GLFW/glfw3.h>
#include <cstdint>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <memory>
#include <types.h>
#include <vulkan/vulkan_core.h>
#include <iostream>
#include "loader.h"
#include <glm/packing.hpp>
#include "texture_loader.h"

Engine* engine = nullptr;  // Keep this here OR remove if defined in vulkan_core.cpp
```

**Keep these functions:**

1. `init()` - Lines 23-49
   - **Total**: 27 lines

2. `create_draw_image()` - Lines 517-590
   - **Total**: 74 lines

3. `destroy_draw_image()` - Lines 400-415
   - **Total**: 16 lines

4. `init_default_data()` - Lines 1290-1411
   - **Total**: 122 lines (LARGE)

5. `engine_cleanup()` - Lines 1569-1615
   - **Total**: 47 lines

---

## INTEGRATION STEPS

### Step 1: Create Directory
Place all these files in your project:
```
engine.h              (refactored, reorganized)
engine.cpp            (core: init, cleanup, etc.)
vulkan_core.cpp       (Vulkan initialization)
swapchain.cpp         (Swapchain management)
commands_and_sync.cpp (Commands & sync)
descriptors.cpp       (Descriptors)
pipelines.cpp         (Pipelines)
memory.cpp            (Memory allocation)
mesh.cpp              (Mesh loading)
textures.cpp          (Textures)
rendering.cpp         (Rendering)
immediate_submit.cpp  (Immediate submit)
imgui_integration.cpp (ImGui)
```

### Step 2: Update Build System
In CMakeLists.txt or your build system, add all .cpp files.

### Step 3: Compile
```bash
cmake . && make
```

### Step 4: Verify
- Code compiles cleanly
- No linker errors
- Application runs unchanged

---

## Important Notes

1. **Only ONE `Engine* engine = nullptr;`**
   - Define it in vulkan_core.cpp (preferred)
   - Remove from engine.cpp if you move it
   - This is the global engine pointer

2. **Include Order**
   - Every .cpp file includes engine.h FIRST
   - engine.h includes types.h, loader.h, etc.
   - Other includes are subsystem-specific

3. **No Behavior Changes**
   - Code is copied exactly as-is
   - No modifications to algorithms
   - No type changes
   - Binary-identical behavior

4. **Gradual Integration**
   - You don't have to do all files at once
   - Can start with vulkan_core.cpp + engine.cpp
   - Add other subsystems one by one
   - Test after each addition

---

## Troubleshooting

**"Undefined reference to init_vulkan"**
- Make sure vulkan_core.cpp is in your build
- Check CMakeLists.txt includes the file

**"Multiple definitions of Engine* engine"**
- Remove `Engine* engine = nullptr;` from all but ONE file
- Keep it in vulkan_core.cpp

**"Header not found" errors**
- Make sure all includes (VkBootstrap.h, graphics_pipeline.h, etc.) are in your path
- These are from your existing project, not new

---

## What NOT To Do

❌ Don't modify includes (except adding subsystem headers)
❌ Don't change function signatures
❌ Don't move code within functions
❌ Don't change types or struct definitions
❌ Don't try to optimize while refactoring

✅ Do copy code exactly as shown
✅ Do test compilation after each file
✅ Do check that functions are called in correct order
✅ Do verify no duplicate global definitions

---

## Next Steps After Refactoring

Once this is working, you can:

1. **Add PBR Pipeline**
   - Create `pipelines_pbr.cpp`
   - Add to init()
   - No need to touch core files

2. **Add New Features**
   - Create new subsystem files
   - Include engine.h
   - Call from appropriate init() location

3. **Further Optimization**
   - Extract more functions
   - Create scene management
   - Modularize rendering

The refactoring is **complete**, and you have a clean foundation to build on!
