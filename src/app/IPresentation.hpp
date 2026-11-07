#pragma once

#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

namespace app {

// Number of frames the CPU may run ahead of the GPU. Shared so per-frame
// resources (UBOs, index buffers, descriptor sets) line up everywhere.
inline constexpr uint32_t kMaxFramesInFlight = 2;

// Maximum number of views a single frame can render. Desktop uses one; OpenXR
// uses one per eye. Kept small so per-view resources can live in arrays.
inline constexpr uint32_t kMaxViews = 2;

// Optional per-view camera supplied by the presentation layer (OpenXR). The
// matrices are relative to the renderer's base camera frame: the presentation
// reports the eye pose in its own reference space, and the renderer composes it
// with the scene camera so locomotion stays in the renderer.
struct PresentViewCamera {
	bool valid = false;
	glm::mat4 localView{ 1.0f };        // reference space -> eye (inverse eye pose)
	glm::mat4 projection{ 1.0f };       // eye projection (asymmetric for XR)
	glm::vec3 localPosition{ 0.0f };    // eye position in reference space
};

// A colour target the renderer can draw into for one frame. For desktop this
// is a swapchain image; for OpenXR it is a per-eye image.
//
// `renderArea` is the sub-rectangle of the target this view fills. It is the
// whole image for XR (one image per eye) and half the window for the desktop
// stereo test. `clear` marks the first view that touches an image so a shared
// image is only cleared once.
struct PresentTarget {
	VkImage image = VK_NULL_HANDLE;
	VkImageView view = VK_NULL_HANDLE;
	uint32_t index = 0;
	VkExtent2D extent{};
	VkFormat format = VK_FORMAT_UNDEFINED;
	VkRect2D renderArea{};
	bool clear = true;
	// Layout the renderer leaves the image in. Desktop presents the swapchain;
	// OpenXR hands the image back to the runtime in colour-attachment layout.
	VkImageLayout finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
};

// Per-in-flight-frame recording state handed to the renderer.
struct PresentFrame {
	uint32_t frameIndex = 0;
	VkCommandBuffer cmd = VK_NULL_HANDLE;
	uint32_t viewCount = 0;
	std::array<PresentTarget, kMaxViews> targets{};
	std::array<PresentViewCamera, kMaxViews> viewCameras{};
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

	// True when beginFrame supplies per-view cameras (OpenXR). The renderer then
	// uses those instead of deriving views from its own camera.
	virtual bool providesViewCameras() const {
		return false;
	}
};

}        // namespace app
