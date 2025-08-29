#include "engine.hpp"
#include <stdio.h>

int main() {
    Engine engine;
    init(&engine,1234, 1234); 

    while (!glfwWindowShouldClose(engine.window)) {
        engine_draw_frame(&engine);
        glfwPollEvents();
    }

    engine_cleanup(&engine);
    return 0;
}

