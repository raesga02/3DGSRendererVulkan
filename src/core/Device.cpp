#include "Device.hpp"
#include "core/VulkanContext.hpp"

#include <stdexcept>

namespace core {

void Device::init(VulkanContext &context) {
	// Vulkan 1.3 features used across the renderer (dynamic rendering + sync2).
	VkPhysicalDeviceVulkan13Features features13{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
		.synchronization2 = VK_TRUE,
		.dynamicRendering = VK_TRUE,
	};

	// Select physical device.
	vkb::PhysicalDeviceSelector selector(context.getVkbInstance());
	auto phys_ret = selector
	                    .set_minimum_version(1, 3)
	                    .set_required_features_13(features13)
	                    .set_surface(context.getSurface())
	                    .select();
	if (!phys_ret)
		throw std::runtime_error("Failed to select physical device");

	vkb::PhysicalDevice physicalDevice = phys_ret.value();
	m_physicalDevice = physicalDevice.physical_device;

	// Build logical device.
	vkb::DeviceBuilder builder(physicalDevice);
	auto dev_ret = builder.build();
	if (!dev_ret)
		throw std::runtime_error("Failed to create logical device");

	m_vkbDevice = dev_ret.value();
	m_device = m_vkbDevice.device;
	m_ownsDevice = true;

	// Graphics queue.
	auto graphics_queue_ret = m_vkbDevice.get_queue(vkb::QueueType::graphics);
	if (!graphics_queue_ret)
		throw std::runtime_error("Failed to get graphics queue");
	m_graphicsQueue = graphics_queue_ret.value();

	auto graphics_queue_index_ret = m_vkbDevice.get_queue_index(vkb::QueueType::graphics);
	if (!graphics_queue_index_ret)
		throw std::runtime_error("Failed to get graphics queue index");
	m_graphicsQueueIndex = graphics_queue_index_ret.value();

	// Present queue.
	auto present_queue_ret = m_vkbDevice.get_queue(vkb::QueueType::present);
	if (!present_queue_ret)
		throw std::runtime_error("Failed to get present queue");
	m_presentQueue = present_queue_ret.value();

	createTransientPool();
}

void Device::initFromHandles(VkPhysicalDevice physicalDevice, VkDevice device, VkQueue graphicsQueue, uint32_t graphicsQueueIndex) {
	m_physicalDevice = physicalDevice;
	m_device = device;
	m_graphicsQueue = graphicsQueue;
	m_graphicsQueueIndex = graphicsQueueIndex;
	m_presentQueue = graphicsQueue;
	m_ownsDevice = false;

	createTransientPool();
}

void Device::createTransientPool() {
	// Transient command pool for one-time command buffers.
	VkCommandPoolCreateInfo poolInfo{
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
		.queueFamilyIndex = m_graphicsQueueIndex,
	};

	if (vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_transientCommandPool) != VK_SUCCESS)
		throw std::runtime_error("Failed to create transient command pool");
}

void Device::shutdown() {
	if (m_device)
		vkDeviceWaitIdle(m_device);

	if (m_transientCommandPool)
		vkDestroyCommandPool(m_device, m_transientCommandPool, nullptr);
	m_transientCommandPool = VK_NULL_HANDLE;

	if (m_ownsDevice)
		vkb::destroy_device(m_vkbDevice);

	m_device = VK_NULL_HANDLE;
	m_physicalDevice = VK_NULL_HANDLE;
	m_ownsDevice = false;
}

VkCommandBuffer Device::beginSingleTimeCommands() {
	VkCommandBufferAllocateInfo allocInfo{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = m_transientCommandPool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = 1,
	};

	VkCommandBuffer commandBuffer;
	if (vkAllocateCommandBuffers(m_device, &allocInfo, &commandBuffer) != VK_SUCCESS)
		throw std::runtime_error("Failed to allocate single time command buffer");

	VkCommandBufferBeginInfo beginInfo{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};

	vkBeginCommandBuffer(commandBuffer, &beginInfo);

	return commandBuffer;
}

void Device::endSingleTimeCommands(VkCommandBuffer commandBuffer) {
	vkEndCommandBuffer(commandBuffer);

	VkSubmitInfo submitInfo{
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.commandBufferCount = 1,
		.pCommandBuffers = &commandBuffer,
	};

	vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
	vkQueueWaitIdle(m_graphicsQueue);

	vkFreeCommandBuffers(m_device, m_transientCommandPool, 1, &commandBuffer);
}

}        // namespace core
