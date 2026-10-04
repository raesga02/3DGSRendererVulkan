#pragma once

#include <VkBootstrap.h>
#include <vector>
#include <vulkan/vulkan.h>

namespace core {

class Device;

struct SwapchainInfo {
	VkFormat format;
	VkExtent2D extent;
	uint32_t imageCount;
};

// Thin wrapper around a vkb::Swapchain that exposes the raw handles and
// image views used by the presentation layer.
class Swapchain {
  public:
	void init(Device &device, uint32_t width, uint32_t height);
	void shutdown(Device &device);

	VkSwapchainKHR get() const {
		return m_vkbSwapchain.swapchain;
	}

	SwapchainInfo getSwapchainInfo() const {
		return SwapchainInfo{
			.format = m_vkbSwapchain.image_format,
			.extent = m_vkbSwapchain.extent,
			.imageCount = static_cast<uint32_t>(m_images.size()),
		};
	}

	const std::vector<VkImageView> &getImageViews() const {
		return m_imageViews;
	}

	const std::vector<VkImage> &getImages() const {
		return m_images;
	}

  private:
	void createSwapchain(Device &device, uint32_t width, uint32_t height);

  private:
	vkb::Swapchain m_vkbSwapchain{};
	std::vector<VkImage> m_images;
	std::vector<VkImageView> m_imageViews;
};

}        // namespace core
