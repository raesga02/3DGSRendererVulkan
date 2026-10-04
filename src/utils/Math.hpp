#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace utils::math {

// Rotation matrix R from a (normalized) unit quaternion, matching the
// 3DGS convention where the quaternion is stored as (w, x, y, z).
inline glm::mat3 quatToRotation(const glm::vec4 &q) {
	const float w = q.x;
	const float x = q.y;
	const float y = q.z;
	const float z = q.w;

	return glm::mat3(
	    1.0f - 2.0f * (y * y + z * z), 2.0f * (x * y + w * z), 2.0f * (x * z - w * y),
	    2.0f * (x * y - w * z), 1.0f - 2.0f * (x * x + z * z), 2.0f * (y * z + w * x),
	    2.0f * (x * z + w * y), 2.0f * (y * z - w * x), 1.0f - 2.0f * (x * x + y * y));
}

// 3D covariance from per-axis scale and rotation: Sigma = R S S^T R^T.
inline glm::mat3 computeCov3D(const glm::vec3 &scale, const glm::vec4 &rotation) {
	glm::mat3 R = quatToRotation(rotation);
	glm::mat3 S = glm::mat3(scale.x, 0.0f, 0.0f,
	                        0.0f, scale.y, 0.0f,
	                        0.0f, 0.0f, scale.z);
	glm::mat3 M = R * S;
	return M * glm::transpose(M);
}

// Perspective projection with reverse-Z disabled (standard OpenGL-style depth).
inline glm::mat4 perspective(float fovY, float aspect, float zNear, float zFar) {
	return glm::perspective(fovY, aspect, zNear, zFar);
}

inline glm::mat4 lookAt(const glm::vec3 &eye, const glm::vec3 &center, const glm::vec3 &up) {
	return glm::lookAt(eye, center, up);
}

}        // namespace utils::math
