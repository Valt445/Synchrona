#include "engine.h"
#include "debug_ui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"
#include <imgui.h>


int main()
{
    Engine engineInstance;
    engine = &engineInstance;
     
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_FALSE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    
    init(engine, 3840, 2160);
    glfwSwapInterval(0);
    while (!glfwWindowShouldClose(engine->window))
    {
        glfwPollEvents();

        // Skip rendering while the window is minimised
        if (glfwGetWindowAttrib(engine->window, GLFW_ICONIFIED) != 0)
            continue;
        engine_draw_frame(engine);
    }

    vkDeviceWaitIdle(engine->device);
    engine_cleanup(engine);
    return 0;
}