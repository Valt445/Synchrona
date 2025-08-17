
cmake_minimum_required(VERSION 3.10)
project(MyVulkanEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

# Vulkan
find_package(Vulkan REQUIRED)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# GLFW
find_package(PkgConfig REQUIRED)
pkg_search_module(GLFW REQUIRED glfw3)

add_executable(my_vulkan_engine
    src/main.cpp
    src/engine.cpp
)

if(APPLE)
    # Homebrew default paths
    set(GLFW_INCLUDE_DIR /opt/homebrew/include)
    set(GLFW_LIBRARY /opt/homebrew/lib/libglfw.3.dylib)
endif()

target_include_directories(my_vulkan_engine PRIVATE
    include
    ${GLFW_INCLUDE_DIR}
    external/vk-bootstrap/src   # include VkBootstrap headers
)

target_link_libraries(my_vulkan_engine PRIVATE
    Vulkan::Vulkan
    ${GLFW_LIBRARY}
)

# Ensure VkBootstrap uses KHR types on macOS
target_compile_definitions(my_vulkan_engine PRIVATE VKB_FORCE_KHR)
