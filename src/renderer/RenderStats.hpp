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
};

}        // namespace renderer
