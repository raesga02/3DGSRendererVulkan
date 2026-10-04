#include "Buffer.hpp"
#include "MemoryTracker.hpp"
#include <stdexcept>

#include "core/Device.hpp"

namespace gpu {

void Buffer::init(core::Device &device, VmaAllocator allocator, VkDeviceSize size, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage, VmaAllocationCreateFlags flags) {
	m_device = &device;
	m_size = size;

	createBuffer(allocator, size, usage, memoryUsage, flags);

	memory::addBuffer(size);
	memory::addAllocation();
}

void Buffer::shutdown(VmaAllocator allocator) {
	if (m_buffer == VK_NULL_HANDLE)
		return;

	memory::subBuffer(m_size);
	memory::subAllocation();

	vmaDestroyBuffer(allocator, m_buffer, m_allocation);
	m_buffer = VK_NULL_HANDLE;
}

void Buffer::createBuffer(VmaAllocator allocator, VkDeviceSize size, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage, VmaAllocationCreateFlags flags) {
	VkBufferCreateInfo bufferInfo{
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = size,
		.usage = usage,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE
	};

	VmaAllocationCreateInfo allocInfo{
		.flags = flags,
		.usage = memoryUsage,
	};

	if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &m_buffer, &m_allocation, &m_allocationInfo) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create buffer");
	}
}

void Buffer::invalidate(VmaAllocator allocator, VkDeviceSize offset, VkDeviceSize size) {
	vmaInvalidateAllocation(allocator, m_allocation, offset, size);
}

}        // namespace gpu
