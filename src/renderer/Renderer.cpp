#include "Renderer.hpp"

#include "app/IPresentation.hpp"
#include "app/InputState.hpp"
#include "core/Device.hpp"
#include "core/VulkanContext.hpp"
#include "gpu/Allocator.hpp"
#include "utils/Log.hpp"
#include "utils/VulkanUtils.hpp"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fmt/format.h>
#include <stdexcept>

namespace renderer {

void Renderer::init(core::Device &device, gpu::Allocator &allocator, core::VulkanContext &context, app::IPresentation &presentation, GLFWwindow *window, const std::string &initialScene) {
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
	m_rasterizePass.init(m_renderContext);
	m_sortPass.init(m_renderContext);
	m_guiPass.init(m_renderContext);

	core::SwapchainInfo swapchainInfo{
		.format = presentation.colorFormat(),
		.extent = presentation.extent(),
		.imageCount = presentation.imageCount(),
	};
	m_guiPass.initVulkan(window, context, device, allocator, swapchainInfo, m_sceneState);

	createFrameBuffers();
	scanScenes();

	// Load the requested scene, or a sensible default if one is available.
	if (!initialScene.empty() && std::filesystem::exists(initialScene)) {
		loadScene(initialScene);
	} else if (!m_sceneState.scenePaths.empty()) {
		size_t defaultIndex = 0;
		for (size_t i = 0; i < m_sceneState.availableScenes.size(); ++i) {
			if (m_sceneState.availableScenes[i] == "room") {
				defaultIndex = i;
				break;
			}
		}
		loadScene(m_sceneState.scenePaths[defaultIndex]);
	}

	m_lastFrameTime = std::chrono::steady_clock::now();
}

void Renderer::shutdown() {
	vkDeviceWaitIdle(m_device->get());

	for (auto &buffer : m_cameraBuffers)
		buffer.shutdown(m_allocator->get());

	if (m_readbackValid) {
		m_readbackBuffer.shutdown(m_allocator->get());
		m_readbackValid = false;
	}

	if (m_scene.isLoaded())
		m_scene.shutdown(*m_allocator);

	m_sortPass.shutdown(m_renderContext);
	m_guiPass.shutdown(m_renderContext);
	m_rasterizePass.shutdown(m_renderContext);
	m_backgroundPass.shutdown(m_renderContext);
}

void Renderer::createFrameBuffers() {
	VkDeviceSize uboSize = sizeof(CameraUbo);
	for (auto &buffer : m_cameraBuffers) {
		buffer.init(
		    *m_device, m_allocator->get(), uboSize,
		    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		    VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
		    VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);
	}

	VkExtent2D extent = m_presentation->extent();
	m_readbackWidth = extent.width;
	m_readbackHeight = extent.height;
	VkDeviceSize readbackSize = static_cast<VkDeviceSize>(extent.width) * extent.height * 4;
	m_readbackBuffer.init(
	    *m_device, m_allocator->get(), readbackSize,
	    VK_BUFFER_USAGE_TRANSFER_DST_BIT,
	    VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
	    VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);
	m_readbackValid = true;
}

void Renderer::scanScenes() {
	m_sceneState.availableScenes.clear();
	m_sceneState.scenePaths.clear();

#ifdef SCENE_MODELS_DIR
	const std::filesystem::path root = SCENE_MODELS_DIR;
#else
	const std::filesystem::path root = "assets/models";
#endif
	if (!std::filesystem::exists(root))
		return;

	std::vector<std::pair<std::string, std::string>> found;
	for (const auto &entry : std::filesystem::directory_iterator(root)) {
		if (!entry.is_directory())
			continue;
		std::filesystem::path ply = entry.path() / "point_cloud.ply";
		if (!std::filesystem::exists(ply))
			continue;

		found.emplace_back(entry.path().filename().string(), ply.string());
	}

	std::sort(found.begin(), found.end());

	for (auto &[name, path] : found) {
		m_sceneState.availableScenes.push_back(name);
		m_sceneState.scenePaths.push_back(path);
	}

	utils::logInfo("Found {} scene(s) in {}", found.size(), root.string());
}

void Renderer::loadScene(const std::string &plyPath) {
	vkDeviceWaitIdle(m_device->get());

	if (m_scene.isLoaded())
		m_scene.shutdown(*m_allocator);

	m_scene.init(*m_device, *m_allocator, plyPath);

	uint32_t count = m_scene.getCount();

	const io::SceneMetadata &meta = m_scene.getMetadata();
	if (meta.hasCamera)
		m_cameraController.frameFromCamera(meta.cameraPosition, meta.cameraForward, meta.cameraUp, meta.radius);
	else
		m_cameraController.frame(meta.center, meta.radius);

	std::array<VkBuffer, MAX_FRAMES_IN_FLIGHT> cameraHandles;
	for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
		cameraHandles[i] = m_cameraBuffers[i].get();

	// The sort pass produces the per-frame index buffers the rasterizer reads.
	m_sortPass.bind(m_scene.getGaussianBuffer(), cameraHandles, count);

	std::array<VkBuffer, MAX_FRAMES_IN_FLIGHT> indexHandles;
	for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
		indexHandles[i] = m_sortPass.getSortedIndices(i);

	m_rasterizePass.bind(m_scene.getGaussianBuffer(), indexHandles, cameraHandles, count);

	// Derive a display name from the containing directory.
	std::filesystem::path p(plyPath);
	m_sceneState.currentSceneName = p.parent_path().filename().string();
	m_sceneState.gaussianCount = count;
	m_sceneState.shDegree = meta.shDegree;
	m_sceneState.maxShDegree = meta.shDegree;
	m_sceneState.sceneDirty = true;

	utils::logInfo("Loaded scene '{}': {} Gaussians, SH degree {}, radius {:.2f}",
	               m_sceneState.currentSceneName, count, meta.shDegree, meta.radius);
}

void Renderer::update(const app::InputState &input, float dt) {
	if (!m_sceneState.requestedScenePath.empty()) {
		std::string requested = m_sceneState.requestedScenePath;
		m_sceneState.requestedScenePath.clear();
		loadScene(requested);
	}

	if (input.toggleCamera || m_sceneState.cameraToggleRequested) {
		m_sceneState.cameraToggleRequested = false;
		m_cameraController.toggleMode();
		m_sceneState.cameraMode = (m_cameraController.getMode() == app::CameraController::Mode::Fly) ? 1 : 0;
	}

	m_cameraController.update(input, dt);

	VkExtent2D extent = m_presentation->extent();
	float aspect = extent.height > 0 ? static_cast<float>(extent.width) / static_cast<float>(extent.height) : 1.0f;
	m_cameraController.apply(m_camera, aspect);
}

void Renderer::updateCameraBuffer(uint32_t frameIndex) {
	VkExtent2D extent = m_presentation->extent();

	CameraUbo ubo{};
	ubo.view = m_camera.getView();
	ubo.proj = m_camera.getProjection();
	ubo.viewport = glm::vec4(
	    static_cast<float>(extent.width),
	    static_cast<float>(extent.height),
	    1.0f / static_cast<float>(extent.width),
	    1.0f / static_cast<float>(extent.height));
	glm::vec3 pos = m_camera.getPosition();
	ubo.cameraPos = glm::vec4(pos, 1.0f);

	std::memcpy(m_cameraBuffers[frameIndex].getMappedData(), &ubo, sizeof(CameraUbo));
}

void Renderer::drawFrame() {
	if (m_sceneState.showGUI)
		m_guiPass.newFrame(m_stats);

	app::PresentFrame frame;
	m_presentation->beginFrame(frame);

	if (m_scene.isLoaded())
		updateCameraBuffer(frame.frameIndex);

	recordFrame(frame);

	m_presentation->endFrame(frame);

	if (m_sceneState.screenshotRequested && m_readbackValid)
		captureScreenshot();

	updateStats();
}

void Renderer::captureScreenshot() {
	vkQueueWaitIdle(m_device->getGraphicsQueue());

	std::filesystem::create_directories("saves");
	m_readbackBuffer.invalidate(m_allocator->get());

	const uint8_t *src = static_cast<const uint8_t *>(m_readbackBuffer.getMappedData());
	size_t pixelCount = static_cast<size_t>(m_readbackWidth) * m_readbackHeight;
	std::vector<uint8_t> rgba(pixelCount * 4);
	for (size_t i = 0; i < pixelCount; ++i) {
		// Swapchain is BGRA; PNG wants RGBA.
		rgba[i * 4 + 0] = src[i * 4 + 2];
		rgba[i * 4 + 1] = src[i * 4 + 1];
		rgba[i * 4 + 2] = src[i * 4 + 0];
		rgba[i * 4 + 3] = src[i * 4 + 3];
	}

	std::string path = fmt::format("saves/screenshot_{:03d}.png", m_sceneState.screenshotCounter++);
	stbi_write_png(path.c_str(), static_cast<int>(m_readbackWidth), static_cast<int>(m_readbackHeight), 4, rgba.data(), static_cast<int>(m_readbackWidth) * 4);
	utils::logInfo("Saved screenshot {}", path);

	m_sceneState.screenshotRequested = false;
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

	if (m_scene.isLoaded()) {
		m_sortPass.record(m_renderContext, passCtx);
		m_rasterizePass.record(m_renderContext, passCtx);
	}

	if (m_sceneState.showGUI)
		m_guiPass.record(m_renderContext, passCtx);

	// Hand the image to the presentation engine, optionally reading it back first.
	if (m_sceneState.screenshotRequested && m_readbackValid) {
		utils::vk::transitionImage(
		    frame.cmd, frame.target.image,
		    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		    VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
		    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

		VkBufferImageCopy region{
			.bufferOffset = 0,
			.bufferRowLength = 0,
			.bufferImageHeight = 0,
			.imageSubresource = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1 },
			.imageOffset = { 0, 0, 0 },
			.imageExtent = { frame.target.extent.width, frame.target.extent.height, 1 },
		};
		vkCmdCopyImageToBuffer(frame.cmd, frame.target.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_readbackBuffer.get(), 1, &region);

		utils::vk::transitionImage(
		    frame.cmd, frame.target.image,
		    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
		    VK_ACCESS_TRANSFER_READ_BIT, 0,
		    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
	} else {
		utils::vk::transitionImage(
		    frame.cmd, frame.target.image,
		    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
		    VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0,
		    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
	}
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
