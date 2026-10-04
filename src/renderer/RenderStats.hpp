#pragma once

#include <cstdint>

namespace renderer {

// Lightweight per-frame statistics surfaced in the debug GUI.
struct RenderStats {
	float cpuFrameMs = 0.0f;
	float fps = 0.0f;
	uint32_t drawCalls = 0;
};

}        // namespace renderer
