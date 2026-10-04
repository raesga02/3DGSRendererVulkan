#pragma once
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

namespace gpu {

// Owns the VMA allocator used by all GPU buffer/image wrappers.
class Allocator {
  public:
	void init(VkPhysicalDevice physicalDevice, VkDevice device, VkInstance instance);
	void shutdown();

	VmaAllocator get() const {
		return m_allocator;
	}

  private:
	VmaAllocator m_allocator = VK_NULL_HANDLE;
};

}        // namespace gpu
