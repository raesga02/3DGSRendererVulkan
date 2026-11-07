#pragma once

#include "DesktopPresentation.hpp"
#include "InputState.hpp"
#include "core/Device.hpp"
#include "core/VulkanContext.hpp"
#include "gpu/Allocator.hpp"
#include "renderer/Renderer.hpp"

#ifdef HAS_OPENXR
#include "xr/XrContext.hpp"
#include "xr/XrPresentation.hpp"
#include <memory>
#endif

#include <chrono>
#include <string>

struct GLFWwindow;

namespace app {

// Owns the platform window, the shared Vulkan services and the main loop.
class Application {
  public:
	void run(int argc, char **argv);

  private:
	void init(const std::string &initialScene);
	void initWindow();
	void mainLoop();
	void updateInput();
	void runVr(const std::string &initialScene);
	void shutdown();
	void shutdownVr();

  private:
	int m_windowWidth = 1600;
	int m_windowHeight = 900;

	bool m_screenshotMode = false;
	int m_screenshotFrame = 0;

	bool m_profileMode = false;
	int m_profileFrame = 0;

	bool m_tiledMode = false;
	bool m_noSort = false;
	bool m_noIdle = false;
	bool m_stereoTest = false;
	bool m_vrMode = false;
	int m_maxVrFrames = -1;
	float m_vrWorldScale = 1.4f;

  private:
	GLFWwindow *m_window = nullptr;

	core::VulkanContext m_context;
	core::Device m_device;
	gpu::Allocator m_allocator;
	DesktopPresentation m_presentation;
	renderer::Renderer m_renderer;

#ifdef HAS_OPENXR
	std::unique_ptr<xr::XrContext> m_xrContext;
	std::unique_ptr<xr::XrPresentation> m_xrPresentation;
#endif

	InputState m_inputState;
	double m_lastMouseX = 0.0;
	double m_lastMouseY = 0.0;
	bool m_prevToggleCamera = false;
	bool m_prevGuiToggle = false;
	bool m_prevScreenshot = false;

	std::chrono::steady_clock::time_point m_lastFrameTime{};
};

}        // namespace app
