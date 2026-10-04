#pragma once

#include <glm/glm.hpp>

namespace renderer {

// UI-adjustable renderer state. Grows every phase (SH degree, splat scale,
// sort toggle, debug views, loaded scene, ...).
struct SceneState {
	bool showGUI = true;

	// Phase 0 background gradient (linear RGB).
	glm::vec3 backgroundColorTop{ 0.05f, 0.06f, 0.09f };
	glm::vec3 backgroundColorBottom{ 0.01f, 0.01f, 0.02f };

	// Phase 1+ placeholders.
	bool sortEnabled = true;
	int shDegree = 0;
};

}        // namespace renderer
