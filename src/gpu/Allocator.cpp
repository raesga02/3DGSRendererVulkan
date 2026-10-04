#define VMA_IMPLEMENTATION
#include "Allocator.hpp"
#include <stdexcept>
#include <vk_mem_alloc.h>

namespace gpu {

void Allocator::init(VkPhysicalDevice physicalDevice, VkDevice device, VkInstance instance) {
	VmaAllocatorCreateInfo allocatorInfo{
		.physicalDevice = physicalDevice,
		.device = device,
		.instance = instance,
	};

	if (vmaCreateAllocator(&allocatorInfo, &m_allocator) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create VMA allocator");
	}
}

void Allocator::shutdown() {
	if (m_allocator) {
		vmaDestroyAllocator(m_allocator);
		m_allocator = VK_NULL_HANDLE;
	}
}

}        // namespace gpu
