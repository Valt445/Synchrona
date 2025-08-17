cmake_minimum_required(VERSION 3.10)
project(MyVulkanEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Vulkan
find_package(Vulkan REQUIRED)

# GLFW (use pkg-config on Linux, manual on macOS)
find_package(PkgConfig REQUIRED)
if(APPLE)
    set(GLFW_INCLUDE_DIR /opt/homebrew/include)
    set(GLFW_LIBRARY /opt/homebrew/lib/libglfw.3.dylib)
else()
    pkg_search_module(GLFW REQUIRED glfw3)
endif()

add_executable(my_vulkan_engine
    src/main.cpp
    src/engine.cpp
    external/vk-bootstrap/src/VkBootstrap.cpp
)

target_include_directories(my_vulkan_engine PRIVATE
    include
    ${GLFW_INCLUDE_DIRS}
    ${Vulkan_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/external/vk-bootstrap/src  # header-only
)

target_link_libraries(my_vulkan_engine PRIVATE
    Vulkan::Vulkan
    ${GLFW_LIBRARIES}
)

# Force vk-bootstrap to use KHR suffixes on platforms with older headers
target_compile_definitions(my_vulkan_engine PRIVATE VKB_FORCE_KHR)
