#pragma once

#include "scene/GaussianData.hpp"

#include <cmath>
#include <cstdint>

namespace io {

// Tightly-packed GPU representation of a Gaussian. Layout is consumed as-is by
// shaders/splat.vert, so keep it std430-friendly: scalar arrays only, no vec3
// members (which would add padding).
struct GpuGaussian {
	float position[3];        // world position
	float cov3D[6];           // upper triangle: xx, xy, xz, yy, yz, zz
	float opacity;            // already sigmoid-activated
	float sh[48];             // spherical harmonics, [coeff][channel]; coeff 0 = DC
};

static_assert(sizeof(GpuGaussian) == 232, "GpuGaussian must stay tightly packed for std430");

// Spherical-harmonics constants (matching the INRIA 3DGS reference).
constexpr float kShC0 = 0.28209479177387814f;

inline float sigmoid(float x) {
	return 1.0f / (1.0f + std::exp(-x));
}

}        // namespace io
