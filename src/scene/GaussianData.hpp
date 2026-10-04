#pragma once

#include <cstdint>

namespace scene {

// CPU-side representation of a single 3D Gaussian as produced by the
// INRIA 3DGS trainer. Scales/opacity are stored in their raw (pre-activation)
// form: scale is log-space and opacity is stored pre-sigmoid. Colour is kept
// as spherical-harmonics coefficients; degree 0 (DC) is always present.
struct GaussianData {
	// Layout matches the packed GPU upload in Phase 1.
	float position[3];

	float normal[3];        // Upstream PLY carries normals; often zero. Kept for round-tripping.

	// Base (DC) colour term, one per channel.
	float f_dc[3];

	// Higher-order SH coefficients, flattened as [coeff][channel].
	// Degree 3 => 15 higher-order coefficients * 3 channels = 45 floats.
	// A scene may be loaded with fewer coefficients; `shDegree` records how
	// many orders are actually valid.
	float f_rest[45];

	float opacity;        // pre-sigmoid

	float scale[3];       // log-space

	float rotation[4];    // normalized quaternion, (w, x, y, z)
};

}        // namespace scene
