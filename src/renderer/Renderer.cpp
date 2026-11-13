#include "Renderer.hpp"

#include "app/IPresentation.hpp"
#include "app/InputState.hpp"
#include "core/Device.hpp"
#include "core/VulkanContext.hpp"
#include "gpu/Allocator.hpp"
#include "gpu/MemoryTracker.hpp"
#include "io/GpuSplatData.hpp"
#include "utils/Log.hpp"
#include "utils/VulkanUtils.hpp"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fmt/format.h>
#include <glm/gtc/matrix_transform.hpp>
#include <stdexcept>

namespace renderer {

void Renderer::init(core::Device &device, gpu::Allocator &allocator, core::VulkanContext *context, app::IPresentation &presentation, GLFWwindow *window, const std::string &initialScene) {
	m_device = &device;
	m_allocator = &allocator;
	m_presentation = &presentation;
	m_presentationExtent = presentation.extent();
	m_presentationImageCount = presentation.imageCount();

	m_renderContext = RenderContext{
		.device = &device,
		.allocator = &allocator,
		.extent = presentation.extent(),
		.format = presentation.colorFormat(),
		.imageCount = presentation.imageCount(),
		.sceneState = &m_sceneState,
	};

	if (m_foveationRate > 1 && !device.fragmentShadingRateSupported()) {
		utils::logWarn("Fixed foveation requested but VK_KHR_fragment_shading_rate is unsupported");
		m_foveationRate = 0;
	}
	const bool foveation = m_foveationRate > 1;
	if (foveation)
		m_shadingRate = 1;        // foveation supplies the per-fragment rate

	m_renderContext.foveation = foveation;
	m_renderContext.fragmentShadingRate = device.fragmentShadingRateSupported() && (m_shadingRate > 1 || foveation);
	m_renderContext.fragmentShadingRateExtent = { m_shadingRate, m_shadingRate };

	const char *vrsState = m_renderContext.fragmentShadingRate ? (foveation ? "fixed foveation" : "uniform") : "off";
	utils::logInfo("Fragment shading rate: {} (uniform {}x{}, foveation {})", vrsState,
	               m_shadingRate, m_shadingRate, foveation ? m_foveationRate : 0);

	m_renderContext.foveationDebug = foveation && m_foveationDebug;
	if (m_foveationDebug && !foveation)
		utils::logWarn("Foveation debug overlay requires --vr-foveation");

	m_renderContext.foveationBlur = foveation && m_foveationBlur;
	createOffscreenTargets();
	m_renderContext.offscreenViews = { m_offscreen[0].getView(), m_offscreen[1].getView() };
	utils::logInfo("Foveation blur post-process: {}", m_renderContext.foveationBlur ? "on" : "off");

	m_profiler.init(device, MAX_FRAMES_IN_FLIGHT, GpuSlot::Count);
	m_renderContext.profiler = &m_profiler;

	m_backgroundPass.init(m_renderContext);
	m_rasterizePass.init(m_renderContext);
	m_sortPass.init(m_renderContext);
	m_foveationDebugPass.init(m_renderContext);
	m_foveationBlurPass.init(m_renderContext);
	m_tileRasterizer.init(device, allocator, m_renderContext, presentation.extent().width, presentation.extent().height);
	m_guiPass.init(m_renderContext);

	core::SwapchainInfo swapchainInfo{
		.format = presentation.colorFormat(),
		.extent = presentation.extent(),
		.imageCount = presentation.imageCount(),
	};
	if (window && context) {
		m_guiPass.initVulkan(window, *context, device, allocator, swapchainInfo, m_sceneState);
		m_guiReady = true;
	} else {
		// VR has no window/GUI yet.
		m_sceneState.showGUI = false;
	}

	createFrameBuffers();
	scanScenes();

	// Load the requested scene. Accept a .ply path or a scene name (the
	// directory under assets/models); fall back to the default otherwise.
	std::string scenePath;
	if (!initialScene.empty()) {
		if (std::filesystem::exists(initialScene)) {
			scenePath = initialScene;
		} else {
			for (size_t i = 0; i < m_sceneState.availableScenes.size(); ++i) {
				if (m_sceneState.availableScenes[i] == initialScene) {
					scenePath = m_sceneState.scenePaths[i];
					break;
				}
			}
			if (scenePath.empty())
				utils::logWarn("Scene '{}' not found under assets/models; using the default", initialScene);
		}
	}

	if (scenePath.empty() && !m_sceneState.scenePaths.empty()) {
		size_t defaultIndex = 0;
		for (size_t i = 0; i < m_sceneState.availableScenes.size(); ++i) {
			if (m_sceneState.availableScenes[i] == "room") {
				defaultIndex = i;
				break;
			}
		}
		scenePath = m_sceneState.scenePaths[defaultIndex];
	}

	if (!scenePath.empty())
		loadScene(scenePath);

	m_lastFrameTime = std::chrono::steady_clock::now();
}

void Renderer::shutdown() {
	vkDeviceWaitIdle(m_device->get());

	if (m_foveation) {
		m_foveation->shutdown();
		m_foveation.reset();
	}

	for (auto &frameBuffers : m_cameraBuffers)
		for (auto &buffer : frameBuffers)
			buffer.shutdown(m_allocator->get());

	if (m_readbackValid) {
		m_readbackBuffer.shutdown(m_allocator->get());
		m_readbackValid = false;
	}

	if (m_scene.isLoaded())
		m_scene.shutdown(*m_allocator);

	m_sortPass.shutdown(m_renderContext);
	m_tileRasterizer.shutdown();
	if (m_guiReady)
		m_guiPass.shutdown(m_renderContext);
	m_foveationDebugPass.shutdown(m_renderContext);
	m_foveationBlurPass.shutdown(m_renderContext);
	for (auto &image : m_offscreen)
		image.shutdown(m_allocator->get());
	m_rasterizePass.shutdown(m_renderContext);
	m_backgroundPass.shutdown(m_renderContext);

	m_profiler.shutdown();
}

void Renderer::createFrameBuffers() {
	VkDeviceSize uboSize = sizeof(CameraUbo);	for (auto &frameBuffers : m_cameraBuffers) {
		for (auto &buffer : frameBuffers) {
			buffer.init(
			    *m_device, m_allocator->get(), uboSize,
			    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			    VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
			    VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);
		}
	}

	VkExtent2D extent = m_presentation->extent();
	m_readbackWidth = extent.width;
	m_readbackHeight = extent.height;
	VkDeviceSize perEyeBytes = static_cast<VkDeviceSize>(extent.width) * extent.height * 4;
	VkDeviceSize readbackSize = perEyeBytes * app::kMaxViews;
	m_readbackBuffer.init(
	    *m_device, m_allocator->get(), readbackSize,
	    VK_BUFFER_USAGE_TRANSFER_DST_BIT,
	    VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
	    VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);
	m_readbackValid = true;
}

void Renderer::createOffscreenTargets() {
	VkExtent2D extent = m_presentation->extent();
	VkFormat format = m_presentation->colorFormat();
	for (auto &image : m_offscreen) {
		image.init(*m_device, m_allocator->get(), extent, format,
		           VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		           VK_IMAGE_ASPECT_COLOR_BIT);
	}
}

void Renderer::scanScenes() {
	m_sceneState.availableScenes.clear();
	m_sceneState.scenePaths.clear();
	m_sceneState.sceneEstimatedBytes.clear();

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

		uint32_t count = io::PlyLoader::readVertexCount(path);
		m_sceneState.sceneEstimatedBytes.push_back(estimateGpuBytes(count));
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

	// Remember the scene's up so VR can level the starting view. Use the
	// average camera up (the reconstruction's vertical), not the first camera's
	// up, which is tilted with that camera's roll/pitch.
	if (meta.hasCamera && glm::dot(meta.worldUp, meta.worldUp) > 1e-6f)
		m_sceneUp = glm::normalize(meta.worldUp);
	else if (meta.hasCamera && glm::dot(meta.cameraUp, meta.cameraUp) > 1e-6f)
		m_sceneUp = glm::normalize(meta.cameraUp);
	else
		m_sceneUp = glm::vec3(0.0f, 1.0f, 0.0f);
	utils::logInfo("Scene up for VR leveling: ({:.2f}, {:.2f}, {:.2f})", m_sceneUp.x, m_sceneUp.y, m_sceneUp.z);
	m_loggedViewCameras = false;

	std::array<VkBuffer, MAX_FRAMES_IN_FLIGHT> cameraHandles;
	std::array<std::array<VkBuffer, app::kMaxViews>, MAX_FRAMES_IN_FLIGHT> viewCameraHandles;
	for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
		cameraHandles[i] = m_cameraBuffers[i][0].get();
		for (uint32_t v = 0; v < app::kMaxViews; ++v)
			viewCameraHandles[i][v] = m_cameraBuffers[i][v].get();
	}

	// The sort pass produces the per-frame index buffers the rasterizer reads.
	// Sorting happens once per frame from the centre (view 0) camera and is
	// reused for all views.
	m_sortPass.bind(m_scene.getGaussianBuffer(), cameraHandles, count);
	m_tileRasterizer.bind(m_scene.getGaussianBuffer(), cameraHandles, count);

	std::array<VkBuffer, MAX_FRAMES_IN_FLIGHT> indexHandles;
	std::array<VkBuffer, MAX_FRAMES_IN_FLIGHT> indirectHandles;
	for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
		indexHandles[i] = m_sortPass.getSortedIndices(i);
		indirectHandles[i] = m_sortPass.getIndirectBuffer(i);
	}

	m_rasterizePass.bind(m_scene.getGaussianBuffer(), indexHandles, viewCameraHandles, indirectHandles, count);

	// Derive a display name from the containing directory.
	std::filesystem::path p(plyPath);
	m_sceneState.currentSceneName = p.parent_path().filename().string();
	m_sceneState.gaussianCount = count;
	m_sceneState.shDegree = meta.shDegree;
	m_sceneState.maxShDegree = meta.shDegree;
	m_sceneState.sceneCenter = meta.center;
	m_sceneState.sceneRadius = meta.radius;
	m_sceneState.sceneDirty = true;
	m_hasSortView = false;        // force a sort rebuild for the new scene

	utils::logInfo("Loaded scene '{}': {} Gaussians, SH degree {}, radius {:.2f}",
	               m_sceneState.currentSceneName, count, meta.shDegree, meta.radius);

	auto mib = [](uint64_t bytes) { return static_cast<double>(bytes) / (1024.0 * 1024.0); };
	uint64_t swapchainBytes = static_cast<uint64_t>(m_presentationImageCount) *
	                          m_presentationExtent.width * m_presentationExtent.height * 4;
	utils::logInfo("Estimated GPU memory: {:.1f} MiB (scene {:.1f}, sort {:.1f}, swapchain {:.1f})",
	               mib(estimateGpuBytes(count)), mib(m_scene.getGpuBytes()),
	               mib(m_sortPass.getGpuBytes()), mib(swapchainBytes));
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

void Renderer::updateSortDirty() {
	if (!m_scene.isLoaded()) {
		m_sceneState.sortDirty = false;
		return;
	}

	glm::mat4 view = m_camera.getView();
	// OpenXR supplies per-frame eye poses that are not part of m_camera; the
	// sort (built from view 0) must follow the head every frame.
	const bool externalViews = m_presentation->providesViewCameras();
	bool changed = externalViews || m_sceneState.forceSort || !m_hasSortView || view != m_lastSortView ||
	               static_cast<int>(m_sceneState.renderMode) != m_prevRenderMode ||
	               m_sceneState.shDegree != m_prevShDegree ||
	               m_sceneState.splatScale != m_prevSplatScale ||
	               m_sceneState.cullLargeSplats != m_prevCull ||
	               m_sceneState.maxSplatFraction != m_prevMaxSplat ||
	               m_sceneState.minOpacity != m_prevMinOpacity ||
	               m_sceneState.maxWorldScaleFraction != m_prevMaxWorldScale ||
	               m_sceneState.maxDistanceFactor != m_prevMaxDistance ||
	               m_sceneState.sortEnabled != m_prevSortEnabled;

	if (changed) {
		m_lastSortView = view;
		m_hasSortView = true;
		m_sortCooldown = MAX_FRAMES_IN_FLIGHT;        // rebuild every frame slot
	}

	m_sceneState.sortDirty = changed || m_sortCooldown > 0;
	if (m_sortCooldown > 0)
		--m_sortCooldown;

	m_prevRenderMode = static_cast<int>(m_sceneState.renderMode);
	m_prevShDegree = m_sceneState.shDegree;
	m_prevSplatScale = m_sceneState.splatScale;
	m_prevCull = m_sceneState.cullLargeSplats;
	m_prevMaxSplat = m_sceneState.maxSplatFraction;
	m_prevMinOpacity = m_sceneState.minOpacity;
	m_prevMaxWorldScale = m_sceneState.maxWorldScaleFraction;
	m_prevMaxDistance = m_sceneState.maxDistanceFactor;
	m_prevSortEnabled = m_sceneState.sortEnabled;
}

void Renderer::updateCameraBuffers(const app::PresentFrame &frame) {
	const VkExtent2D fullExtent = m_presentation->extent();
	const float fullAspect = fullExtent.height > 0
	                             ? static_cast<float>(fullExtent.width) / static_cast<float>(fullExtent.height)
	                             : 1.0f;

	// Camera-space right axis from the base view matrix, used to offset the
	// synthetic desktop stereo test eyes. XR supplies real eye poses instead.
	const glm::mat4 &baseView = m_camera.getView();
	const glm::vec3 basePos = m_camera.getPosition();

	// VR starts level: keep the scene camera's yaw but discard its pitch/roll, so
	// looking straight ahead doesn't inherit the dataset camera's downward tilt.
	glm::mat4 xrBaseView = baseView;
	if (frame.viewCameras[0].valid) {
		const glm::vec3 up = glm::normalize(m_sceneUp);
		const glm::vec3 forward = -glm::normalize(glm::vec3(baseView[0][2], baseView[1][2], baseView[2][2]));
		const glm::vec3 flat = forward - glm::dot(forward, up) * up;
		if (glm::length(flat) > 1e-4f)
			xrBaseView = glm::lookAt(basePos, basePos + glm::normalize(flat), up);
	}

	// 3DGS scenes are in arbitrary units. Scale scene->eye space about the base
	// camera. Keeping the homogeneous row at 1 makes this a plain affine scale:
	// the monocular view is unchanged, but IPD/head-motion parallax is divided
	// by the scale, so larger values make the world feel larger.
	xrBaseView = xrBaseView * m_worldScale;
	xrBaseView[3][3] = 1.0f;
	const glm::mat4 xrBaseWorld = glm::inverse(xrBaseView);

	glm::vec3 right{
		baseView[0][0],
		baseView[1][0],
		baseView[2][0],
	};

	constexpr float kEyeSeparation = 0.064f;

	for (uint32_t v = 0; v < frame.viewCount; ++v) {
		const VkRect2D area = frame.targets[v].renderArea;
		const float viewAspect = area.extent.height > 0
		                             ? static_cast<float>(area.extent.width) / static_cast<float>(area.extent.height)
		                             : fullAspect;

		scene::Camera camera = m_camera;
		const app::PresentViewCamera &presented = frame.viewCameras[v];

		if (presented.valid) {
			// Compose the runtime's eye pose with the scaled scene frame, so the
			// head pose is metric while locomotion/scaling stays in the renderer.
			camera.setViewMatrix(presented.localView * xrBaseView);
			camera.setProjectionMatrix(presented.projection);
			camera.setPosition(glm::vec3(xrBaseWorld * glm::vec4(presented.localPosition, 1.0f)));

			if (v == 0 && !m_loggedViewCameras) {
				const glm::vec3 &local = presented.localPosition;
				const glm::vec3 &base = m_camera.getPosition();
				const glm::vec3 &eye = camera.getPosition();
				const glm::mat4 &projection = presented.projection;
				auto forwardOf = [](const glm::mat4 &m) {
					return -glm::normalize(glm::vec3(m[0][2], m[1][2], m[2][2]));
				};
				const glm::vec3 baseForward = forwardOf(baseView);
				const glm::vec3 eyeForward = forwardOf(camera.getView());
				utils::logInfo("XR world scale {:.2f}: base eye height {:.2f} m, eye pos ({:.2f}, {:.2f}, {:.2f})",
				               m_worldScale, glm::vec3(xrBaseWorld[3]).y, eye.x, eye.y, eye.z);
				utils::logInfo("XR view0: basePos ({:.2f}, {:.2f}, {:.2f}) baseFwd ({:.2f}, {:.2f}, {:.2f})", base.x, base.y, base.z, baseForward.x, baseForward.y, baseForward.z);
				utils::logInfo("XR view0: localPos ({:.2f}, {:.2f}, {:.2f}) eyeFwd ({:.2f}, {:.2f}, {:.2f})", local.x, local.y, local.z, eyeForward.x, eyeForward.y, eyeForward.z);
				utils::logInfo("XR view0: proj00 {:.3f} proj11 {:.3f} proj20 {:.3f} proj21 {:.3f} proj22 {:.3f} proj23 {:.3f}",
				               projection[0][0], projection[1][1], projection[2][0], projection[2][1],
				               projection[2][2], projection[2][3]);

				const io::SceneMetadata &meta = m_scene.getMetadata();
				const glm::vec4 centerView = camera.getView() * glm::vec4(meta.center, 1.0f);
				const glm::vec4 centerClip = camera.getProjection() * centerView;
				const glm::vec3 centerNdc = glm::vec3(centerClip) / centerClip.w;
				utils::logInfo("XR view0: scene center ({:.2f}, {:.2f}, {:.2f}) viewZ {:.2f} w {:.2f} ndc ({:.3f}, {:.3f}, {:.3f})",
				               meta.center.x, meta.center.y, meta.center.z, centerView.z, centerClip.w,
				               centerNdc.x, centerNdc.y, centerNdc.z);
				m_loggedViewCameras = true;
			}
		} else if (frame.viewCount > 1) {
			const float eyeOffset = (v == 0 ? -0.5f : 0.5f) * kEyeSeparation;
			const glm::vec3 worldOffset = right * eyeOffset;
			camera.setViewMatrix(baseView * glm::translate(glm::mat4(1.0f), -worldOffset));
			camera.setPosition(m_camera.getPosition() + worldOffset);

			// The base projection was built for the full window; scale X for the
			// narrower per-view render area.
			glm::mat4 projection = m_camera.getProjection();
			projection[0][0] *= fullAspect / viewAspect;
			camera.setProjectionMatrix(projection);
		}
		m_viewCameras[v] = camera;

		CameraUbo ubo{};
		ubo.view = camera.getView();
		ubo.proj = camera.getProjection();
		ubo.viewport = glm::vec4(
		    static_cast<float>(area.extent.width),
		    static_cast<float>(area.extent.height),
		    1.0f / static_cast<float>(area.extent.width),
		    1.0f / static_cast<float>(area.extent.height));
		ubo.viewportOrigin = glm::vec4(
		    static_cast<float>(area.offset.x),
		    static_cast<float>(area.offset.y),
		    0.0f,
		    0.0f);
		glm::vec3 pos = camera.getPosition();
		ubo.cameraPos = glm::vec4(pos, 1.0f);

		std::memcpy(m_cameraBuffers[frame.frameIndex][v].getMappedData(), &ubo, sizeof(CameraUbo));
	}
}

void Renderer::drawFrame() {
	updateSortDirty();

	if (m_sceneState.showGUI && m_guiReady)
		m_guiPass.newFrame(m_stats);

	app::PresentFrame frame;
	m_presentation->beginFrame(frame);

	// Read the previous submission's GPU timings for this frame slot (the fence
	// was just waited on, so the results are available without a stall).
	if (m_profiler.valid() && m_profiler.read(frame.frameIndex, m_gpuMs)) {
		auto ema = [](float &acc, float value) { acc = acc * 0.9f + value * 0.1f; };
		ema(m_stats.gpuBackgroundMs, m_gpuMs[0]);
		ema(m_stats.gpuKeygenMs, m_gpuMs[1]);
		ema(m_stats.gpuRadixMs, m_gpuMs[2]);
		ema(m_stats.gpuRasterMs, m_gpuMs[3]);
		ema(m_stats.gpuGuiMs, m_gpuMs[4]);
		float total = 0.0f;
		for (float v : m_gpuMs)
			total += v;
		ema(m_stats.gpuTotalMs, total);
		m_stats.gpuTimingsValid = true;

		for (size_t i = 0; i < m_gpuMs.size() && i < GpuSlot::Count; ++i)
			m_profileSums[i] += m_gpuMs[i];
		++m_profileFrames;
	}

	m_renderContext.foveationDebug = m_foveationDebug && m_renderContext.foveation;

	if (m_renderContext.foveation && !m_foveation && frame.viewCount > 0) {
		try {
			const glm::mat4 leftProjection =
			    frame.viewCameras[0].valid ? frame.viewCameras[0].projection : m_camera.getProjection();
			const glm::mat4 rightProjection =
			    (frame.viewCount > 1 && frame.viewCameras[1].valid) ? frame.viewCameras[1].projection : leftProjection;
			m_foveation = std::make_unique<Foveation>();
			m_foveation->init(*m_device, *m_allocator, frame.targets[0].extent,
			                  leftProjection, rightProjection, m_foveationRate, m_foveationSize, m_foveationRing);
			m_renderContext.foveationImages = m_foveation.get();
		} catch (const std::exception &e) {
			utils::logWarn("Fixed foveation unavailable: {}", e.what());
			m_renderContext.foveation = false;
			m_renderContext.foveationImages = nullptr;
			m_foveationRate = 0;
		}
	}

	if (m_scene.isLoaded())
		updateCameraBuffers(frame);

	recordFrame(frame);

	m_presentation->endFrame(frame);

	if (m_sceneState.screenshotRequested && m_readbackValid)
		captureScreenshot();

	updateStats();
}

void Renderer::toggleFoveationDebug() {
	m_foveationDebug = !m_foveationDebug;
	utils::logInfo("Foveation debug: {}", m_foveationDebug ? "on" : "off");
}

void Renderer::logGpuTimings() {
	if (m_profileFrames == 0) {
		utils::logInfo("GPU timings unavailable (timestamps unsupported)");
		return;
	}
	float n = static_cast<float>(m_profileFrames);
	float total = 0.0f;
	for (uint32_t i = 0; i < GpuSlot::Count; ++i)
		total += m_profileSums[i];
	utils::logInfo("GPU timings (avg {} frames): bg {:.2f} | keygen {:.2f} | radix {:.2f} | raster {:.2f} | gui {:.2f} | total {:.2f} ms",
	               m_profileFrames,
	               m_profileSums[0] / n, m_profileSums[1] / n, m_profileSums[2] / n,
	               m_profileSums[3] / n, m_profileSums[4] / n, total / n);
}

void Renderer::captureScreenshot() {
	vkQueueWaitIdle(m_device->getGraphicsQueue());

	std::filesystem::create_directories("saves");
	m_readbackBuffer.invalidate(m_allocator->get());

	const uint8_t *src = static_cast<const uint8_t *>(m_readbackBuffer.getMappedData());
	const uint32_t eyeWidth = m_readbackWidth;
	const uint32_t eyeHeight = m_readbackHeight;
	const uint32_t eyes = std::max(1u, m_screenshotEyeCount);
	const uint32_t outputWidth = eyeWidth * eyes;
	const size_t eyePixels = static_cast<size_t>(eyeWidth) * eyeHeight;

	std::vector<uint8_t> rgba(static_cast<size_t>(outputWidth) * eyeHeight * 4);
	for (uint32_t eye = 0; eye < eyes; ++eye) {
		const uint8_t *eyeSrc = src + static_cast<size_t>(eye) * eyePixels * 4;
		for (uint32_t y = 0; y < eyeHeight; ++y) {
			for (uint32_t x = 0; x < eyeWidth; ++x) {
				const size_t si = (static_cast<size_t>(y) * eyeWidth + x) * 4;
				const size_t di = (static_cast<size_t>(y) * outputWidth + static_cast<size_t>(eye) * eyeWidth + x) * 4;
				// Swapchain is BGRA; PNG wants RGBA.
				rgba[di + 0] = eyeSrc[si + 2];
				rgba[di + 1] = eyeSrc[si + 1];
				rgba[di + 2] = eyeSrc[si + 0];
				rgba[di + 3] = eyeSrc[si + 3];
			}
		}
	}

	std::string path = fmt::format("saves/screenshot_{:03d}.png", m_sceneState.screenshotCounter++);
	stbi_write_png(path.c_str(), static_cast<int>(outputWidth), static_cast<int>(eyeHeight), 4, rgba.data(), static_cast<int>(outputWidth) * 4);
	utils::logInfo("Saved screenshot {} ({} eye(s), {}x{})", path, eyes, outputWidth, eyeHeight);

	m_sceneState.screenshotRequested = false;
}

void Renderer::recordFrame(const app::PresentFrame &frame) {
	if (frame.viewCount == 0)
		return;

	PassContext baseCtx{
		.cmd = frame.cmd,
		.frameIndex = frame.frameIndex,
		.imageIndex = frame.targets[0].index,
	};

	if (m_profiler.valid()) {
		m_profiler.beginFrame(frame.cmd, frame.frameIndex);
		m_profiler.write(frame.cmd, frame.frameIndex, GpuSlot::FrameStart);
	}

	const bool tiled = m_scene.isLoaded() && m_sceneState.renderMode == RenderMode::Tiled;
	const bool useTiledPath = tiled && frame.viewCount == 1;
	const bool useOffscreen = !useTiledPath && m_renderContext.foveation && m_renderContext.foveationBlur;

	if (tiled && frame.viewCount > 1 && !m_warnedTiledMultiView) {
		utils::logInfo("Tiled rasterizer does not support multiple views yet; using the instanced rasterizer");
		m_warnedTiledMultiView = true;
	}

	if (useTiledPath) {
		// The tile rasterizer writes the background and composites to the target,
		// and emits its own per-stage timestamps.
		PassContext passCtx = baseCtx;
		passCtx.viewIndex = 0;
		passCtx.targetImage = frame.targets[0].image;
		passCtx.targetView = frame.targets[0].view;
		passCtx.extent = frame.targets[0].extent;
		passCtx.renderArea = frame.targets[0].renderArea;
		passCtx.clearTarget = frame.targets[0].clear;
		m_tileRasterizer.record(m_renderContext, passCtx);
		if (m_renderContext.foveationDebug)
			m_foveationDebugPass.record(m_renderContext, passCtx);
	} else {
		if (useOffscreen) {
			// Render into the per-eye offscreen images; they are sampled by the
			// blur pass, so track their layout across frames.
			for (uint32_t v = 0; v < frame.viewCount; ++v)
				m_offscreen[v].transition(frame.cmd, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
		} else {
			// Bring each distinct target image into the colour-attachment layout once.
			// Multiple views may share one image (desktop stereo test), so reusing
			// UNDEFINED for the second view would discard the first view's pixels.
			for (uint32_t v = 0; v < frame.viewCount; ++v) {
				bool firstUse = true;
				for (uint32_t p = 0; p < v; ++p) {
					if (frame.targets[p].image == frame.targets[v].image) {
						firstUse = false;
						break;
					}
				}
				if (!firstUse)
					continue;

				utils::vk::transitionImage(
				    frame.cmd, frame.targets[v].image,
				    VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
				    0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
				    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
			}
		}

		for (uint32_t v = 0; v < frame.viewCount; ++v) {
			if (v > 0 && !useOffscreen) {
				// Adjacent dynamic-rendering instances touch the same image; make
				// the previous view's writes visible for the next view's LOAD.
				VkMemoryBarrier barrier{
					.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
					.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
					.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
				};
				vkCmdPipelineBarrier(frame.cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				                     VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				                     0, 1, &barrier, 0, nullptr, 0, nullptr);
			}

			PassContext passCtx = baseCtx;
			passCtx.viewIndex = v;
			if (useOffscreen) {
				passCtx.targetImage = m_offscreen[v].get();
				passCtx.targetView = m_offscreen[v].getView();
				passCtx.extent = frame.targets[v].extent;
				passCtx.renderArea = { .offset = { 0, 0 }, .extent = frame.targets[v].extent };
				passCtx.clearTarget = true;
			} else {
				passCtx.targetImage = frame.targets[v].image;
				passCtx.targetView = frame.targets[v].view;
				passCtx.extent = frame.targets[v].extent;
				passCtx.renderArea = frame.targets[v].renderArea;
				passCtx.clearTarget = frame.targets[v].clear;
			}

			m_backgroundPass.record(m_renderContext, passCtx);
			if (v == 0 && m_profiler.valid())
				m_profiler.write(frame.cmd, frame.frameIndex, GpuSlot::Background);

			if (m_scene.isLoaded()) {
				// Sort once from the centre view, then reuse for every view.
				if (v == 0)
					m_sortPass.record(m_renderContext, passCtx);
				m_rasterizePass.record(m_renderContext, passCtx);
			}
			if (!useOffscreen && m_renderContext.foveationDebug)
				m_foveationDebugPass.record(m_renderContext, passCtx);
		}

		if (useOffscreen) {
			for (uint32_t v = 0; v < frame.viewCount; ++v)
				m_offscreen[v].transition(frame.cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

			// The blur writes the presentation targets; bring each distinct image
			// into the colour-attachment layout first.
			for (uint32_t v = 0; v < frame.viewCount; ++v) {
				bool firstUse = true;
				for (uint32_t p = 0; p < v; ++p) {
					if (frame.targets[p].image == frame.targets[v].image) {
						firstUse = false;
						break;
					}
				}
				if (!firstUse)
					continue;

				utils::vk::transitionImage(
				    frame.cmd, frame.targets[v].image,
				    VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
				    0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
				    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
			}

			for (uint32_t v = 0; v < frame.viewCount; ++v) {
				PassContext outCtx = baseCtx;
				outCtx.viewIndex = v;
				outCtx.targetImage = frame.targets[v].image;
				outCtx.targetView = frame.targets[v].view;
				outCtx.extent = frame.targets[v].extent;
				outCtx.renderArea = frame.targets[v].renderArea;
				outCtx.clearTarget = false;

				m_foveationBlurPass.record(m_renderContext, outCtx);
				if (m_renderContext.foveationDebug)
					m_foveationDebugPass.record(m_renderContext, outCtx);
			}
		}

		if (m_profiler.valid())
			m_profiler.write(frame.cmd, frame.frameIndex, GpuSlot::Raster);
	}

	if (m_sceneState.showGUI && m_guiReady) {
		PassContext guiCtx = baseCtx;
		guiCtx.targetImage = frame.targets[0].image;
		guiCtx.targetView = frame.targets[0].view;
		guiCtx.extent = frame.targets[0].extent;
		guiCtx.renderArea = { .offset = { 0, 0 }, .extent = frame.targets[0].extent };
		guiCtx.clearTarget = false;
		m_guiPass.record(m_renderContext, guiCtx);
	}
	if (m_profiler.valid() && !useTiledPath)
		m_profiler.write(frame.cmd, frame.frameIndex, GpuSlot::Gui);

	// Hand the image to the presentation engine, optionally reading it back first.
	if (m_sceneState.screenshotRequested && m_readbackValid) {
		// Default: both eyes side by side. --screenshot-eye 0|1 selects one.
		// Views sharing one image (desktop stereo test) are copied once.
		std::array<uint32_t, app::kMaxViews> eyes{};
		uint32_t eyeCount = 0;
		if (m_screenshotEye >= 0 && static_cast<uint32_t>(m_screenshotEye) < frame.viewCount) {
			eyes[0] = static_cast<uint32_t>(m_screenshotEye);
			eyeCount = 1;
		} else {
			for (uint32_t v = 0; v < frame.viewCount && eyeCount < app::kMaxViews; ++v) {
				bool duplicate = false;
				for (uint32_t i = 0; i < eyeCount; ++i) {
					if (frame.targets[eyes[i]].image == frame.targets[v].image) {
						duplicate = true;
						break;
					}
				}
				if (!duplicate)
					eyes[eyeCount++] = v;
			}
		}
		m_screenshotEyeCount = std::max(1u, eyeCount);

		const VkDeviceSize perEyeBytes = static_cast<VkDeviceSize>(m_readbackWidth) * m_readbackHeight * 4;
		for (uint32_t i = 0; i < eyeCount; ++i) {
			const app::PresentTarget &shotTarget = frame.targets[eyes[i]];

			utils::vk::transitionImage(
			    frame.cmd, shotTarget.image,
			    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			    VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
			    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

			VkBufferImageCopy region{
				.bufferOffset = perEyeBytes * i,
				.bufferRowLength = 0,
				.bufferImageHeight = 0,
				.imageSubresource = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1 },
				.imageOffset = { 0, 0, 0 },
				.imageExtent = { shotTarget.extent.width, shotTarget.extent.height, 1 },
			};
			vkCmdCopyImageToBuffer(frame.cmd, shotTarget.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_readbackBuffer.get(), 1, &region);

			utils::vk::transitionImage(
			    frame.cmd, shotTarget.image,
			    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, shotTarget.finalLayout,
			    VK_ACCESS_TRANSFER_READ_BIT, 0,
			    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
		}
	} else {
		// Each distinct target image (one per eye in XR) goes to the layout its
		// presentation expects. Desktop presents; XR hands back colour attachments.
		for (uint32_t v = 0; v < frame.viewCount; ++v) {
			bool firstUse = true;
			for (uint32_t p = 0; p < v; ++p) {
				if (frame.targets[p].image == frame.targets[v].image) {
					firstUse = false;
					break;
				}
			}
			if (!firstUse)
				continue;
			if (frame.targets[v].finalLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
				continue;

			utils::vk::transitionImage(
			    frame.cmd, frame.targets[v].image,
			    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, frame.targets[v].finalLayout,
			    VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0,
			    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
		}
	}

	if (m_profiler.valid())
		m_profiler.write(frame.cmd, frame.frameIndex, GpuSlot::FrameEnd);
}

uint64_t Renderer::estimateGpuBytes(uint32_t count) const {
	uint64_t scene = static_cast<uint64_t>(count) * sizeof(io::GpuGaussian);
	uint64_t sort = SortPass::getGpuBytesForCount(count);
	uint64_t pixels = static_cast<uint64_t>(m_presentationExtent.width) * m_presentationExtent.height;
	uint64_t frame = static_cast<uint64_t>(MAX_FRAMES_IN_FLIGHT) * app::kMaxViews * sizeof(CameraUbo) + pixels * 4;
	frame += static_cast<uint64_t>(app::kMaxViews) * pixels * 4;        // offscreen post-process targets
	uint64_t swapchain = static_cast<uint64_t>(m_presentationImageCount) * pixels * 4;
	return scene + sort + frame + swapchain;
}

void Renderer::updateStats() {
	auto now = std::chrono::steady_clock::now();
	float ms = std::chrono::duration<float, std::milli>(now - m_lastFrameTime).count();
	m_lastFrameTime = now;

	m_stats.cpuFrameMs = ms;
	m_stats.fps = ms > 0.0f ? 1000.0f / ms : 0.0f;

	uint64_t pixels = static_cast<uint64_t>(m_presentationExtent.width) * m_presentationExtent.height;
	m_stats.gpuSceneBytes = m_scene.isLoaded() ? m_scene.getGpuBytes() : 0;
	m_stats.gpuSortBytes = m_sortPass.getGpuBytes();
	m_stats.gpuFrameBytes = static_cast<uint64_t>(MAX_FRAMES_IN_FLIGHT) * app::kMaxViews * sizeof(CameraUbo) + pixels * 4;
	m_stats.gpuFrameBytes += static_cast<uint64_t>(app::kMaxViews) * pixels * 4;        // offscreen targets
	m_stats.gpuSwapchainBytes = static_cast<uint64_t>(m_presentationImageCount) * pixels * 4;
	m_stats.gpuTotalBytes = gpu::memory::totalBytes();
	m_stats.gpuAllocations = gpu::memory::allocations();
}

bool Renderer::wantsCaptureMouse() const {
	return m_guiReady && m_guiPass.wantsCaptureMouse();
}

bool Renderer::wantsCaptureKeyboard() const {
	return m_guiReady && m_guiPass.wantsCaptureKeyboard();
}

}        // namespace renderer
