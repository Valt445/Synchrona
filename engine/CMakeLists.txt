cmake_minimum_required(VERSION 3.20)
project(Engine VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

add_compile_definitions(GLM_ENABLE_EXPERIMENTAL)
add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)

find_package(Vulkan REQUIRED)

# ----------------------------------------------------------------------------- 
# DEPENDENCIES - WITH DEBUG OUTPUT
# -----------------------------------------------------------------------------

message(STATUS "=== CHECKING DEPENDENCIES ===")

# GLFW - Check with debug output
set(GLFW_PATH ${CMAKE_CURRENT_SOURCE_DIR}/external/glfw)
message(STATUS "Looking for GLFW at: ${GLFW_PATH}")

if(EXISTS ${GLFW_PATH}/CMakeLists.txt)
    message(STATUS "✓ GLFW found at: ${GLFW_PATH}")
    add_subdirectory(external/glfw)
    set(GLFW_LIBRARIES glfw)
else()
    message(FATAL_ERROR "✗ GLFW not found at: ${GLFW_PATH}")
endif()

# GLM - Check with debug output  
set(GLM_PATH ${CMAKE_CURRENT_SOURCE_DIR}/external/glm/glm/glm.hpp)
message(STATUS "Looking for GLM at: ${GLM_PATH}")

if(EXISTS ${GLM_PATH})
    set(GLM_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/glm)
    message(STATUS "✓ GLM found")
else()
    message(FATAL_ERROR "✗ GLM not found at: ${GLM_PATH}")
endif()

# ImGui - Check with debug output
set(IMGUI_PATH ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui/imgui.cpp)
message(STATUS "Looking for ImGui at: ${IMGUI_PATH}")

if(EXISTS ${IMGUI_PATH})
    set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui)
    set(IMGUI_SOURCES
        ${IMGUI_DIR}/imgui.cpp
        ${IMGUI_DIR}/imgui_draw.cpp
        ${IMGUI_DIR}/imgui_tables.cpp
        ${IMGUI_DIR}/imgui_widgets.cpp
        ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
        ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp
    )
    message(STATUS "✓ ImGui found")
else()
    message(FATAL_ERROR "✗ ImGui not found at: ${IMGUI_PATH}")
endif()

# cgltf - Check with debug output
set(CGLTF_PATH ${CMAKE_CURRENT_SOURCE_DIR}/external/cgltf/cgltf.h)
message(STATUS "Looking for cgltf at: ${CGLTF_PATH}")

if(EXISTS ${CGLTF_PATH})
    set(CGLTF_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/cgltf)
    message(STATUS "✓ cgltf found")
else()
    message(FATAL_ERROR "✗ cgltf not found at: ${CGLTF_PATH}")
endif()

# Optional dependencies
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/external/vk-bootstrap/CMakeLists.txt)
    add_subdirectory(external/vk-bootstrap)
    message(STATUS "✓ vk-bootstrap found")
else()
    message(STATUS "⚠ vk-bootstrap not found")
endif()

# Set Visual Studio debug working directory


if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/external/VulkanMemoryAllocator/CMakeLists.txt)
    add_subdirectory(external/VulkanMemoryAllocator)
    message(STATUS "✓ VMA found")
else()
    message(STATUS "⚠ VMA not found")
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/external/fastgltf/include/fastgltf/fastgltf.hpp)
    set(FASTGLTF_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/fastgltf/include)
    message(STATUS "✓ fastgltf found")
else()
    message(STATUS "⚠ fastgltf not found")
    set(FASTGLTF_INCLUDE_DIR "")
endif()

message(STATUS "=== ALL DEPENDENCIES CHECKED ===")

# ----------------------------------------------------------------------------- 
# ENGINE LIBRARY
# -----------------------------------------------------------------------------

# Get header files FIRST
file(GLOB HEADER_FILES "include/*.h")

set(ENGINE_SOURCES
    src/engine.cpp
    src/images.cpp
    src/vma.cpp
    src/helper.cpp
    src/descriptors.cpp
    src/debug_ui.cpp
    src/graphics_pipeline.cpp
    src/loader.cpp
    src/texture_loader.cpp         # <-- texture loader TU (implements load_texture_from_file)
    src/stb_image_impl.cpp
    src/camer.cpp             
    src/pipelines.cpp 
    src/swapchain.cpp
    src/textures.cpp
    src/vulkan_core.cpp
    src/rendering.cpp
    src/memory.cpp
    src/mesh.cpp
    src/commands_and_sync.cpp
    src/immediate_submit.cpp
    src/imgui_integration.cpp
    ${IMGUI_SOURCES}
    ${HEADER_FILES}  # ADD HEADERS HERE
)

add_library(Engine STATIC ${ENGINE_SOURCES})

# Prefer explicit STB include dir; adjust to where your stb_image.h actually lives.
set(STB_POSSIBLE_DIRS
    "${CMAKE_CURRENT_SOURCE_DIR}/external/stb"
    "${CMAKE_CURRENT_SOURCE_DIR}/external/stb_image"
    "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/stb"
)

set(STB_INCLUDE_DIR "")
foreach(dir IN LISTS STB_POSSIBLE_DIRS)
    if(EXISTS "${dir}/stb_image.h")
        set(STB_INCLUDE_DIR ${dir})
        break()
    endif()
endforeach()

if(STB_INCLUDE_DIR)
    message(STATUS "✓ stb_image.h found at: ${STB_INCLUDE_DIR}")
else()
    message(WARNING "⚠ stb_image.h not found in expected locations. Place stb_image.h into engine/include or external/stb and re-run CMake.")
endif()

target_include_directories(Engine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${IMGUI_DIR}
    ${GLM_INCLUDE_DIR}
    ${CGLTF_INCLUDE_DIR}
    $<IF:$<BOOL:${STB_INCLUDE_DIR}>,${STB_INCLUDE_DIR},>
)

target_include_directories(Engine PUBLIC 
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

if(FASTGLTF_INCLUDE_DIR)
    target_include_directories(Engine PUBLIC ${FASTGLTF_INCLUDE_DIR})
endif()

target_link_libraries(Engine PUBLIC
    Vulkan::Vulkan
    ${GLFW_LIBRARIES}
)

if(TARGET vk-bootstrap)
    target_link_libraries(Engine PUBLIC vk-bootstrap)
endif()

if(TARGET VulkanMemoryAllocator)
    target_link_libraries(Engine PUBLIC VulkanMemoryAllocator)
endif()

if(WIN32)
    target_link_libraries(Engine PUBLIC
        d3dcompiler.lib
        dxguid.lib
    )
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(Engine PUBLIC 
        VULKAN_DEBUG=1
        _DEBUG
    )
    
    if(MSVC)
        target_compile_options(Engine PRIVATE /Zi /Od /W4)
        target_link_options(Engine PRIVATE /DEBUG)
    endif()
endif()

# Copy assets and shaders
add_custom_command(TARGET Engine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Engine>/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/assets/" "$<TARGET_FILE_DIR:Engine>/assets/"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Game>/shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/engine/shaders" "$<TARGET_FILE_DIR:Game>/shaders"
    COMMENT "Copying assets and shaders to build directory"
)

message(STATUS "🎉 Engine configured successfully!")