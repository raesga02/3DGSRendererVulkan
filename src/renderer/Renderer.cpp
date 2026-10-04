#include "Renderer.hpp"

#include "app/IPresentation.hpp"
#include "core/Device.hpp"
#include "core/VulkanContext.hpp"
#include "gpu/Allocator.hpp"
#include "utils/VulkanUtils.hpp"

#include <stdexcept>

namespace renderer {

void Renderer::init(core::Device &device, gpu::Allocator &allocator, core::VulkanContext &context, app::IPresentation &presentation, GLFWwindow *window) {
	m_device = &device;
	m_allocator = &allocator;
	m_presentation = &presentation;

	m_renderContext = RenderContext{
		.device = &device,
		.allocator = &allocator,
		.extent = presentation.extent(),
		.format = presentation.colorFormat(),
		.imageCount = presentation.imageCount(),
		.sceneState = &m_sceneState,
	};

	m_backgroundPass.init(m_renderContext);
	m_guiPass.init(m_renderContext);

	core::SwapchainInfo swapchainInfo{
		.format = presentation.colorFormat(),
		.extent = presentation.extent(),
		.imageCount = presentation.imageCount(),
	};
	m_guiPass.initVulkan(window, context, device, allocator, swapchainInfo, m_sceneState);

	m_lastFrameTime = std::chrono::steady_clock::now();
}

void Renderer::shutdown() {
	vkDeviceWaitIdle(m_device->get());

	m_guiPass.shutdown(m_renderContext);
	m_backgroundPass.shutdown(m_renderContext);
}

void Renderer::drawFrame() {
	if (m_sceneState.showGUI)
		m_guiPass.newFrame(m_stats);

	app::PresentFrame frame;
	m_presentation->beginFrame(frame);

	recordFrame(frame);

	m_presentation->endFrame(frame);

	updateStats();
}

void Renderer::recordFrame(const app::PresentFrame &frame) {
	PassContext passCtx{
		.cmd = frame.cmd,
		.frameIndex = frame.frameIndex,
		.imageIndex = frame.target.index,
		.targetImage = frame.target.image,
		.targetView = frame.target.view,
		.extent = frame.target.extent,
	};

	m_backgroundPass.record(m_renderContext, passCtx);

	if (m_sceneState.showGUI)
		m_guiPass.record(m_renderContext, passCtx);

	// Hand the image to the presentation engine.
	utils::vk::transitionImage(
	    frame.cmd, frame.target.image,
	    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
	    VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0,
	    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
}

void Renderer::updateStats() {
	auto now = std::chrono::steady_clock::now();
	float ms = std::chrono::duration<float, std::milli>(now - m_lastFrameTime).count();
	m_lastFrameTime = now;

	m_stats.cpuFrameMs = ms;
	m_stats.fps = ms > 0.0f ? 1000.0f / ms : 0.0f;
}

bool Renderer::wantsCaptureMouse() const {
	return m_guiPass.wantsCaptureMouse();
}

bool Renderer::wantsCaptureKeyboard() const {
	return m_guiPass.wantsCaptureKeyboard();
}

}        // namespace renderer
