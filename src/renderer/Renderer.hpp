#pragma once

#include "RenderContext.hpp"
#include "RenderStats.hpp"
#include "SceneState.hpp"
#include "app/CameraController.hpp"
#include "gpu/Buffer.hpp"
#include "renderer/passes/BackgroundPass.hpp"
#include "renderer/passes/GUIPass.hpp"
#include "renderer/passes/RasterizePass.hpp"
#include "scene/Camera.hpp"
#include "scene/GaussianScene.hpp"

#include <array>
#include <chrono>
#include <glm/glm.hpp>
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
	void init(core::Device &device, gpu::Allocator &allocator, core::VulkanContext &context, app::IPresentation &presentation, GLFWwindow *window, const std::string &initialScene = "");
	void shutdown();

	void update(const app::InputState &input, float dt);
	void drawFrame();

	void loadScene(const std::string &plyPath);

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
		glm::vec4 cameraPos;
	};

	static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

	void scanScenes();
	void createFrameBuffers();
	void updateSortedIndices();
	void updateCameraBuffer(uint32_t frameIndex);
	void recordFrame(const app::PresentFrame &frame);
	void captureScreenshot();
	void updateStats();

  private:
	core::Device *m_device = nullptr;
	gpu::Allocator *m_allocator = nullptr;
	app::IPresentation *m_presentation = nullptr;

	RenderContext m_renderContext;
	SceneState m_sceneState;
	RenderStats m_stats;

	scene::GaussianScene m_scene;
	scene::Camera m_camera;
	app::CameraController m_cameraController;

	BackgroundPass m_backgroundPass;
	RasterizePass m_rasterizePass;
	GUIPass m_guiPass;

	std::array<gpu::Buffer, MAX_FRAMES_IN_FLIGHT> m_cameraBuffers;
	std::array<gpu::Buffer, MAX_FRAMES_IN_FLIGHT> m_indexBuffers;

	gpu::Buffer m_readbackBuffer;
	bool m_readbackValid = false;
	uint32_t m_readbackWidth = 0;
	uint32_t m_readbackHeight = 0;

	std::vector<uint64_t> m_sortKeys;
	std::vector<uint32_t> m_sortedIndices;

	std::chrono::steady_clock::time_point m_lastFrameTime{};
};

}        // namespace renderer
