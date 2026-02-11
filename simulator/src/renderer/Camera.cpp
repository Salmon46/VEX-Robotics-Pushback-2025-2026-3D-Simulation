#include "renderer/Camera.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>

void Camera::Init(float distance, float yaw, float pitch) {
  mDistance = distance;
  mYaw = yaw;
  mPitch = pitch;
  mTarget = glm::vec3(0.0f, 0.5f, 0.0f); // Slightly above ground
}

glm::vec3 Camera::GetEyePosition() const {
  float yawRad = glm::radians(mYaw);
  float pitchRad = glm::radians(mPitch);

  glm::vec3 offset;
  offset.x = mDistance * cos(pitchRad) * cos(yawRad);
  offset.y = mDistance * sin(pitchRad);
  offset.z = mDistance * cos(pitchRad) * sin(yawRad);

  return mTarget + offset;
}

glm::mat4 Camera::GetViewMatrix() const {
  return glm::lookAt(GetEyePosition(), mTarget, glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 Camera::GetProjectionMatrix(float aspectRatio) const {
  glm::mat4 proj =
      glm::perspective(glm::radians(mFov), aspectRatio, mNearPlane, mFarPlane);
  // Vulkan has inverted Y compared to OpenGL
  proj[1][1] *= -1.0f;
  return proj;
}

glm::mat4 Camera::GetViewProjection(float aspectRatio) const {
  return GetProjectionMatrix(aspectRatio) * GetViewMatrix();
}

void Camera::ProcessInput(GLFWwindow *window, float dt) {
  // --- Right-click drag to orbit ---
  double mouseX, mouseY;
  glfwGetCursorPos(window, &mouseX, &mouseY);

  if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
    if (!mDragging) {
      mDragging = true;
      mLastMouseX = mouseX;
      mLastMouseY = mouseY;
    }

    float dx = static_cast<float>(mouseX - mLastMouseX);
    float dy = static_cast<float>(mouseY - mLastMouseY);

    mYaw += dx * mOrbitSpeed;
    mPitch += dy * mOrbitSpeed;
    mPitch = std::clamp(mPitch, mMinPitch, mMaxPitch);

    mLastMouseX = mouseX;
    mLastMouseY = mouseY;
  } else {
    mDragging = false;
  }

  // --- Scroll to zoom ---
  // NOTE: Scroll is handled via callback in main.cpp, which calls
  // a global variable. We check it here.
  // For simplicity, we use +/- keys as an alternative
  if (glfwGetKey(window, GLFW_KEY_EQUAL) == GLFW_PRESS ||
      glfwGetKey(window, GLFW_KEY_KP_ADD) == GLFW_PRESS) {
    mDistance -= mZoomSpeed * dt * 10.0f;
  }
  if (glfwGetKey(window, GLFW_KEY_MINUS) == GLFW_PRESS ||
      glfwGetKey(window, GLFW_KEY_KP_SUBTRACT) == GLFW_PRESS) {
    mDistance += mZoomSpeed * dt * 10.0f;
  }
  mDistance = std::clamp(mDistance, mMinDistance, mMaxDistance);

  // --- Arrow keys to pan target ---
  // Calculate forward/right relative to camera yaw (projected on XZ plane)
  float yawRad = glm::radians(mYaw);
  glm::vec3 forward(-cos(yawRad), 0.0f, -sin(yawRad));
  glm::vec3 right =
      glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));

  float speed = mPanSpeed * dt;

  if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS)
    mTarget += forward * speed;
  if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS)
    mTarget -= forward * speed;
  if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS)
    mTarget -= right * speed;
  if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS)
    mTarget += right * speed;
  if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS)
    mTarget.y -= speed;
  if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS)
    mTarget.y += speed;
}
