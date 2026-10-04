#pragma once
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

namespace core {
class Device;
}

namespace gpu {

// VMA-backed GPU buffer wrapper.
class Buffer {
  public:
	void init(core::Device &device, VmaAllocator allocator, VkDeviceSize size, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage, VmaAllocationCreateFlags flags = 0);
	void shutdown(VmaAllocator allocator);
	void invalidate(VmaAllocator allocator, VkDeviceSize offset = 0, VkDeviceSize size = VK_WHOLE_SIZE);

	VkBuffer get() const {
		return m_buffer;
	}
	VkDeviceSize getSize() const {
		return m_size;
	}
	void *getMappedData() const {
		return m_allocationInfo.pMappedData;
	}

  private:
	void createBuffer(VmaAllocator allocator, VkDeviceSize size, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage, VmaAllocationCreateFlags flags);

  private:
	core::Device *m_device = nullptr;
	VkBuffer m_buffer = VK_NULL_HANDLE;
	VmaAllocation m_allocation = VK_NULL_HANDLE;
	VmaAllocationInfo m_allocationInfo{};
	VkDeviceSize m_size = 0;
};

}        // namespace gpu
