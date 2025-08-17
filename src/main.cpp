#include "engine.hpp"
#include <stdio.h>

int main() {
    Engine engine;
    if (!engine_init(&engine)) {
        fprintf(stderr, "Engine init failed.\n");
        return -1;
    }

    while (!glfwWindowShouldClose(engine.window)) {
        engine_draw_frame(&engine);
        glfwPollEvents();
    }

    engine_cleanup(&engine);
    return 0;
}

