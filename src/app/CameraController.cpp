#include "CameraController.hpp"

#include "InputState.hpp"
#include "scene/Camera.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace app {

void CameraController::rebuildBasis() {
	glm::vec3 up = glm::normalize(m_up);

	// Pick a reference axis that is not parallel to up, then orthonormalize.
	glm::vec3 ref = (std::abs(glm::dot(up, glm::vec3(0.0f, 0.0f, 1.0f))) > 0.99f)
	                    ? glm::vec3(1.0f, 0.0f, 0.0f)
	                    : glm::vec3(0.0f, 0.0f, 1.0f);

	m_forward0 = glm::normalize(ref - glm::dot(ref, up) * up);
	m_right0 = glm::normalize(glm::cross(up, m_forward0));
}

glm::vec3 CameraController::direction(float yaw, float pitch) const {
	return std::cos(pitch) * (std::cos(yaw) * m_forward0 + std::sin(yaw) * m_right0) +
	       std::sin(pitch) * m_up;
}

void CameraController::frame(const glm::vec3 &center, float radius) {
	m_target = center;
	m_radius = std::max(radius, 1e-3f);
	m_distance = m_radius * 2.2f;

	m_yaw = 0.0f;
	m_pitch = 0.3f;
	m_up = glm::vec3(0.0f, 1.0f, 0.0f);
	rebuildBasis();

	m_position = m_target + m_distance * direction(m_yaw, m_pitch);
	m_flyYaw = m_yaw;
	m_flyPitch = m_pitch;
}

void CameraController::frameFromCamera(const glm::vec3 &position, const glm::vec3 &forward, const glm::vec3 &up, float radius) {
	m_radius = std::max(radius, 1e-3f);
	glm::vec3 f = glm::normalize(forward);
	m_up = glm::normalize(up);
	rebuildBasis();

	m_distance = m_radius;
	m_target = position + f * m_distance;

	// direction(yaw, pitch) == normalize(eye - target) == -f
	glm::vec3 dir = -f;
	float upDot = std::clamp(glm::dot(dir, m_up), -1.0f, 1.0f);
	m_pitch = std::asin(upDot);

	glm::vec3 horizontal = dir - upDot * m_up;
	if (glm::length(horizontal) > 1e-5f)
		m_yaw = std::atan2(glm::dot(horizontal, m_right0), glm::dot(horizontal, m_forward0));
	else
		m_yaw = 0.0f;

	m_position = position;
	m_flyYaw = m_yaw;
	m_flyPitch = m_pitch;
}

void CameraController::toggleMode() {
	if (m_mode == Mode::Orbit) {
		// Carry the current orbit pose into the fly camera.
		m_position = m_target + m_distance * direction(m_yaw, m_pitch);
		m_flyYaw = m_yaw;
		m_flyPitch = m_pitch;
		m_mode = Mode::Fly;
	} else {
		m_target = m_position - m_distance * direction(m_flyYaw, m_flyPitch);
		m_mode = Mode::Orbit;
	}
}

void CameraController::update(const InputState &input, float dt) {
	constexpr float kLookSensitivity = 0.0025f;

	if (m_mode == Mode::Orbit) {
		if (input.mouseDown) {
			m_yaw -= input.mouseDelta.x * kLookSensitivity;
			m_pitch += input.mouseDelta.y * kLookSensitivity;
			m_pitch = std::clamp(m_pitch, -1.5f, 1.5f);
		}
		if (input.scrollDelta != 0.0f) {
			m_distance *= std::exp(-input.scrollDelta * 0.1f);
			m_distance = std::clamp(m_distance, m_radius * 0.05f, m_radius * 50.0f);
		}
	} else {
		if (input.rightMouseDown) {
			m_flyYaw -= input.mouseDelta.x * kLookSensitivity;
			m_flyPitch += input.mouseDelta.y * kLookSensitivity;
			m_flyPitch = std::clamp(m_flyPitch, -1.5f, 1.5f);
		}

		glm::vec3 forward = -direction(m_flyYaw, m_flyPitch);
		glm::vec3 right = glm::normalize(glm::cross(forward, m_up));

		float speed = m_radius * 0.5f * (input.keyShift ? 3.0f : 1.0f);
		glm::vec3 move(0.0f);
		if (input.keyW)
			move += forward;
		if (input.keyS)
			move -= forward;
		if (input.keyD)
			move += right;
		if (input.keyA)
			move -= right;
		if (input.keyE)
			move += m_up;
		if (input.keyQ)
			move -= m_up;

		if (glm::length(move) > 0.0f)
			m_position += glm::normalize(move) * speed * dt;
	}
}

void CameraController::apply(scene::Camera &camera, float aspect) const {
	float zNear = std::max(0.001f, m_radius * 0.01f);
	float zFar = m_radius * 200.0f + m_distance;

	glm::mat4 projection = glm::perspective(glm::radians(m_fovDegrees), aspect, zNear, zFar);
	// Flip Y for Vulkan's framebuffer orientation.
	projection[1][1] *= -1.0f;

	glm::vec3 eye;
	if (m_mode == Mode::Orbit) {
		eye = m_target + m_distance * direction(m_yaw, m_pitch);
		camera.setViewMatrix(glm::lookAt(eye, m_target, m_up));
	} else {
		eye = m_position;
		glm::vec3 forward = -direction(m_flyYaw, m_flyPitch);
		camera.setViewMatrix(glm::lookAt(eye, eye + forward, m_up));
	}

	camera.setProjectionMatrix(projection);
	camera.setPosition(eye);
}

}        // namespace app
