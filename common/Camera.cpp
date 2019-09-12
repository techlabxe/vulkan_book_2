#include "Camera.h"
#include <glm/gtx/transform.hpp>
#include <glm/gtc/matrix_access.hpp>

using namespace glm;

Camera::Camera() : m_isDragged(false)
{
}

void Camera::SetLookAt(glm::vec3 eyePos, glm::vec3 target, glm::vec3 up)
{
  m_view = glm::lookAt(eyePos, target, up);
}

void Camera::OnMouseButtonDown(int buttonType)
{
  m_isDragged = true;
  m_buttonType = buttonType;
}
void Camera::OnMouseButtonUp()
{
  m_isDragged = false;
}

void Camera::OnMouseMove(int dx, int dy)
{
  if (!m_isDragged)
  {
    return;
  }

  if (m_buttonType == 0)
  {
    auto axisX = glm::vec3(glm::row(m_view, 0));
    auto ry = glm::rotate(float(dx) * 0.01f, glm::vec3(0.0f, 1.0f, 0.0f));
    auto rx = glm::rotate(float(dy) * 0.01f, axisX);
    m_view = m_view * ry * rx;
  }
  if (m_buttonType == 1)
  {
    auto invMat = glm::inverse(m_view);
    vec3 forward = glm::vec3(glm::column(invMat, 2));
    vec3 pos = glm::vec3(glm::column(invMat, 3));
    pos += forward * float(dy * 0.1f);
    invMat[3] = vec4(pos, 1.0f);
    m_view = glm::inverse(invMat);
  }
  if (m_buttonType == 2)
  {
    auto m = glm::translate(glm::vec3(dx*0.05f, dy * -0.05f, 0.0f));
    m_view = m_view * m;
  }
}

glm::vec3 Camera::GetPosition() const
{
  auto axis0 = glm::vec3(glm::column(m_view, 0));
  auto axis1 = glm::vec3(glm::column(m_view, 1));
  auto axis2 = glm::vec3(glm::column(m_view, 2));
  auto axis3 = glm::vec3(glm::column(m_view, 3));

  auto x = -glm::dot(axis0, axis3);
  auto y = -glm::dot(axis1, axis3);
  auto z = -glm::dot(axis2, axis3);
  return glm::vec3(x, y, z);
}
