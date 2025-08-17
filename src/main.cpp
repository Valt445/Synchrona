#include "engine.hpp"
#include <stdio.h>

int main() {
    Engine engine;
    init(&engine, 1200, 720); 

    while (!glfwWindowShouldClose(engine.window)) {
        engine_draw_frame(&engine);
        glfwPollEvents();
    }

    engine_cleanup(&engine);
    return 0;
}

