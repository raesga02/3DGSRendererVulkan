#include "Swapchain.hpp"
#include "core/Device.hpp"

#include <VkBootstrap.h>
#include <stdexcept>

namespace core {

void Swapchain::init(Device &device, uint32_t width, uint32_t height) {
	createSwapchain(device, width, height);
}

void Swapchain::shutdown(Device &device) {
	for (VkImageView view : m_imageViews) {
		vkDestroyImageView(device.get(), view, nullptr);
	}

	vkb::destroy_swapchain(m_vkbSwapchain);
}

void Swapchain::createSwapchain(Device &device, uint32_t width, uint32_t height) {
	vkb::SwapchainBuilder swapchainBuilder(device.getVkbDevice());

	auto swap_ret = swapchainBuilder
	                    .set_desired_extent(width, height)
	                    .set_desired_format(VkSurfaceFormatKHR{
	                        .format = VK_FORMAT_B8G8R8A8_SRGB,
	                        .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR })
	                    .set_image_usage_flags(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
	                    .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
	                    .build();
	if (!swap_ret)
		throw std::runtime_error("Failed to create swapchain");

	m_vkbSwapchain = swap_ret.value();

	auto images_ret = m_vkbSwapchain.get_images();
	if (!images_ret)
		throw std::runtime_error("Failed to get swapchain images");
	m_images = images_ret.value();

	auto image_views_ret = m_vkbSwapchain.get_image_views();
	if (!image_views_ret)
		throw std::runtime_error("Failed to get swapchain image views");
	m_imageViews = image_views_ret.value();
}

}        // namespace core
