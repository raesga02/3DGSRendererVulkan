#pragma once

#include "IPresentation.hpp"
#include "core/Swapchain.hpp"

#include <vector>
#include <vulkan/vulkan.h>

namespace core {
class Device;
}

namespace app {

// Desktop presentation: a vkb swapchain plus per-frame command buffers and
// synchronisation. Present mode is FIFO (vsync); resize support is intentionally
// deferred to a later phase.
class DesktopPresentation : public IPresentation {
  public:
	DesktopPresentation() = default;
	~DesktopPresentation() override = default;

	void init(core::Device &device, uint32_t width, uint32_t height);
	void shutdown();

	void beginFrame(PresentFrame &out) override;
	void endFrame(const PresentFrame &frame) override;

	VkExtent2D extent() const override {
		return m_swapchain.getSwapchainInfo().extent;
	}
	VkFormat colorFormat() const override {
		return m_swapchain.getSwapchainInfo().format;
	}
	uint32_t imageCount() const override {
		return m_swapchain.getSwapchainInfo().imageCount;
	}

	core::Swapchain &getSwapchain() {
		return m_swapchain;
	}

  private:
	static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = kMaxFramesInFlight;

	struct FrameData {
		VkCommandPool commandPool = VK_NULL_HANDLE;
		VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
		VkSemaphore imageAvailable = VK_NULL_HANDLE;
		VkFence inFlightFence = VK_NULL_HANDLE;
	};

	void createCommandBuffers(core::Device &device);
	void createSyncObjects(core::Device &device);

  private:
	core::Device *m_device = nullptr;
	core::Swapchain m_swapchain;

	std::vector<FrameData> m_frames;
	std::vector<VkSemaphore> m_renderFinishedSemaphores;

	uint32_t m_currentFrame = 0;
};

}        // namespace app
