#include "Camera.hpp"

#include <glm/gtc/matrix_transform.hpp>

namespace scene {

void Camera::setPerspective(float fovYDegrees, float aspect, float zNear, float zFar) {
	m_projection = glm::perspective(glm::radians(fovYDegrees), aspect, zNear, zFar);
}

void Camera::setView(const glm::vec3 &eye, const glm::vec3 &center, const glm::vec3 &up) {
	m_view = glm::lookAt(eye, center, up);
	m_position = eye;
}

}        // namespace scene
