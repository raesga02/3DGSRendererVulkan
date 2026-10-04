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

// Shared, read-only-ish services + target description handed to every pass.
struct RenderContext {
	core::Device *device = nullptr;
	gpu::Allocator *allocator = nullptr;

	VkExtent2D extent{};
	VkFormat format = VK_FORMAT_UNDEFINED;
	uint32_t imageCount = 0;

	SceneState *sceneState = nullptr;
};

}        // namespace renderer
