#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace renderer {

enum class RenderMode : int {
	Instanced = 0,
	Tiled = 1,
};

// UI-adjustable renderer state. Grows every phase (SH degree, sort toggle,
// debug views, ...).
struct SceneState {
	bool showGUI = true;

	// Rasterization path.
	RenderMode renderMode = RenderMode::Instanced;

	// Background gradient (linear RGB).
	glm::vec3 backgroundColorTop{ 0.05f, 0.06f, 0.09f };
	glm::vec3 backgroundColorBottom{ 0.01f, 0.01f, 0.02f };

	// Splat settings.
	bool sortEnabled = true;
	float splatScale = 1.0f;
	bool cullLargeSplats = true;          // heuristic floater filter
	float maxSplatFraction = 0.2f;        // max screen-space radius, fraction of min dimension
	float minOpacity = 0.0f;              // discard fainter Gaussians (floater haze)

	// 3D floater filters (0 disables each).
	float maxWorldScaleFraction = 0.0f;   // cull if 3D stddev > fraction * sceneRadius
	float maxDistanceFactor = 0.0f;       // cull if distance from center > factor * sceneRadius

	// Scene bounds used by the 3D filters.
	glm::vec3 sceneCenter{ 0.0f };
	float sceneRadius = 0.0f;

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

	// Set by the renderer each frame: true when the view/scene/settings changed
	// and the depth/tile sort must be rebuilt. When false, the previous sort is
	// reused (idle-skip).
	bool sortDirty = true;
	bool forceSort = false;        // disable idle-skip (profiling)

	// Screenshot request (F12).
	bool screenshotRequested = false;
	int screenshotCounter = 0;
};

}        // namespace renderer
