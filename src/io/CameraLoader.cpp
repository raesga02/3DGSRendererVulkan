#include "CameraLoader.hpp"

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <vector>

namespace io {

namespace {

// Parses up to `count` floats starting at the first '[' after `keyPos`.
bool parseFloatsAfter(const std::string &text, size_t keyPos, int count, std::vector<float> &out) {
	size_t p = text.find('[', keyPos);
	if (p == std::string::npos)
		return false;

	const char *begin = text.c_str();
	const char *cur = begin + p + 1;
	const char *end = begin + text.size();

	out.clear();
	while (static_cast<int>(out.size()) < count && cur < end) {
		char *next = nullptr;
		float value = std::strtof(cur, &next);
		if (next == cur) {
			if (*cur == ']' && out.empty())
				return false;
			++cur;        // skip separators / nested brackets
			continue;
		}
		out.push_back(value);
		cur = next;
	}
	return static_cast<int>(out.size()) == count;
}

}        // namespace

bool loadFirstCamera(const std::string &path, CameraPose &outPose) {
	std::ifstream file(path, std::ios::binary);
	if (!file.is_open())
		return false;

	std::stringstream ss;
	ss << file.rdbuf();
	const std::string text = ss.str();

	size_t posKey = text.find("\"position\"");
	size_t rotKey = text.find("\"rotation\"");
	if (posKey == std::string::npos || rotKey == std::string::npos)
		return false;

	std::vector<float> position;
	std::vector<float> rotation;
	if (!parseFloatsAfter(text, posKey, 3, position))
		return false;
	if (!parseFloatsAfter(text, rotKey, 9, rotation))
		return false;

	outPose.position = glm::vec3(position[0], position[1], position[2]);
	// 3DGS stores the camera rotation (camera-to-world, columns = camera axes)
	// with COLMAP's y-down convention, so the world "up" is -column(1).
	outPose.forward = glm::normalize(glm::vec3(rotation[2], rotation[5], rotation[8]));
	outPose.up = -glm::normalize(glm::vec3(rotation[1], rotation[4], rotation[7]));

	// Average the up vector over every camera: a single camera can be rolled,
	// but the mean points along the reconstruction's actual vertical axis.
	glm::vec3 upSum(0.0f);
	uint32_t upCount = 0;
	size_t search = 0;
	while ((search = text.find("\"rotation\"", search)) != std::string::npos) {
		std::vector<float> r;
		if (parseFloatsAfter(text, search, 9, r)) {
			glm::vec3 up = -glm::normalize(glm::vec3(r[1], r[4], r[7]));
			if (glm::dot(up, up) > 1e-6f) {
				upSum += up;
				++upCount;
			}
		}
		search += 10;
	}

	if (upCount > 0 && glm::dot(upSum, upSum) > 1e-6f)
		outPose.worldUp = glm::normalize(upSum);
	else
		outPose.worldUp = outPose.up;

	return true;
}

}        // namespace io
