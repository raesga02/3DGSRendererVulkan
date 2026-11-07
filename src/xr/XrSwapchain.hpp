#pragma once

#include <cstdint>
#include <vector>

#include <vulkan/vulkan.h>
#define XR_USE_GRAPHICS_API_VULKAN
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

namespace xr {

// One OpenXR colour swapchain plus the VkImageViews the renderer needs.
class XrSwapchain {
  public:
	void init(XrSession session, VkDevice device, VkExtent2D extent, VkFormat format);
	void shutdown();

	// Acquires and waits for the next image. Throws on failure.
	void acquire();
	void release();

	::XrSwapchain handle() const {
		return m_swapchain;
	}
	VkImage image() const {
		return m_images[m_index];
	}
	VkImageView imageView() const {
		return m_views[m_index];
	}
	uint32_t index() const {
		return m_index;
	}
	uint32_t imageCount() const {
		return static_cast<uint32_t>(m_images.size());
	}
	VkExtent2D extent() const {
		return m_extent;
	}
	VkFormat format() const {
		return m_format;
	}
	bool acquired() const {
		return m_acquired;
	}

  private:
	::XrSwapchain m_swapchain = XR_NULL_HANDLE;
	VkDevice m_device = VK_NULL_HANDLE;
	VkExtent2D m_extent{};
	VkFormat m_format = VK_FORMAT_UNDEFINED;
	std::vector<VkImage> m_images;
	std::vector<VkImageView> m_views;
	uint32_t m_index = 0;
	bool m_acquired = false;
};

}        // namespace xr
