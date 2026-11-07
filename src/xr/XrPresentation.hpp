#pragma once

#include "XrContext.hpp"
#include "XrSwapchain.hpp"
#include "app/IPresentation.hpp"

#include <array>
#include <cstdint>
#include <vulkan/vulkan.h>

namespace core {
class Device;
}

namespace xr {

// OpenXR presentation: paces frames with xrWaitFrame, acquires one swapchain
// image per eye, and submits a stereo projection layer. Per-eye cameras come
// from xrLocateViews and are relative to the runtime's LOCAL space.
class XrPresentation : public app::IPresentation {
  public:
	void init(core::Device &device, XrContext &context);
	void shutdown();

	// Per-eye render scale relative to the runtime's recommended resolution.
	// Must be set before init(); clamped to [0.25, 2.0].
	void setResolutionScale(float scale) {
		m_resolutionScale = scale < 0.25f ? 0.25f : (scale > 2.0f ? 2.0f : scale);
	}

	void beginFrame(app::PresentFrame &out) override;
	void endFrame(const app::PresentFrame &frame) override;

	VkExtent2D extent() const override {
		return m_extent;
	}
	VkFormat colorFormat() const override {
		return m_format;
	}
	uint32_t imageCount() const override {
		return m_eyes[0].imageCount();
	}
	bool providesViewCameras() const override {
		return true;
	}
	bool lastFrameRendered() const {
		return m_lastFrameRendered;
	}

  private:
	static constexpr uint32_t kMaxFrames = app::kMaxFramesInFlight;

	struct FrameData {
		VkCommandPool pool = VK_NULL_HANDLE;
		VkCommandBuffer cmd = VK_NULL_HANDLE;
		VkFence fence = VK_NULL_HANDLE;
	};

	void createFrameData(core::Device &device);
	void destroyFrameData(core::Device &device);
	VkFormat chooseFormat(XrSession session) const;

  private:
	core::Device *m_device = nullptr;
	XrContext *m_context = nullptr;

	std::array<XrSwapchain, XrContext::kViewCount> m_eyes;
	VkExtent2D m_extent{};
	VkFormat m_format = VK_FORMAT_UNDEFINED;
	float m_resolutionScale = 0.5f;

	std::array<FrameData, kMaxFrames> m_frames{};
	uint32_t m_frameCounter = 0;
	bool m_loggedFirstFrame = false;
	bool m_loggedFirstWait = false;
	bool m_loggedFirstWaitReturn = false;
	bool m_loggedFirstLayer = false;
	bool m_lastShouldRender = false;
	bool m_lastFrameRendered = false;

	XrFrameState m_frameState{XR_TYPE_FRAME_STATE};
	std::array<XrView, XrContext::kViewCount> m_views{};
	bool m_frameBegun = false;
};

}        // namespace xr
