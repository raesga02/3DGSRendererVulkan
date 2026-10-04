#version 450

// Shared constants for the 3DGS pipeline. Included by compute and graphics
// shaders via #include-style duplication until glslang adds native include
// support (or until the build preprocesses includes).
const int GAUSSIAN_SH_DEGREE = 3;
const int GAUSSIAN_SH_COEFFS = (GAUSSIAN_SH_DEGREE + 1) * (GAUSSIAN_SH_DEGREE + 1);
