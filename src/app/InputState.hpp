#pragma once

#include <glm/glm.hpp>

namespace app {

// Per-frame input snapshot. Desktop fills this from GLFW today; Phase 4 will
// fill the same struct from OpenXR actions/poses.
struct InputState {
	glm::vec2 mousePosition{ 0.0f, 0.0f };        // normalized screen space
	glm::vec2 mouseDelta{ 0.0f, 0.0f };           // pixels since last frame
	float scrollDelta = 0.0f;

	bool mouseDown = false;         // left button
	bool rightMouseDown = false;    // right button

	bool keyW = false;
	bool keyA = false;
	bool keyS = false;
	bool keyD = false;
	bool keyQ = false;
	bool keyE = false;
	bool keyShift = false;

	bool toggleCamera = false;        // edge-triggered: switch orbit/fly
	bool keyH = false;                // edge-triggered: toggle GUI
	bool keyScreenshot = false;       // edge-triggered: save a PNG
	bool keyEscape = false;
};

}        // namespace app
