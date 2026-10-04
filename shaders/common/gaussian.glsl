#version 450

// Shared constants for the 3DGS pipeline, mirrored from io/GpuSplatData.hpp.
// Higher-order SH (degree 3) is added in Phase 2.

#define GAUSSIAN_SH_DEGREE 3
#define GAUSSIAN_SH_COEFFS ((GAUSSIAN_SH_DEGREE + 1) * (GAUSSIAN_SH_DEGREE + 1))
#define GAUSSIAN_SH_C0 0.28209479177387814
