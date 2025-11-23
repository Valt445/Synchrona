project(Game)

add_executable(Game src/main.cpp)
set_property(TARGET Game PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:Game>")


# Just link with engine - that's it!
target_link_libraries(Game PRIVATE Engine)

# Install assets to the binary directory
install(DIRECTORY ${CMAKE_SOURCE_DIR}/assets/
    DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/assets
)

# Install shaders to the binary directory
install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/shaders/
    DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/shaders
)

# Create install target
add_custom_target(deploy_all
    COMMAND ${CMAKE_COMMAND} --install . --config $<CONFIG>
    COMMENT "Deploying all assets"
)

add_dependencies(Game deploy_all)
# Add ImGui includes for the game
target_include_directories(Game PRIVATE
    ${CMAKE_SOURCE_DIR}/engine/external/imgui
    ${CMAKE_SOURCE_DIR}/engine/external/imgui/backends
)

# Shader compilation for game executable
find_program(GLSL_VALIDATOR glslangValidator PATHS "C:/VulkanSDK/*/Bin" "C:/VulkanSDK/*/bin")
if(NOT GLSL_VALIDATOR)
    message(WARNING "glslangValidator not found - shaders won't be compiled to SPIR-V")
else()
    message(STATUS "Found glslangValidator: ${GLSL_VALIDATOR}")
    
    # Define shader output directory
    set(SHADER_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
    
    # Create shader output directory
    file(MAKE_DIRECTORY ${SHADER_OUTPUT_DIR})
    
    # Find all shader files
    file(GLOB_RECURSE SHADER_SOURCES 
        "${CMAKE_SOURCE_DIR}/engine/shaders/*.vert"
        "${CMAKE_SOURCE_DIR}/engine/shaders/*.frag" 
        "${CMAKE_SOURCE_DIR}/engine/shaders/*.comp"
        "${CMAKE_SOURCE_DIR}/engine/shaders/*.tesc"
        "${CMAKE_SOURCE_DIR}/engine/shaders/*.tese"
        "${CMAKE_SOURCE_DIR}/engine/shaders/*.geom"
    )
    
    # Compile each shader to SPIR-V
    foreach(shader ${SHADER_SOURCES})
        get_filename_component(shader_name ${shader} NAME_WE)
        get_filename_component(shader_ext ${shader} EXT)
        
        # Map file extensions to shader stages
        if(shader_ext STREQUAL ".vert")
            set(stage vert)
        elseif(shader_ext STREQUAL ".frag")
            set(stage frag)
        elseif(shader_ext STREQUAL ".comp")
            set(stage comp)
        elseif(shader_ext STREQUAL ".tesc")
            set(stage tesc)
        elseif(shader_ext STREQUAL ".tese")
            set(stage tese)
        elseif(shader_ext STREQUAL ".geom")
            set(stage geom)
        else()
            set(stage "unknown")
        endif()
        
        if(NOT stage STREQUAL "unknown")
            set(spv_file ${SHADER_OUTPUT_DIR}/${shader_name}.${stage}.spv)
            
            add_custom_command(
                OUTPUT ${spv_file}
                COMMAND ${GLSL_VALIDATOR} -V ${shader} -o ${spv_file} --target-env vulkan1.3
                DEPENDS ${shader}
                COMMENT "Compiling ${shader_name}${shader_ext} to SPIR-V"
                VERBATIM
            )
            
            list(APPEND SPIRV_SHADERS ${spv_file})
        endif()
    endforeach()
    
    # Create a custom target for shader compilation
    add_custom_target(compile_shaders ALL DEPENDS ${SPIRV_SHADERS})
    
    # Make Game depend on shader compilation
    add_dependencies(Game compile_shaders)
    
    # Copy compiled shaders to game output
    add_custom_command(TARGET Game POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:Game>/assets
    COMMENT "Copying assets to Game output folder"
    )

    add_custom_command(TARGET Game POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${SHADER_OUTPUT_DIR}
            $<TARGET_FILE_DIR:Game>/shaders
    )


endif()