#pragma once

#include <glm/glm.hpp>

namespace scene {

// Simple pinhole camera with an orbit-friendly API. Phase 1 will drive this
// from the camera controller; Phase 4 will drive it from XR head poses.
class Camera {
  public:
	void setPerspective(float fovYDegrees, float aspect, float zNear, float zFar);
	void setView(const glm::vec3 &eye, const glm::vec3 &center, const glm::vec3 &up);

	void setViewMatrix(const glm::mat4 &view) {
		m_view = view;
	}
	void setProjectionMatrix(const glm::mat4 &projection) {
		m_projection = projection;
	}
	void setPosition(const glm::vec3 &position) {
		m_position = position;
	}

	const glm::mat4 &getView() const {
		return m_view;
	}
	const glm::mat4 &getProjection() const {
		return m_projection;
	}

	glm::vec3 getPosition() const {
		return m_position;
	}

  private:
	glm::mat4 m_view{ 1.0f };
	glm::mat4 m_projection{ 1.0f };
	glm::vec3 m_position{ 0.0f };
};

}        // namespace scene
