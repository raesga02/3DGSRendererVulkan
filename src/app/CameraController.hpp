#pragma once

#include <glm/glm.hpp>

namespace app {

struct InputState;
}

namespace scene {
class Camera;
}

namespace app {

// Orbit and fly cameras over a bounded scene. The fly mode is toggled with a
// key; input deltas are supplied by the platform layer (GLFW today, XR later).
//
// The orbit/fly basis is built around the scene's up vector (m_up), so scenes
// whose "up" is not world +Y (e.g. COLMAP y-down reconstructions) orbit around
// the correct vertical axis.
class CameraController {
  public:
	enum class Mode {
		Orbit,
		Fly,
	};

	void frame(const glm::vec3 &center, float radius);
	void frameFromCamera(const glm::vec3 &position, const glm::vec3 &forward, const glm::vec3 &up, float radius);
	void update(const InputState &input, float dt);
	void apply(scene::Camera &camera, float aspect) const;

	void toggleMode();
	Mode getMode() const {
		return m_mode;
	}

  private:
	void rebuildBasis();
	glm::vec3 direction(float yaw, float pitch) const;

  private:
	Mode m_mode = Mode::Orbit;

	glm::vec3 m_target{ 0.0f };
	float m_radius = 1.0f;
	float m_distance = 3.0f;
	float m_yaw = 0.0f;
	float m_pitch = 0.3f;

	glm::vec3 m_up{ 0.0f, 1.0f, 0.0f };
	glm::vec3 m_forward0{ 0.0f, 0.0f, 1.0f };
	glm::vec3 m_right0{ 1.0f, 0.0f, 0.0f };

	glm::vec3 m_position{ 0.0f };
	float m_flyYaw = 0.0f;
	float m_flyPitch = 0.3f;

	float m_fovDegrees = 45.0f;
};

}        // namespace app
