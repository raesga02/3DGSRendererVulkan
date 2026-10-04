#pragma once

#include "scene/GaussianData.hpp"

#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace io {

// Scene-level information gathered while loading.
struct SceneMetadata {
	uint32_t count = 0;
	int shDegree = 0;

	glm::vec3 aabbMin{ 0.0f };
	glm::vec3 aabbMax{ 0.0f };

	// Outlier-robust framing: component-wise median and the 90th percentile
	// distance from it. Raw AABBs are dominated by far-away stray Gaussians.
	glm::vec3 center{ 0.0f };
	float radius = 1.0f;

	// Optional training camera (from cameras.json) for a good initial view.
	bool hasCamera = false;
	glm::vec3 cameraPosition{ 0.0f };
	glm::vec3 cameraForward{ 0.0f, 0.0f, 1.0f };
	glm::vec3 cameraUp{ 0.0f, 1.0f, 0.0f };
};

// Loads an INRIA-format 3DGS .ply (binary_little_endian, float properties).
// Property names are mapped by name, so column order does not matter.
class PlyLoader {
  public:
	static std::vector<scene::GaussianData> load(const std::string &path, SceneMetadata &outMeta);

	// Reads only the ASCII header to get the vertex count (cheap; no body read).
	// Returns 0 on failure.
	static uint32_t readVertexCount(const std::string &path);
};

}        // namespace io
