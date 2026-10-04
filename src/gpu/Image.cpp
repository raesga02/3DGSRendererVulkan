#include "Image.hpp"
#include "MemoryTracker.hpp"
#include <stdexcept>

#include "core/Device.hpp"
#include "utils/VulkanUtils.hpp"

namespace gpu {

namespace {

uint64_t formatBytes(VkFormat format) {
	switch (format) {
		case VK_FORMAT_R32G32B32A32_SFLOAT:
			return 16;
		case VK_FORMAT_R16G16B16A16_SFLOAT:
		case VK_FORMAT_R32G32_SFLOAT:
			return 8;
		case VK_FORMAT_R32_SFLOAT:
		case VK_FORMAT_R8G8B8A8_UNORM:
		case VK_FORMAT_R8G8B8A8_SRGB:
		case VK_FORMAT_B8G8R8A8_UNORM:
		case VK_FORMAT_B8G8R8A8_SRGB:
			return 4;
		case VK_FORMAT_R16_SFLOAT:
			return 2;
		default:
			return 4;
	}
}

}        // namespace

void Image::init(core::Device &device, VmaAllocator allocator, VkExtent2D extent, VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspectFlags, VkImageLayout initialLayout) {
	m_device = &device;
	m_format = format;
	m_extent = extent;

	createImage(allocator, usage);
	createImageView(aspectFlags);

	memory::addImage(static_cast<uint64_t>(extent.width) * extent.height * formatBytes(format));
	memory::addAllocation();

	m_currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	if (initialLayout != VK_IMAGE_LAYOUT_UNDEFINED) {
		VkCommandBuffer cmd = m_device->beginSingleTimeCommands();
		transition(cmd, initialLayout);
		m_device->endSingleTimeCommands(cmd);
	}
}

void Image::shutdown(VmaAllocator allocator) {
	memory::subImage(static_cast<uint64_t>(m_extent.width) * m_extent.height * formatBytes(m_format));
	memory::subAllocation();

	vkDestroyImageView(m_device->get(), m_imageView, nullptr);
	vmaDestroyImage(allocator, m_image, m_allocation);
}

void Image::createImage(VmaAllocator allocator, VkImageUsageFlags usage) {
	VkImageCreateInfo imageInfo{
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = m_format,
		.extent = { m_extent.width, m_extent.height, 1 },
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = usage,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	};

	VmaAllocationCreateInfo allocInfo{};
	allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

	if (vmaCreateImage(allocator, &imageInfo, &allocInfo, &m_image, &m_allocation, nullptr) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create GPU image");
	}
}

void Image::createImageView(VkImageAspectFlags aspectFlags) {
	VkImageViewCreateInfo viewInfo{
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = m_image,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.format = m_format,
		.subresourceRange = {
		    .aspectMask = aspectFlags,
		    .baseMipLevel = 0,
		    .levelCount = 1,
		    .baseArrayLayer = 0,
		    .layerCount = 1 }
	};

	if (vkCreateImageView(m_device->get(), &viewInfo, nullptr, &m_imageView) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create image view");
	}
}

void Image::transition(VkCommandBuffer cmd, VkImageLayout newLayout) {
	if (newLayout == m_currentLayout)
		return;

	utils::vk::LayoutInfo src = utils::vk::getLayoutInfo(m_currentLayout);
	utils::vk::LayoutInfo dst = utils::vk::getLayoutInfo(newLayout);

	utils::vk::transitionImage(cmd, m_image, m_currentLayout, newLayout, src.access, dst.access, src.stage, dst.stage);

	m_currentLayout = newLayout;
}

void Image::barrier(VkCommandBuffer cmd, VkAccessFlags srcAccess, VkAccessFlags dstAccess, VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage) {
	VkImageMemoryBarrier barrier = utils::vk::buildImageBarrier(m_image, srcAccess, dstAccess, m_currentLayout, m_currentLayout);
	vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void Image::clear(VkCommandBuffer cmd, VkClearColorValue clearColor) {
	transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

	VkImageSubresourceRange range{
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.baseMipLevel = 0,
		.levelCount = 1,
		.baseArrayLayer = 0,
		.layerCount = 1
	};

	vkCmdClearColorImage(cmd, m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor, 1, &range);
}

}        // namespace gpu
