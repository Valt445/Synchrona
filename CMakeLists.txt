cmake_minimum_required(VERSION 3.10)
set(CMAKE_C_COMPILER /usr/bin/gcc)
set(CMAKE_CXX_COMPILER /usr/bin/clang++)
project(MyVulkanEngine LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# -----------------------------------------------------------------------------
# Module and Dependency Management
# -----------------------------------------------------------------------------
# Vulkan
find_package(Vulkan REQUIRED)

# GLFW
find_package(PkgConfig REQUIRED)
pkg_search_module(GLFW REQUIRED glfw3)
if(NOT GLFW_FOUND)
    message(FATAL_ERROR "GLFW not found! Please install libglfw3-dev.")
endif()

# ImGui
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui)
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp
)

# -----------------------------------------------------------------------------
# Submodule Integration
# -----------------------------------------------------------------------------
# cgltf - We'll handle this differently since it's a single-header C library

# These submodules contain their own CMakeLists.txt and build into a library.
add_subdirectory(external/vk-bootstrap)
add_subdirectory(external/VulkanMemoryAllocator)

# -----------------------------------------------------------------------------
# Main Executable
# -----------------------------------------------------------------------------
add_executable(my_vulkan_engine
    src/main.cpp
    src/engine.cpp
    src/images.cpp
    src/vma.cpp
    src/helper.cpp
    src/descriptors.cpp
    src/debug_ui.cpp
    src/graphics_pipeline.cpp
    src/loader.cpp
    ${IMGUI_SOURCES}
    # Note: We DON'T add cgltf.c directly to the executable sources
    # Instead, we'll include it through one of our .cpp files
)

# Link all submodules and libraries
target_link_libraries(my_vulkan_engine PRIVATE
    Vulkan::Vulkan
    ${GLFW_LIBRARIES}
    vk-bootstrap
    VulkanMemoryAllocator
)

# Set include directories
target_include_directories(my_vulkan_engine PRIVATE
    include
    ${GLFW_INCLUDE_DIRS}
    ${Vulkan_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/external/cgltf  # For cgltf.h
    ${CMAKE_CURRENT_SOURCE_DIR}/external/vk-bootstrap/src
    ${CMAKE_CURRENT_SOURCE_DIR}/external/VulkanMemoryAllocator/include
    ${IMGUI_DIR}
    external/glm
    
)

add_custom_command(
    TARGET my_vulkan_engine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/assets
        ${CMAKE_CURRENT_BINARY_DIR}/assets
    COMMENT "Copying assets to build directory"
)

# Optional: Enable warnings for debugging
target_compile_options(my_vulkan_engine PRIVATE -Wall -Wextra)

# -----------------------------------------------------------------------------
# Shader Compilation
# -----------------------------------------------------------------------------
find_program(GLSLC_EXECUTABLE glslc)
if(NOT GLSLC_EXECUTABLE)
    message(FATAL_ERROR "glslc not found! Please install glslang-tools.")
endif()

file(GLOB SHADER_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.vert"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.frag"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.comp"
)

set(SPIRV_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
foreach(SHADER ${SHADER_SOURCES})
    get_filename_component(FILE_NAME ${SHADER} NAME)
    set(SPIRV_FILE ${SPIRV_OUTPUT_DIR}/${FILE_NAME}.spv)
    add_custom_command(
        OUTPUT ${SPIRV_FILE}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SPIRV_OUTPUT_DIR}
        COMMAND ${GLSLC_EXECUTABLE} ${SHADER} -o ${SPIRV_FILE}
        DEPENDS ${SHADER}
        COMMENT "Compiling shader ${FILE_NAME}"
        VERBATIM
    )
    list(APPEND SPIRV_BINARIES ${SPIRV_FILE})
endforeach()

add_custom_target(Shaders ALL DEPENDS ${SPIRV_BINARIES})
add_dependencies(my_vulkan_engine Shaders)
