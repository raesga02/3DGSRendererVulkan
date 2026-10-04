#pragma once

#include <vulkan/vulkan.h>

namespace app {

// A colour target the renderer can draw into for one frame. For desktop this
// is a swapchain image; for OpenXR (Phase 4) it will be a per-eye image.
struct PresentTarget {
	VkImage image = VK_NULL_HANDLE;
	VkImageView view = VK_NULL_HANDLE;
	uint32_t index = 0;
	VkExtent2D extent{};
	VkFormat format = VK_FORMAT_UNDEFINED;
};

// Per-in-flight-frame recording state handed to the renderer.
struct PresentFrame {
	uint32_t frameIndex = 0;
	VkCommandBuffer cmd = VK_NULL_HANDLE;
	PresentTarget target{};
};

// Presentation owns "where pixels go". The renderer only ever talks to this
// interface, which is what keeps the OpenXR/MR path additive later on.
class IPresentation {
  public:
	virtual ~IPresentation() = default;

	// Waits for the frame slot, acquires a target and begins its command buffer.
	virtual void beginFrame(PresentFrame &out) = 0;
	// Ends the command buffer, submits it and presents the target.
	virtual void endFrame(const PresentFrame &frame) = 0;

	virtual VkExtent2D extent() const = 0;
	virtual VkFormat colorFormat() const = 0;
	virtual uint32_t imageCount() const = 0;
};

}        // namespace app
