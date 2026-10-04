#pragma once

#include <glm/glm.hpp>

namespace app {

// Per-frame input snapshot. Desktop fills this from GLFW today; Phase 4 will
// fill the same struct from OpenXR actions/poses.
struct InputState {
	glm::vec2 mousePosition{ 0.0f, 0.0f };        // normalized screen space
	bool mouseDown = false;

	bool keyH = false;        // toggle GUI
	bool keyEscape = false;   // close window
};

}        // namespace app
