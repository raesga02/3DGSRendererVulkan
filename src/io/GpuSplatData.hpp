#pragma once

#include "scene/GaussianData.hpp"

#include <cmath>
#include <cstdint>

namespace io {

// Tightly-packed GPU representation of a Gaussian (Phase 1, DC colour).
// Layout is consumed as-is by shaders/splat.vert, so keep it std430-friendly:
// only scalar arrays, no vec3 members (which would add padding).
struct GpuGaussian {
	float position[3];
	float cov3D[6];        // upper triangle: xx, xy, xz, yy, yz, zz
	float color[3];        // RGB, already resolved from the DC term
	float opacity;         // already sigmoid-activated
};

static_assert(sizeof(GpuGaussian) == 52, "GpuGaussian must stay tightly packed for std430");

// Spherical-harmonics constant for the DC term.
constexpr float kShC0 = 0.28209479177387814f;

inline float sigmoid(float x) {
	return 1.0f / (1.0f + std::exp(-x));
}

}        // namespace io
