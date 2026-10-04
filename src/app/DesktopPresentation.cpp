#include "DesktopPresentation.hpp"

#include "core/Device.hpp"
#include "core/Swapchain.hpp"

#include <stdexcept>

namespace app {

void DesktopPresentation::init(core::Device &device, uint32_t width, uint32_t height) {
	m_device = &device;
	m_swapchain.init(device, width, height);
	createCommandBuffers(device);
	createSyncObjects(device);
}

void DesktopPresentation::shutdown() {
	VkDevice device = m_device->get();
	vkDeviceWaitIdle(device);

	for (auto &frame : m_frames) {
		vkDestroyFence(device, frame.inFlightFence, nullptr);
		vkDestroySemaphore(device, frame.imageAvailable, nullptr);
		vkDestroyCommandPool(device, frame.commandPool, nullptr);
	}

	for (VkSemaphore semaphore : m_renderFinishedSemaphores) {
		vkDestroySemaphore(device, semaphore, nullptr);
	}

	m_swapchain.shutdown(*m_device);
}

void DesktopPresentation::createCommandBuffers(core::Device &device) {
	VkDevice vkDevice = device.get();
	uint32_t queueFamilyIndex = device.getGraphicsQueueIndex();
	m_frames.resize(MAX_FRAMES_IN_FLIGHT);

	for (auto &frame : m_frames) {
		VkCommandPoolCreateInfo poolInfo{
			.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
			.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
			.queueFamilyIndex = queueFamilyIndex,
		};

		if (vkCreateCommandPool(vkDevice, &poolInfo, nullptr, &frame.commandPool) != VK_SUCCESS)
			throw std::runtime_error("Failed to create command pool");

		VkCommandBufferAllocateInfo allocInfo{
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
			.commandPool = frame.commandPool,
			.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
			.commandBufferCount = 1,
		};

		if (vkAllocateCommandBuffers(vkDevice, &allocInfo, &frame.commandBuffer) != VK_SUCCESS)
			throw std::runtime_error("Failed to allocate command buffer");
	}
}

void DesktopPresentation::createSyncObjects(core::Device &device) {
	VkDevice vkDevice = device.get();

	m_renderFinishedSemaphores.resize(m_swapchain.getImageViews().size());

	VkSemaphoreCreateInfo semaphoreInfo{
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
	};
	VkFenceCreateInfo fenceInfo{
		.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
		.flags = VK_FENCE_CREATE_SIGNALED_BIT,
	};

	for (auto &frame : m_frames) {
		if (vkCreateSemaphore(vkDevice, &semaphoreInfo, nullptr, &frame.imageAvailable) != VK_SUCCESS)
			throw std::runtime_error("Failed to create imageAvailable semaphore");
		if (vkCreateFence(vkDevice, &fenceInfo, nullptr, &frame.inFlightFence) != VK_SUCCESS)
			throw std::runtime_error("Failed to create in-flight fence");
	}

	for (VkSemaphore &semaphore : m_renderFinishedSemaphores) {
		if (vkCreateSemaphore(vkDevice, &semaphoreInfo, nullptr, &semaphore) != VK_SUCCESS)
			throw std::runtime_error("Failed to create renderFinished semaphore");
	}
}

void DesktopPresentation::beginFrame(PresentFrame &out) {
	VkDevice vkDevice = m_device->get();
	FrameData &frame = m_frames[m_currentFrame];

	vkWaitForFences(vkDevice, 1, &frame.inFlightFence, VK_TRUE, UINT64_MAX);

	uint32_t imageIndex = 0;
	VkResult result = vkAcquireNextImageKHR(
	    vkDevice, m_swapchain.get(), UINT64_MAX, frame.imageAvailable, VK_NULL_HANDLE, &imageIndex);
	if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
		throw std::runtime_error("Failed to acquire swapchain image");
	}

	vkResetFences(vkDevice, 1, &frame.inFlightFence);
	vkResetCommandBuffer(frame.commandBuffer, 0);

	VkCommandBufferBeginInfo beginInfo{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};
	vkBeginCommandBuffer(frame.commandBuffer, &beginInfo);

	out.frameIndex = m_currentFrame;
	out.cmd = frame.commandBuffer;
	out.target.image = m_swapchain.getImages()[imageIndex];
	out.target.view = m_swapchain.getImageViews()[imageIndex];
	out.target.index = imageIndex;
	out.target.extent = m_swapchain.getSwapchainInfo().extent;
	out.target.format = m_swapchain.getSwapchainInfo().format;
}

void DesktopPresentation::endFrame(const PresentFrame &frame) {
	VkDevice vkDevice = m_device->get();
	FrameData &frameData = m_frames[m_currentFrame];

	vkEndCommandBuffer(frame.cmd);

	VkSemaphore waitSemaphores[] = { frameData.imageAvailable };
	VkSemaphore signalSemaphores[] = { m_renderFinishedSemaphores[frame.target.index] };
	VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };

	VkSubmitInfo submitInfo{
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = waitSemaphores,
		.pWaitDstStageMask = waitStages,
		.commandBufferCount = 1,
		.pCommandBuffers = &frame.cmd,
		.signalSemaphoreCount = 1,
		.pSignalSemaphores = signalSemaphores,
	};

	if (vkQueueSubmit(m_device->getGraphicsQueue(), 1, &submitInfo, frameData.inFlightFence) != VK_SUCCESS)
		throw std::runtime_error("Failed to submit command buffer");

	VkSwapchainKHR swapchains[] = { m_swapchain.get() };
	VkPresentInfoKHR presentInfo{
		.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = signalSemaphores,
		.swapchainCount = 1,
		.pSwapchains = swapchains,
		.pImageIndices = &frame.target.index,
	};

	VkResult result = vkQueuePresentKHR(m_device->getPresentQueue(), &presentInfo);
	if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
		throw std::runtime_error("Failed to present swapchain image");
	}

	m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

}        // namespace app
