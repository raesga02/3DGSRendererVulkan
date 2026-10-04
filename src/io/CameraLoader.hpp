#pragma once

#include <glm/glm.hpp>
#include <string>

namespace io {

struct CameraPose {
	glm::vec3 position{ 0.0f };
	glm::vec3 forward{ 0.0f, 0.0f, 1.0f };
	glm::vec3 up{ 0.0f, 1.0f, 0.0f };
};

// Reads the first camera from an INRIA 3DGS `cameras.json`. Returns false if
// the file is missing or could not be parsed. The rotation matrix in the file
// is camera-to-world, so the view direction is its third column.
bool loadFirstCamera(const std::string &path, CameraPose &outPose);

}        // namespace io
