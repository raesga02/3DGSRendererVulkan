#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace renderer {

// UI-adjustable renderer state. Grows every phase (SH degree, sort toggle,
// debug views, ...).
struct SceneState {
	bool showGUI = true;

	// Background gradient (linear RGB).
	glm::vec3 backgroundColorTop{ 0.05f, 0.06f, 0.09f };
	glm::vec3 backgroundColorBottom{ 0.01f, 0.01f, 0.02f };

	// Splat settings.
	bool sortEnabled = true;
	float splatScale = 1.0f;
	bool cullLargeSplats = true;          // heuristic floater filter
	float maxSplatFraction = 0.2f;        // max screen-space radius, fraction of min dimension

	// Loaded scene info.
	std::string currentSceneName;
	uint32_t gaussianCount = 0;
	int shDegree = 0;         // currently displayed SH order
	int maxShDegree = 0;      // maximum available in the loaded scene

	// Scene browser.
	std::vector<std::string> availableScenes;        // display names
	std::vector<std::string> scenePaths;             // matching full paths
	std::vector<uint64_t> sceneEstimatedBytes;       // per-scene GPU estimate
	int selectedScene = -1;
	std::string requestedScenePath;                  // set to trigger a load

	// Camera mode mirror (for display): 0 = orbit, 1 = fly.
	int cameraMode = 0;
	bool cameraToggleRequested = false;

	// Set when the renderer has loaded a scene and derived new derived state.
	bool sceneDirty = false;

	// Screenshot request (F12).
	bool screenshotRequested = false;
	int screenshotCounter = 0;
};

}        // namespace renderer
