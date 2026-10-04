#pragma once

#include "RenderContext.hpp"
#include "RenderStats.hpp"
#include "SceneState.hpp"
#include "renderer/passes/BackgroundPass.hpp"
#include "renderer/passes/GUIPass.hpp"

#include <chrono>

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
	void init(core::Device &device, gpu::Allocator &allocator, core::VulkanContext &context, app::IPresentation &presentation, GLFWwindow *window);
	void shutdown();

	void drawFrame();

	SceneState &getSceneState() {
		return m_sceneState;
	}

	bool wantsCaptureMouse() const;
	bool wantsCaptureKeyboard() const;

  private:
	void recordFrame(const app::PresentFrame &frame);
	void updateStats();

  private:
	core::Device *m_device = nullptr;
	gpu::Allocator *m_allocator = nullptr;
	app::IPresentation *m_presentation = nullptr;

	RenderContext m_renderContext;
	SceneState m_sceneState;
	RenderStats m_stats;

	BackgroundPass m_backgroundPass;
	GUIPass m_guiPass;

	std::chrono::steady_clock::time_point m_lastFrameTime{};
};

}        // namespace renderer
