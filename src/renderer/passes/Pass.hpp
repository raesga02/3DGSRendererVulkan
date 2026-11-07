#pragma once

#include "core/Swapchain.hpp"
#include <vulkan/vulkan.h>

namespace core {
class Device;
}

namespace gpu {
class Allocator;
}

namespace renderer {

struct RenderContext;
struct SceneState;

// Everything a pass needs to record into the current frame.
struct PassContext {
	VkCommandBuffer cmd = VK_NULL_HANDLE;
	uint32_t frameIndex = 0;
	uint32_t imageIndex = 0;

	// Which view of the frame this pass is recording. Passes that keep per-view
	// resources (descriptor sets, cameras) index them with this.
	uint32_t viewIndex = 0;

	VkImage targetImage = VK_NULL_HANDLE;
	VkImageView targetView = VK_NULL_HANDLE;
	VkExtent2D extent{};

	// Sub-rectangle of the target this view renders into. Passes that draw
	// 3D content must use it for both the viewport and the scissor. GUI overlays
	// use the full extent instead.
	VkRect2D renderArea{};
	bool clearTarget = true;
};

// Base class for all render passes. Passes are deliberately unaware of the
// window/swapchain so the same passes run for desktop and OpenXR.
class Pass {
  public:
	virtual ~Pass() = default;

	virtual void init(RenderContext &renderCtx) = 0;
	virtual void shutdown(RenderContext &renderCtx) = 0;

	virtual void record(RenderContext &renderCtx, const PassContext &passCtx) = 0;
};

}        // namespace renderer
