@echo off
echo === SYNCHRONA BUILD (FIXED) ===

echo Checking submodules...
if exist "external\glfw\CMakeLists.txt" (
    echo ✓ GLFW found
) else (
    echo ✗ GLFW missing
)

if exist "external\glm\glm\glm.hpp" (
    echo ✓ GLM found  
) else (
    echo ✗ GLM missing
)

if exist "external\imgui\imgui.cpp" (
    echo ✓ ImGui found
) else (
    echo ✗ ImGui missing
)

if exist "external\cgltf\cgltf.h" (
    echo ✓ cgltf found
) else (
    echo ✗ cgltf missing
)

echo.
echo Building engine...
cmake -B build -DCMAKE_BUILD_TYPE=Debug
if %errorlevel% neq 0 (
    echo Build failed!
    pause
    exit /b 1
)

cmake --build build --config Debug
echo ✓ Build successful!

pause