#include "camer.h"
#include "engine.h"          // for Engine struct   needed to reach mainCamera via userPointer
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <algorithm>
#include <cmath>

// ============================================================
//  Helpers
// ============================================================

static float clampAngle(float v, float lo, float hi) {
    return std::max(lo, std::min(hi, v));
}

// ============================================================
//  Camera public API
// ============================================================

void Camera::_updateVectors()
{
    glm::vec3 f;
    f.x = std::cos(glm::radians(yaw)) * std::cos(glm::radians(pitch));
    f.y = std::sin(glm::radians(pitch));
    f.z = std::sin(glm::radians(yaw)) * std::cos(glm::radians(pitch));
    front = glm::normalize(f);
    right = glm::normalize(glm::cross(front, worldUp));
    up = glm::normalize(glm::cross(right, front));
}

// %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
void Camera::_syncOrbitFromPose()
{
    // Recompute orbitDistance and orbitTarget so orbit picks up
    // where FPS left off, preventing a snap on mode switch.
    orbitDistance = 5.0f;   // reasonable default distance
    orbitTarget = position + front * orbitDistance;
}

// %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
void Camera::focusOn(glm::vec3 center, float radius)
{
    orbitTarget = center;
    // Stand back far enough to see the whole model comfortably
    orbitDistance = radius * 2.5f;
    if (orbitDistance < 1.0f) orbitDistance = 1.0f;

    // Start slightly above and in front
    yaw = -90.0f;
    pitch = -15.0f;
    _updateVectors();

    position = orbitTarget - front * orbitDistance;
    fov = 45.0f;
}

// %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
glm::mat4 Camera::getViewMatrix() const
{
    return glm::lookAt(position, position + front, up);
}

glm::mat4 Camera::getProjectionMatrix(float aspect, float nearPlane, float farPlane) const
{
    return glm::perspective(glm::radians(fov), aspect, nearPlane, farPlane);
}

// ============================================================
//  Orbit helpers
// ============================================================

void Camera::_orbitDrag(float dx, float dy)
{
    yaw += dx * orbitSensitivity;
    pitch -= dy * orbitSensitivity;    // subtract so dragging up looks up
    pitch = clampAngle(pitch, -89.0f, 89.0f);
    _updateVectors();

    // Recompute position from angles + target + distance
    position = orbitTarget - front * orbitDistance;
}

void Camera::_pan(float dx, float dy)
{
    // Scale pan speed by distance so near objects don't fly out of frame
    float scale = orbitDistance * panSensitivity;
    orbitTarget -= right * (dx * scale);
    orbitTarget += up * (dy * scale);
    position = orbitTarget - front * orbitDistance;
}

// ============================================================
//  FPS helpers
// ============================================================

void Camera::_enterFPS(GLFWwindow* window)
{
    mode = CameraMode::FPS;
    firstMouse = true;               // prevent snap on entry
    velocity = glm::vec3(0.0f);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
}

void Camera::_exitFPS(GLFWwindow* window)
{
    mode = CameraMode::ORBIT;
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    // Sync orbit state so the transition is seamless
    _syncOrbitFromPose();
}

void Camera::_fpsTick(GLFWwindow* window)
{
    // Direction input
    glm::vec3 wishDir(0.0f);
    bool shift = (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS);
    float speed = movementSpeed * (shift ? 3.0f : 1.0f);

    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) wishDir += front;
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) wishDir -= front;
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) wishDir -= right;
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) wishDir += right;
    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) wishDir += worldUp;
    if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS) wishDir -= worldUp;

    glm::vec3 targetVelocity(0.0f);
    if (glm::length(wishDir) > 0.0f)
        targetVelocity = glm::normalize(wishDir) * speed;

    // Smooth deceleration   lerp velocity toward target each frame
    float t = 1.0f - std::exp(-dampingFactor * deltaTime);
    velocity = glm::mix(velocity, targetVelocity, t);

    position += velocity * deltaTime;

    // Keep orbitTarget in sync so a mode switch won't snap
    orbitTarget = position + front * orbitDistance;
}

// ============================================================
//  Per-frame update   call this BEFORE engine_draw_frame
// ============================================================

void Camera::update(GLFWwindow* window)
{
    // %% Delta time %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
    double now = glfwGetTime();
    deltaTime = (float)(now - lastFrameTime);
    lastFrameTime = now;
    // Guard against huge spikes (e.g. first frame, or debugger pauses)
    if (deltaTime > 0.1f) deltaTime = 0.1f;

    // %% FPS keyboard movement %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
    if (mode == CameraMode::FPS) {
        _fpsTick(window);
    }
}

// ============================================================
//  Callbacks (called from the GLFW dispatch below)
// ============================================================

void Camera::_onKey(GLFWwindow* window, int key, int action)
{
    if (action != GLFW_PRESS) return;

    if (key == GLFW_KEY_TAB) {
        if (mode == CameraMode::ORBIT) _enterFPS(window);
        else                           _exitFPS(window);
        return;
    }
    if (key == GLFW_KEY_ESCAPE && mode == CameraMode::FPS) {
        _exitFPS(window);
        return;
    }
    if (key == GLFW_KEY_F) {
        // Re-focus on origin   useful if you get lost
        focusOn(glm::vec3(0.0f), orbitDistance * 0.4f);
        return;
    }
}

void Camera::_onMouseButton(GLFWwindow* window, int button, int action)
{
    if (mode == CameraMode::FPS) return;   // FPS uses mouse move only

    if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        rightMouseDown = (action == GLFW_PRESS);
        firstMouse = true;   // prevent snap on new drag
    }
    if (button == GLFW_MOUSE_BUTTON_MIDDLE) {
        middleMouseDown = (action == GLFW_PRESS);
        firstMouse = true;
    }
}

void Camera::_onMouseMove(GLFWwindow* window, double xPos, double yPos)
{
    float x = (float)xPos;
    float y = (float)yPos;

    if (firstMouse) {
        lastMouseX = x;
        lastMouseY = y;
        firstMouse = false;
        return;   // skip the big initial delta
    }

    float dx = x - lastMouseX;
    float dy = y - lastMouseY;
    lastMouseX = x;
    lastMouseY = y;

    if (mode == CameraMode::FPS) {
        // Look around in FPS mode
        yaw += dx * mouseSensitivity;
        pitch += dy * -mouseSensitivity;   // inverted Y feels natural
        pitch = clampAngle(pitch, -89.0f, 89.0f);
        _updateVectors();
        return;
    }

    // %% Orbit mode %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
    if (rightMouseDown)  _orbitDrag(dx, dy);
    if (middleMouseDown) _pan(dx, dy);
}

void Camera::_onScroll(GLFWwindow* window, double yOffset)
{
    if (ImGui::GetIO().WantCaptureMouse) return;

    if (mode == CameraMode::FPS) {
        // Scroll adjusts FPS speed in FPS mode
        movementSpeed *= (1.0f + (float)yOffset * 0.1f);
        movementSpeed = std::max(1.0f, std::min(movementSpeed, 500.0f));
        return;
    }

    // %% Orbit: dolly in/out %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
    // Multiply distance by a factor so zooming is proportional
    // (moving the same "percentage" of distance feels natural).
    float factor = 1.0f - (float)yOffset * scrollSensitivity;
    orbitDistance *= factor;
    orbitDistance = std::max(0.1f, orbitDistance);   // never clip through target

    position = orbitTarget - front * orbitDistance;
}

// ============================================================
//  GLFW callback registration
//  The window user pointer must already be Engine*.
//  We access engine->mainCamera from each callback.
// ============================================================

void setupCameraCallbacks(GLFWwindow* window)
{
    // Key callback   extend the existing engine key handler to also forward to camera.
    // We install a NEW combined callback that handles both the engine->keys[] array
    // AND the camera mode switches. This replaces the lambda in init_vulkan, so you
    // should REMOVE the glfwSetKeyCallback call from init_vulkan (or call this after it).
    glfwSetKeyCallback(window, [](GLFWwindow* w, int key, int scancode, int action, int mods) {
        ImGui_ImplGlfw_KeyCallback(w, key, scancode, action, mods);

        Engine* e = static_cast<Engine*>(glfwGetWindowUserPointer(w));
        if (!e) return;

        // Fill the engine key table (used by any other engine systems)
        if (key >= 0 && key < 1024) {
            if (action == GLFW_PRESS)   e->keys[key] = true;
            if (action == GLFW_RELEASE) e->keys[key] = false;
        }

        // Forward to camera
        e->mainCamera._onKey(w, key, action);
        });

    glfwSetMouseButtonCallback(window, [](GLFWwindow* w, int button, int action, int mods) {
        ImGui_ImplGlfw_MouseButtonCallback(w, button, action, mods);
        if (ImGui::GetIO().WantCaptureMouse) return;

        Engine* e = static_cast<Engine*>(glfwGetWindowUserPointer(w));
        if (e) e->mainCamera._onMouseButton(w, button, action);
        });

    glfwSetCursorPosCallback(window, [](GLFWwindow* w, double xPos, double yPos) {
        ImGui_ImplGlfw_CursorPosCallback(w, xPos, yPos);
        if (ImGui::GetIO().WantCaptureMouse) return;

        Engine* e = static_cast<Engine*>(glfwGetWindowUserPointer(w));
        if (e) e->mainCamera._onMouseMove(w, xPos, yPos);
        });

    glfwSetScrollCallback(window, [](GLFWwindow* w, double xOffset, double yOffset) {
        ImGui_ImplGlfw_ScrollCallback(w, xOffset, yOffset);

        Engine* e = static_cast<Engine*>(glfwGetWindowUserPointer(w));
        if (e) e->mainCamera._onScroll(w, yOffset);
        });
}