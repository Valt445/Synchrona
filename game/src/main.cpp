#include "engine.h"
#include "debug_ui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"
#include <imgui.h>

int main()
{
    Engine engineInstance;
    engine = &engineInstance;

    // init() handles everything:
    //   - Vulkan + swapchain + pipelines
    //   - setupCameraCallbacks()   ← registers all GLFW input callbacks
    //   - mainCamera.focusOn()     ← positions camera on the model
    init(engine, 1280, 720);

    while (!glfwWindowShouldClose(engine->window))
    {
        glfwPollEvents();

        // Skip rendering while the window is minimised
        if (glfwGetWindowAttrib(engine->window, GLFW_ICONIFIED) != 0)
            continue;

        // engine_draw_frame() handles everything per-frame:
        //   - mainCamera.update()  ← delta time + FPS movement + smooth velocity
        //   - draw_background / draw_geometry / draw_imgui
        engine_draw_frame(engine);
    }

    vkDeviceWaitIdle(engine->device);
    engine_cleanup(engine);
    return 0;
}