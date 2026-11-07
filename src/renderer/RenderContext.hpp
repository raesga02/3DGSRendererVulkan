#pragma once

#include <vulkan/vulkan.h>

namespace core {
class Device;
}

namespace gpu {
class Allocator;
}

namespace renderer {

struct SceneState;
class GpuProfiler;
class Foveation;

// Shared, read-only-ish services + target description handed to every pass.
struct RenderContext {
	core::Device *device = nullptr;
	gpu::Allocator *allocator = nullptr;

	VkExtent2D extent{};
	VkFormat format = VK_FORMAT_UNDEFINED;
	uint32_t imageCount = 0;

	SceneState *sceneState = nullptr;
	GpuProfiler *profiler = nullptr;

	// Optional VK_KHR_fragment_shading_rate path. Only the splat pipeline uses
	// it; a fragmentSize of {1,1} disables the pipeline VRS state.
	bool fragmentShadingRate = false;
	VkExtent2D fragmentShadingRateExtent{ 1, 1 };

	// Fixed foveation: rate maps are created lazily once the eye projections
	// are known, then reused for the session.
	bool foveation = false;
	Foveation *foveationImages = nullptr;
	bool foveationDebug = false;
};

}        // namespace renderer
