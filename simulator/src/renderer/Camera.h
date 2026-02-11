#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

struct GLFWwindow;

class Camera {
public:
  void Init(float distance = 5.0f, float yaw = -90.0f, float pitch = 25.0f);

  // Call each frame with delta time
  void ProcessInput(GLFWwindow *window, float dt);

  glm::mat4 GetViewMatrix() const;
  glm::mat4 GetProjectionMatrix(float aspectRatio) const;
  glm::mat4 GetViewProjection(float aspectRatio) const;

private:
  // Orbital parameters
  glm::vec3 mTarget = glm::vec3(0.0f);
  float mDistance = 5.0f;
  float mYaw = -90.0f;  // degrees
  float mPitch = 25.0f; // degrees
  float mMinPitch = -89.0f;
  float mMaxPitch = 89.0f;
  float mMinDistance = 1.0f;
  float mMaxDistance = 50.0f;

  // Input state
  float mOrbitSpeed = 0.3f;
  float mPanSpeed = 5.0f;
  float mZoomSpeed = 2.0f;
  double mLastMouseX = 0.0;
  double mLastMouseY = 0.0;
  bool mDragging = false;

  // Camera settings
  float mFov = 60.0f;
  float mNearPlane = 0.1f;
  float mFarPlane = 200.0f;

  glm::vec3 GetEyePosition() const;
};
