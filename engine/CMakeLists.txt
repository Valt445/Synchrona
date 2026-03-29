cmake_minimum_required(VERSION 3.20)
project(Engine VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# -----------------------------------------------------------------------------
# COMPILER CACHE (sccache preferred on Windows, ccache elsewhere)
# Must come before any target definitions.
# -----------------------------------------------------------------------------

find_program(CCACHE_PROGRAM sccache ccache)
if(CCACHE_PROGRAM)
    message(STATUS "✓ Using compiler cache: ${CCACHE_PROGRAM}")
    set(CMAKE_C_COMPILER_LAUNCHER   ${CCACHE_PROGRAM})
    set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
else()
    message(STATUS "⚠ No compiler cache found (install sccache or ccache for faster rebuilds)")
endif()

add_compile_definitions(GLM_ENABLE_EXPERIMENTAL)
add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)

find_package(Vulkan REQUIRED)

<<<<<<< HEAD
# -----------------------------------------------------------------------------
# DEPENDENCIES - WITH DEBUG OUTPUT
=======
# ----------------------------------------------------------------------------- 
# DEPENDENCIES
>>>>>>> origin/main
# -----------------------------------------------------------------------------

message(STATUS "=== CHECKING DEPENDENCIES ===")

set(GLFW_PATH ${CMAKE_CURRENT_SOURCE_DIR}/external/glfw)
if(EXISTS ${GLFW_PATH}/CMakeLists.txt)
    message(STATUS "✓ GLFW found")
    add_subdirectory(external/glfw)
    set(GLFW_LIBRARIES glfw)
else()
    message(FATAL_ERROR "✗ GLFW not found at: ${GLFW_PATH}")
endif()

<<<<<<< HEAD
# GLM - Check with debug output
=======
>>>>>>> origin/main
set(GLM_PATH ${CMAKE_CURRENT_SOURCE_DIR}/external/glm/glm/glm.hpp)
if(EXISTS ${GLM_PATH})
    set(GLM_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/glm)
    message(STATUS "✓ GLM found")
else()
    message(FATAL_ERROR "✗ GLM not found at: ${GLM_PATH}")
endif()

set(IMGUI_PATH ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui/imgui.cpp)
if(EXISTS ${IMGUI_PATH})
    set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui)
    set(IMGUI_SOURCES
        ${IMGUI_DIR}/imgui.cpp
        ${IMGUI_DIR}/imgui_draw.cpp
        ${IMGUI_DIR}/imgui_tables.cpp
        ${IMGUI_DIR}/imgui_widgets.cpp
        ${IMGUI_DIR}/imgui_demo.cpp
        ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
        ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp
    )
    message(STATUS "✓ ImGui found")
else()
    message(FATAL_ERROR "✗ ImGui not found at: ${IMGUI_PATH}")
endif()

set(CGLTF_PATH ${CMAKE_CURRENT_SOURCE_DIR}/external/cgltf/cgltf.h)
if(EXISTS ${CGLTF_PATH})
    set(CGLTF_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/cgltf)
    message(STATUS "✓ cgltf found")
else()
    message(FATAL_ERROR "✗ cgltf not found at: ${CGLTF_PATH}")
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/external/vk-bootstrap/CMakeLists.txt)
    add_subdirectory(external/vk-bootstrap)
    message(STATUS "✓ vk-bootstrap found")
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/external/VulkanMemoryAllocator/CMakeLists.txt)
    add_subdirectory(external/VulkanMemoryAllocator)
    message(STATUS "✓ VMA found")
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/external/fastgltf/include/fastgltf/fastgltf.hpp)
    set(FASTGLTF_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/fastgltf/include)
    message(STATUS "✓ fastgltf found")
else()
    set(FASTGLTF_INCLUDE_DIR "")
endif()

<<<<<<< HEAD
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
=======
>>>>>>> origin/main
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
    message(WARNING "⚠ stb_image.h not found")
endif()

message(STATUS "=== ALL DEPENDENCIES CHECKED ===")

# ----------------------------------------------------------------------------- 
# IMGUI — isolated static library so it never recompiles with Engine changes
# -----------------------------------------------------------------------------

add_library(imgui_lib STATIC ${IMGUI_SOURCES})
target_include_directories(imgui_lib PUBLIC
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)
target_link_libraries(imgui_lib PUBLIC Vulkan::Vulkan ${GLFW_LIBRARIES})
set_target_properties(imgui_lib PROPERTIES FOLDER "External")

# -----------------------------------------------------------------------------
# SLANG COMPILER
# Use prebuilt binaries from https://github.com/shader-slang/slang/releases
# to avoid building Slang from source (saves 5–15 minutes on a clean build).
#
# PREBUILT (default, fast):
#   Download a release zip, extract it to external/slang-prebuilt/ so that
#   external/slang-prebuilt/bin/slangc[.exe] exists, then configure normally.
#
# SOURCE BUILD (fallback):
#   If no prebuilt dir is found, the source submodule at external/slang is
#   used instead. Slower but requires no manual download.
# -----------------------------------------------------------------------------

message(STATUS "=== SETTING UP SLANG ===")

set(SLANG_PREBUILT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/slang-prebuilt")

if(WIN32)
    set(_SLANG_EXE "${SLANG_PREBUILT_DIR}/bin/slangc.exe")
else()
    set(_SLANG_EXE "${SLANG_PREBUILT_DIR}/bin/slangc")
endif()

if(EXISTS "${_SLANG_EXE}")
    # ── Prebuilt path ──────────────────────────────────────────────────────
    message(STATUS "✓ Slang prebuilt binary found at: ${_SLANG_EXE}")
    set(SLANG_COMPILER_GENEX "${_SLANG_EXE}")
    set(SLANG_RUNTIME_DEP "")   # plain executable, no CMake target to depend on
else()
    # ── Source-build fallback ──────────────────────────────────────────────
    message(STATUS "  Slang prebuilt not found — falling back to source build")
    message(STATUS "  (Tip: download a release from github.com/shader-slang/slang/releases")
    message(STATUS "   and extract to ${SLANG_PREBUILT_DIR} to skip this)")

    set(SLANG_SUBMODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/external/slang")
    if(NOT EXISTS "${SLANG_SUBMODULE_PATH}/CMakeLists.txt")
        message(FATAL_ERROR
            "✗ Neither prebuilt Slang nor the source submodule were found.\n"
            "  Option A (fast): download a release zip from\n"
            "    https://github.com/shader-slang/slang/releases\n"
            "    and extract to: ${SLANG_PREBUILT_DIR}\n"
            "  Option B (slow): add the submodule\n"
            "    git submodule add https://github.com/shader-slang/slang external/slang\n"
            "    git submodule update --init --recursive")
    endif()

    set(SLANG_ENABLE_TESTS               OFF CACHE BOOL "" FORCE)
    set(SLANG_ENABLE_EXAMPLES            OFF CACHE BOOL "" FORCE)
    set(SLANG_ENABLE_DOCS                OFF CACHE BOOL "" FORCE)
    set(SLANG_ENABLE_GFX                 OFF CACHE BOOL "" FORCE)
    set(SLANG_ENABLE_SLANGD              OFF CACHE BOOL "" FORCE)
    set(SLANG_ENABLE_SLANGRT             OFF CACHE BOOL "" FORCE)
    set(SLANG_ENABLE_PREBUILT_CORE_MODULE ON  CACHE BOOL "" FORCE)

    add_subdirectory(${SLANG_SUBMODULE_PATH} external/slang_build EXCLUDE_FROM_ALL)

    # Tidy up the VS solution — put every Slang target in one folder
    function(collect_targets_recursive DIR OUT_VAR)
        get_property(TARGETS DIRECTORY "${DIR}" PROPERTY BUILDSYSTEM_TARGETS)
        get_property(SUBDIRS DIRECTORY "${DIR}" PROPERTY SUBDIRECTORIES)
        set(ALL ${TARGETS})
        foreach(SUB IN LISTS SUBDIRS)
            collect_targets_recursive("${SUB}" SUB_TARGETS)
            list(APPEND ALL ${SUB_TARGETS})
        endforeach()
        set(${OUT_VAR} ${ALL} PARENT_SCOPE)
    endfunction()

    collect_targets_recursive("${SLANG_SUBMODULE_PATH}" _SLANG_TARGETS)
    foreach(T IN LISTS _SLANG_TARGETS)
        if(TARGET ${T})
            set_target_properties(${T} PROPERTIES FOLDER "External/Slang")
        endif()
    endforeach()

    set(SLANG_COMPILER_GENEX "$<TARGET_FILE:slangc>")
    if(TARGET slang)
        set(SLANG_RUNTIME_DEP slang)
    else()
        set(SLANG_RUNTIME_DEP "")
    endif()
endif()

message(STATUS "✓ Slang configured")

# -----------------------------------------------------------------------------
# SLANG SHADER COMPILATION
# One add_custom_command per shader, ONE SlangShaders target for all of them.
# -----------------------------------------------------------------------------

set(SLANG_OUTPUT_DIR "${CMAKE_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY "${SLANG_OUTPUT_DIR}")

set(SLANG_SPV_OUTPUTS "" CACHE INTERNAL "Accumulated Slang SPV outputs")

function(compile_slang_shader)
    cmake_parse_arguments(ARG "" "SOURCE;OUTPUT_NAME;STAGE;ENTRY" "" ${ARGN})

    if(NOT ARG_SOURCE OR NOT ARG_OUTPUT_NAME OR NOT ARG_STAGE OR NOT ARG_ENTRY)
        message(FATAL_ERROR "compile_slang_shader: SOURCE, OUTPUT_NAME, STAGE and ENTRY are all required")
    endif()

    set(SPV_OUT "${CMAKE_BINARY_DIR}/shaders/${ARG_OUTPUT_NAME}.spv")

    # Only add slangc as a file-level dependency when it's a real CMake target
    if(SLANG_RUNTIME_DEP)
        set(DEPS "${ARG_SOURCE}" ${SLANG_RUNTIME_DEP})
    else()
        set(DEPS "${ARG_SOURCE}")
    endif()

    add_custom_command(
        OUTPUT  "${SPV_OUT}"
        COMMAND "${SLANG_COMPILER_GENEX}"
                "${ARG_SOURCE}"
                -target  spirv
                -stage   "${ARG_STAGE}"
                -entry   "${ARG_ENTRY}"
                -profile spirv_1_5
                -emit-spirv-directly
                -matrix-layout-column-major
                -O2
                -o "${SPV_OUT}"
        DEPENDS  ${DEPS}
        COMMENT  "slangc → ${ARG_OUTPUT_NAME}.spv  [${ARG_STAGE}]"
        VERBATIM
    )

    set(SLANG_SPV_OUTPUTS ${SLANG_SPV_OUTPUTS} "${SPV_OUT}" CACHE INTERNAL "")
endfunction()

# ----------------------------------------------------------------------------- 
# ENGINE LIBRARY
# -----------------------------------------------------------------------------

# CONFIGURE_DEPENDS re-runs CMake when the glob result changes (new/deleted headers)
file(GLOB HEADER_FILES CONFIGURE_DEPENDS "include/*.h")

set(ENGINE_SOURCES
    src/engine.cpp
    src/images.cpp
    src/vma.cpp
    src/helper.cpp
    src/descriptors.cpp
    src/debug_ui.cpp
    src/graphics_pipeline.cpp
    src/loader.cpp
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
    src/ibl.cpp
    src/skybox.cpp
    # ImGui is now a separate static lib (imgui_lib) — not compiled here
    ${HEADER_FILES}
)

add_library(Engine STATIC ${ENGINE_SOURCES})

# Unity build — batches TUs to reduce redundant header parsing.
set_target_properties(Engine PROPERTIES
    UNITY_BUILD            ON
    UNITY_BUILD_BATCH_SIZE 8
)

# FIX LNK2019 — cgltf, stb_image and VMA are header-only libraries that require
# exactly one TU to define their implementation via a #define before the #include.
# Unity build merges TUs, so if any other file in the same batch includes the
# header first (without the define), the include guard fires and the implementation
# is never compiled.  Solution: compile these three files in isolation, outside
# of any unity batch.
set_source_files_properties(
    src/loader.cpp          # #define CGLTF_IMPLEMENTATION
    src/stb_image_impl.cpp  # #define STB_IMAGE_IMPLEMENTATION
    src/vma.cpp             # #define VMA_IMPLEMENTATION
    PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON
)

# FIX LNK2005 — upload_texture_to_bindless is defined (not just declared) in a
# header, so it ends up compiled in every unity batch that includes that header.
# Two ways to fix this in your source — pick whichever fits:
#
#   Option A (preferred): add `inline` to the definition in the header:
#       inline void upload_texture_to_bindless(...) { ... }
#
#   Option B: move the definition from the header into a single .cpp file and
#       keep only a declaration in the header.
#
# Once you've applied one of the options above, remove the line below.
set_source_files_properties(
    src/texture_loader.cpp  # ← most likely home of upload_texture_to_bindless;
                            #   change this to whichever .cpp actually defines it
                            #   if the linker error persists after the inline fix
    PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON
)

target_include_directories(Engine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${IMGUI_DIR}                        # kept so Engine headers that include imgui still resolve
    ${GLM_INCLUDE_DIR}
    ${CGLTF_INCLUDE_DIR}
    $<IF:$<BOOL:${STB_INCLUDE_DIR}>,${STB_INCLUDE_DIR},>
<<<<<<< HEAD
)

target_include_directories(Engine PUBLIC
=======
>>>>>>> origin/main
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

if(FASTGLTF_INCLUDE_DIR)
    target_include_directories(Engine PUBLIC ${FASTGLTF_INCLUDE_DIR})
endif()

# Precompiled header — heavy Vulkan/GLM includes parsed once per unity batch
target_precompile_headers(Engine PRIVATE
    <vulkan/vulkan.h>
    <glm/glm.hpp>
    <glm/gtc/matrix_transform.hpp>
    <glm/gtx/transform.hpp>
    <vector>
    <string>
    <memory>
    <unordered_map>
    <functional>
    <optional>
    <span>
)

target_link_libraries(Engine PUBLIC
    Vulkan::Vulkan
    ${GLFW_LIBRARIES}
    imgui_lib           # isolated ImGui static lib
)

if(TARGET vk-bootstrap)
    target_link_libraries(Engine PUBLIC vk-bootstrap)
endif()
if(TARGET VulkanMemoryAllocator)
    target_link_libraries(Engine PUBLIC VulkanMemoryAllocator)
endif()
if(WIN32)
    target_link_libraries(Engine PUBLIC d3dcompiler.lib dxguid.lib)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
<<<<<<< HEAD
    target_compile_definitions(Engine PUBLIC
        VULKAN_DEBUG=1
        _DEBUG
    )

=======
    target_compile_definitions(Engine PUBLIC VULKAN_DEBUG=1 _DEBUG)
>>>>>>> origin/main
    if(MSVC)
        target_compile_options(Engine PRIVATE /Zi /Od /W4)
        target_link_options(Engine PRIVATE /DEBUG)
    endif()
endif()

<<<<<<< HEAD
# Copy assets and shaders
# --- FIXED ASSET DEPLOYMENT ---
# We use CMAKE_BINARY_DIR to ensure it goes directly into your 'build' folder
add_custom_command(TARGET Engine POST_BUILD
    # Create the directories first
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/assets"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/shaders"

    # Copy the actual files
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets" "${CMAKE_BINARY_DIR}/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/shaders" "${CMAKE_BINARY_DIR}/shaders"

    COMMENT "🎉 Fixed: Copying assets and shaders to build directory without --config error"
)

message(STATUS "🎉 Engine configured successfully!")
=======
# ----------------------------------------------------------------------------- 
# REGISTER SLANG SHADERS
# -----------------------------------------------------------------------------

set(SHADER_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")

compile_slang_shader(
    SOURCE      "${SHADER_SRC_DIR}/colored_triangle_mesh_slang.slang"
    OUTPUT_NAME "colored_triangle_mesh_slang"
    STAGE       vertex
    ENTRY       vertexMain
)

compile_slang_shader(
    SOURCE      "${SHADER_SRC_DIR}/shadow.slang"
    OUTPUT_NAME "shadow_slang"
    STAGE       vertex
    ENTRY       vertexMain
)

compile_slang_shader(
    SOURCE      "${SHADER_SRC_DIR}/tex_images.slang"
    OUTPUT_NAME "tex_images_slang"
    STAGE       fragment
    ENTRY       fragmentMain
)

# ONE target for ALL slang shaders = ONE project in Visual Studio
add_custom_target(SlangShaders
    DEPENDS ${SLANG_SPV_OUTPUTS}
    COMMENT "Building all Slang shaders"
)
set_target_properties(SlangShaders PROPERTIES FOLDER "Shaders")
add_dependencies(Engine SlangShaders)

# ----------------------------------------------------------------------------- 
# POST-BUILD: copy assets and shaders to Game binary dir
# FIX: guarded behind if(TARGET Game) so this file can be configured
# standalone without the Game target being defined yet.
# -----------------------------------------------------------------------------

if(TARGET Game)
    add_custom_command(TARGET Engine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Game>/assets"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/assets/"
                "$<TARGET_FILE_DIR:Game>/assets/"

        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Game>/shaders"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/engine/shaders"
                "$<TARGET_FILE_DIR:Game>/shaders"

        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_BINARY_DIR}/shaders"
                "$<TARGET_FILE_DIR:Game>/shaders"

        COMMENT "Copying assets and shaders to Game binary directory"
    )
else()
    message(STATUS "⚠ 'Game' target not defined yet — post-build asset copy skipped.")
    message(STATUS "  Add add_dependencies(Game Engine) and the copy step in Game's CMakeLists.")
endif()

message(STATUS "🎉 Engine configured successfully!")
>>>>>>> origin/main
