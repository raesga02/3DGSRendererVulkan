#pragma once

#include <vulkan/vulkan.h>

namespace utils::vk {

struct LayoutInfo {
	VkAccessFlags access;
	VkPipelineStageFlags stage;
};

inline LayoutInfo getLayoutInfo(VkImageLayout layout) {
	switch (layout) {
		case VK_IMAGE_LAYOUT_UNDEFINED:
			return { 0, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT };

		case VK_IMAGE_LAYOUT_GENERAL:
			return { VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
				     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT };

		case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
			return { VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT };

		case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
			return { VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT };

		case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
			return { VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT };

		case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
			return { VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };

		case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
			return { 0, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT };

		default:
			return { 0, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT };
	}
}

inline VkImageMemoryBarrier buildImageBarrier(VkImage image, VkAccessFlags srcAccess, VkAccessFlags dstAccess, VkImageLayout oldLayout, VkImageLayout newLayout, VkImageSubresourceRange subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }) {
	return VkImageMemoryBarrier{
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.srcAccessMask = srcAccess,
		.dstAccessMask = dstAccess,
		.oldLayout = oldLayout,
		.newLayout = newLayout,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = image,
		.subresourceRange = subresourceRange
	};
}

inline void transitionImage(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout, VkAccessFlags srcAccessMask, VkAccessFlags dstAccessMask, VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage) {
	VkImageMemoryBarrier barrier = buildImageBarrier(image, srcAccessMask, dstAccessMask, oldLayout, newLayout);
	vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

}        // namespace utils::vk
