#pragma once
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

namespace core {
class Device;
}

namespace gpu {

// VMA-backed 2D image plus its default image view. Tracks its own
// current VkImageLayout so callers can express intent in terms of layouts.
class Image {
  public:
	void init(core::Device &device, VmaAllocator allocator, VkExtent2D extent, VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspectFlags, VkImageLayout initialLayout = VK_IMAGE_LAYOUT_UNDEFINED);
	void shutdown(VmaAllocator allocator);
	void transition(VkCommandBuffer cmd, VkImageLayout newLayout);
	void barrier(VkCommandBuffer cmd, VkAccessFlags srcAccess, VkAccessFlags dstAccess, VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage);
	void clear(VkCommandBuffer cmd, VkClearColorValue clearColor);

	VkImage get() const {
		return m_image;
	}
	VkImageView getView() const {
		return m_imageView;
	}
	VkFormat getFormat() const {
		return m_format;
	}
	VkExtent2D getExtent() const {
		return m_extent;
	}
	VkImageLayout getLayout() const {
		return m_currentLayout;
	}

  private:
	void createImage(VmaAllocator allocator, VkImageUsageFlags usage);
	void createImageView(VkImageAspectFlags aspectFlags);

  private:
	core::Device *m_device = nullptr;
	VkImage m_image = VK_NULL_HANDLE;
	VkImageView m_imageView = VK_NULL_HANDLE;
	VmaAllocation m_allocation = VK_NULL_HANDLE;

	VkFormat m_format = VK_FORMAT_UNDEFINED;
	VkImageLayout m_currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	VkExtent2D m_extent{};
};

}        // namespace gpu
