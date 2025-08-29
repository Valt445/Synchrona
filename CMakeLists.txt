cmake_minimum_required(VERSION 3.10)
project(MyVulkanEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
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
    src/images.cpp
    src/vma.cpp
    src/helper.cpp
    src/descriptors.cpp
    external/vk-bootstrap/src/VkBootstrap.cpp
)

target_include_directories(my_vulkan_engine PRIVATE
    include
    ${GLFW_INCLUDE_DIRS}
    ${Vulkan_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/external/vk-bootstrap/src
      # header-only
    ${CMAKE_CURRENT_SOURCE_DIR}/external/VulkanMemoryAllocator/include  # header-only
)

target_link_libraries(my_vulkan_engine PRIVATE
    Vulkan::Vulkan
    ${GLFW_LIBRARIES}
)

find_program(GLSLC_EXECUTABLE glslc)
if(NOT GLSLC_EXECUTABLE)
    message(FATAL_ERROR "glslc not found! Please install glslang-tools.")
endif()

file(GLOB SHADER_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.comp")
set(SPIRV_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)

foreach(SHADER ${SHADER_SOURCES})
    get_filename_component(FILE_NAME ${SHADER} NAME_WE) # NAME_WE removes extension
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
