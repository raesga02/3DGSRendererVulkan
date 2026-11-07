#pragma once

#include <VkBootstrap.h>
#include <vulkan/vulkan.h>

namespace core {

class VulkanContext;

// Selects a physical device, creates the logical device and exposes
// the graphics/present queues plus a transient command pool for
// one-shot uploads.
class Device {
  public:
	void init(VulkanContext &context);
	// Adopt an already-created device (used by the OpenXR path, where the
	// runtime picks the physical device and creates the VkDevice).
	void initFromHandles(VkPhysicalDevice physicalDevice, VkDevice device, VkQueue graphicsQueue, uint32_t graphicsQueueIndex);
	void shutdown();

	VkCommandBuffer beginSingleTimeCommands();
	void endSingleTimeCommands(VkCommandBuffer commandBuffer);

	VkDevice get() const {
		return m_device;
	}

	vkb::Device getVkbDevice() const {
		return m_vkbDevice;
	}

	VkPhysicalDevice getPhysicalDevice() const {
		return m_physicalDevice;
	}
	VkQueue getGraphicsQueue() const {
		return m_graphicsQueue;
	}
	uint32_t getGraphicsQueueIndex() const {
		return m_graphicsQueueIndex;
	}
	VkQueue getPresentQueue() const {
		return m_presentQueue;
	}

	// VK_KHR_fragment_shading_rate is optional; enabled only if both the
	// extension and pipelineFragmentShadingRate are available.
	bool fragmentShadingRateSupported() const {
		return m_fragmentShadingRate;
	}
	void setFragmentShadingRateSupported(bool supported) {
		m_fragmentShadingRate = supported;
	}

  private:
	void createTransientPool();

  private:
	VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
	VkDevice m_device = VK_NULL_HANDLE;
	bool m_ownsDevice = false;
	bool m_fragmentShadingRate = false;

	VkCommandPool m_transientCommandPool = VK_NULL_HANDLE;

	VkQueue m_graphicsQueue = VK_NULL_HANDLE;
	uint32_t m_graphicsQueueIndex = 0;
	VkQueue m_presentQueue = VK_NULL_HANDLE;

	vkb::Device m_vkbDevice{};
};

}        // namespace core
