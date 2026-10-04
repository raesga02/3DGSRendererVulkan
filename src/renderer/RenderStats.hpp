#pragma once

#include <cstdint>

namespace renderer {

// Lightweight per-frame statistics surfaced in the debug GUI.
struct RenderStats {
	float cpuFrameMs = 0.0f;
	float fps = 0.0f;
	uint32_t drawCalls = 0;

	// Estimated GPU memory (bytes), all from requested sizes.
	uint64_t gpuTotalBytes = 0;
	uint64_t gpuSceneBytes = 0;
	uint64_t gpuSortBytes = 0;
	uint64_t gpuFrameBytes = 0;
	uint64_t gpuSwapchainBytes = 0;
	uint64_t gpuAllocations = 0;

	// GPU pass timings (ms), smoothed.
	float gpuBackgroundMs = 0.0f;
	float gpuKeygenMs = 0.0f;
	float gpuRadixMs = 0.0f;
	float gpuRasterMs = 0.0f;
	float gpuGuiMs = 0.0f;
	float gpuTotalMs = 0.0f;
	bool gpuTimingsValid = false;
};

}        // namespace renderer
