#pragma once

#include "DesktopPresentation.hpp"
#include "InputState.hpp"
#include "core/Device.hpp"
#include "core/VulkanContext.hpp"
#include "gpu/Allocator.hpp"
#include "renderer/Renderer.hpp"

struct GLFWwindow;

namespace app {

// Owns the platform window, the shared Vulkan services and the main loop.
class Application {
  public:
	void run();

  private:
	int m_windowWidth = 1600;
	int m_windowHeight = 900;

	void init();
	void initWindow();
	void mainLoop();
	void updateInput();
	void shutdown();

  private:
	GLFWwindow *m_window = nullptr;

	core::VulkanContext m_context;
	core::Device m_device;
	gpu::Allocator m_allocator;
	DesktopPresentation m_presentation;
	renderer::Renderer m_renderer;

	InputState m_inputState;
	bool m_prevGuiToggle = false;
};

}        // namespace app
