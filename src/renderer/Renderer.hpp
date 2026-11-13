#pragma once

#include "RenderContext.hpp"
#include "RenderStats.hpp"
#include "SceneState.hpp"
#include "TileRasterizer.hpp"
#include "app/CameraController.hpp"
#include "gpu/Buffer.hpp"
#include "gpu/Image.hpp"
#include "renderer/Foveation.hpp"
#include "renderer/GpuProfiler.hpp"
#include "renderer/passes/BackgroundPass.hpp"
#include "renderer/passes/FoveationBlurPass.hpp"
#include "renderer/passes/FoveationDebugPass.hpp"
#include "renderer/passes/GUIPass.hpp"
#include "renderer/passes/RasterizePass.hpp"
#include "renderer/passes/SortPass.hpp"
#include "scene/Camera.hpp"
#include "scene/GaussianScene.hpp"

#include <array>
#include <chrono>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

struct GLFWwindow;

namespace core {
class Device;
class VulkanContext;
}

namespace gpu {
class Allocator;
}

namespace app {
class IPresentation;
struct InputState;
struct PresentFrame;
}

namespace renderer {

// Orchestrates the passes for a frame. It renders into whatever target the
// presentation layer hands it, so the same renderer serves desktop and XR.
class Renderer {
  public:
	void init(core::Device &device, gpu::Allocator &allocator, core::VulkanContext *context, app::IPresentation &presentation, GLFWwindow *window, const std::string &initialScene = "");
	void shutdown();

	void update(const app::InputState &input, float dt);
	void drawFrame();
	void logGpuTimings();

	void loadScene(const std::string &plyPath);

	// Uniform scale applied to the scene around the base camera in VR. 3DGS
	// reconstructions are in arbitrary units; 1.0 means "one scene unit = one
	// metre". Values > 1 make the world feel larger.
	void setWorldScale(float scale) {
		m_worldScale = scale > 0.0f ? scale : 1.0f;
	}
	float getWorldScale() const {
		return m_worldScale;
	}

	// Which presentation target (eye) --screenshot reads back. -1 = both eyes,
	// 0/1 = a single eye.
	void setScreenshotEye(int32_t eye) {
		m_screenshotEye = eye;
	}

	// Uniform fragment shading rate: 1 = normal, 2 = 2x2, 4 = 4x4. Only takes
	// effect when the device supports VK_KHR_fragment_shading_rate.
	void setShadingRate(uint32_t rate) {
		m_shadingRate = rate;
	}

	// Fixed foveation: peripheral rate 0 = off, 2 = 2x2, 4 = 2x2/4x4 rings.
	void setFoveation(uint32_t rate) {
		m_foveationRate = rate;
	}
	void setFoveationSize(float radius) {
		m_foveationSize = radius;
	}
	void setFoveationRing(float ring) {
		m_foveationRing = ring;
	}
	void setFoveationBlur(bool enabled) {
		m_foveationBlur = enabled;
	}
	void setFoveationDebug(bool enabled) {
		m_foveationDebug = enabled;
	}
	void toggleFoveationDebug();

	SceneState &getSceneState() {
		return m_sceneState;
	}

	bool wantsCaptureMouse() const;
	bool wantsCaptureKeyboard() const;

  private:
	struct CameraUbo {
		glm::mat4 view;
		glm::mat4 proj;
		glm::vec4 viewport;        // width, height, 1/width, 1/height
		glm::vec4 viewportOrigin;  // x, y offset of the view within the target
		glm::vec4 cameraPos;
	};

	static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

	void scanScenes();
	void createFrameBuffers();
	void createOffscreenTargets();
	void updateSortDirty();
	void updateCameraBuffers(const app::PresentFrame &frame);
	void recordFrame(const app::PresentFrame &frame);
	void captureScreenshot();
	void updateStats();
	uint64_t estimateGpuBytes(uint32_t count) const;

  private:
	core::Device *m_device = nullptr;
	gpu::Allocator *m_allocator = nullptr;
	app::IPresentation *m_presentation = nullptr;

	VkExtent2D m_presentationExtent{};
	uint32_t m_presentationImageCount = 0;

	RenderContext m_renderContext;
	SceneState m_sceneState;
	RenderStats m_stats;

	scene::GaussianScene m_scene;
	scene::Camera m_camera;
	std::array<scene::Camera, app::kMaxViews> m_viewCameras;
	glm::vec3 m_sceneUp{ 0.0f, 1.0f, 0.0f };
	app::CameraController m_cameraController;

	BackgroundPass m_backgroundPass;
	RasterizePass m_rasterizePass;
	SortPass m_sortPass;
	TileRasterizer m_tileRasterizer;
	GUIPass m_guiPass;
	FoveationDebugPass m_foveationDebugPass;
	FoveationBlurPass m_foveationBlurPass;
	std::array<gpu::Image, app::kMaxViews> m_offscreen;
	bool m_guiReady = false;

	GpuProfiler m_profiler;
	std::vector<float> m_gpuMs;
	float m_profileSums[GpuSlot::Count] = {};
	uint32_t m_profileFrames = 0;

	std::array<std::array<gpu::Buffer, app::kMaxViews>, MAX_FRAMES_IN_FLIGHT> m_cameraBuffers;

	gpu::Buffer m_readbackBuffer;
	bool m_readbackValid = false;
	uint32_t m_readbackWidth = 0;
	uint32_t m_readbackHeight = 0;

	std::chrono::steady_clock::time_point m_lastFrameTime{};

	// Idle-skip: reuse the previous sort when the view/scene/settings are unchanged.
	glm::mat4 m_lastSortView{ 1.0f };
	bool m_hasSortView = false;
	uint32_t m_sortCooldown = 0;
	int m_prevRenderMode = -1;
	int m_prevShDegree = -1;
	float m_prevSplatScale = -1.0f;
	bool m_prevCull = false;
	float m_prevMaxSplat = -1.0f;
	float m_prevMinOpacity = -1.0f;
	float m_prevMaxWorldScale = -1.0f;
	float m_prevMaxDistance = -1.0f;
	bool m_prevSortEnabled = true;
	bool m_warnedTiledMultiView = false;
	bool m_loggedViewCameras = false;
	float m_worldScale = 1.0f;
	int32_t m_screenshotEye = -1;
	uint32_t m_screenshotEyeCount = 1;
	uint32_t m_shadingRate = 1;
	uint32_t m_foveationRate = 0;
	float m_foveationSize = 0.40f;
	float m_foveationRing = 0.32f;
	bool m_foveationBlur = true;
	bool m_foveationDebug = false;
	std::unique_ptr<Foveation> m_foveation;
};

}        // namespace renderer
