#include "XrSwapchain.hpp"

#include "utils/Log.hpp"

#include <fmt/format.h>
#include <stdexcept>

namespace xr {

namespace {

void check(XrResult result, const char *what) {
	if (XR_SUCCEEDED(result))
		return;
	throw std::runtime_error(fmt::format("{} failed ({})", what, static_cast<int>(result)));
}

}        // namespace

void XrSwapchain::init(XrSession session, VkDevice device, VkExtent2D extent, VkFormat format) {
	m_device = device;
	m_extent = extent;
	m_format = format;

	XrSwapchainCreateInfo createInfo{XR_TYPE_SWAPCHAIN_CREATE_INFO};
	createInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
	createInfo.format = format;
	createInfo.sampleCount = 1;
	createInfo.width = extent.width;
	createInfo.height = extent.height;
	createInfo.faceCount = 1;
	createInfo.arraySize = 1;
	createInfo.mipCount = 1;

	check(xrCreateSwapchain(session, &createInfo, &m_swapchain), "xrCreateSwapchain");

	uint32_t imageCount = 0;
	check(xrEnumerateSwapchainImages(m_swapchain, 0, &imageCount, nullptr), "xrEnumerateSwapchainImages");

	std::vector<XrSwapchainImageVulkan2KHR> images(imageCount);
	for (auto &image : images)
		image.type = XR_TYPE_SWAPCHAIN_IMAGE_VULKAN2_KHR;
	check(xrEnumerateSwapchainImages(m_swapchain, imageCount, &imageCount,
	                                 reinterpret_cast<XrSwapchainImageBaseHeader *>(images.data())),
	      "xrEnumerateSwapchainImages");

	m_images.resize(imageCount);
	m_views.resize(imageCount);

	for (uint32_t i = 0; i < imageCount; ++i) {
		m_images[i] = images[i].image;

		VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
		viewInfo.image = m_images[i];
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format = m_format;
		viewInfo.subresourceRange = {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 1,
		};
		if (vkCreateImageView(m_device, &viewInfo, nullptr, &m_views[i]) != VK_SUCCESS)
			throw std::runtime_error("Failed to create XR swapchain image view");
	}

	utils::logInfo("OpenXR swapchain: {}x{}, format {}, {} images", m_extent.width, m_extent.height,
	               static_cast<int>(m_format), imageCount);
}

void XrSwapchain::acquire() {
	XrSwapchainImageAcquireInfo acquireInfo{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
	check(xrAcquireSwapchainImage(m_swapchain, &acquireInfo, &m_index), "xrAcquireSwapchainImage");

	XrSwapchainImageWaitInfo waitInfo{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
	waitInfo.timeout = XR_INFINITE_DURATION;
	check(xrWaitSwapchainImage(m_swapchain, &waitInfo), "xrWaitSwapchainImage");

	m_acquired = true;
}

void XrSwapchain::release() {
	if (!m_acquired)
		return;

	XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
	check(xrReleaseSwapchainImage(m_swapchain, &releaseInfo), "xrReleaseSwapchainImage");
	m_acquired = false;
}

void XrSwapchain::shutdown() {
	if (m_device) {
		for (VkImageView view : m_views) {
			if (view)
				vkDestroyImageView(m_device, view, nullptr);
		}
	}
	m_views.clear();
	m_images.clear();

	if (m_swapchain)
		xrDestroySwapchain(m_swapchain);
	m_swapchain = XR_NULL_HANDLE;
	m_acquired = false;
}

}        // namespace xr
