#pragma once
#ifndef GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#endif
#include <glm/glm.hpp>

class Camera
{
public:
  Camera();
  void SetLookAt(glm::vec3 eyePos, glm::vec3 target, glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f));

  void OnMouseMove(int dx, int dy);
  void OnMouseButtonDown(int buttonType);
  void OnMouseButtonUp();

  glm::mat4 GetViewMatrix()const { return m_view; }
  glm::vec3 GetPosition() const;

private:
  void UpdateMatrix();
  bool m_isDragged;
  int m_buttonType;
  glm::mat4 m_view;
};