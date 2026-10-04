#include "Application.hpp"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <stdexcept>

namespace app {

void Application::run() {
	init();
	mainLoop();
	shutdown();
}

void Application::init() {
	initWindow();

	m_context.init(m_window);
	m_device.init(m_context);
	m_allocator.init(m_device.getPhysicalDevice(), m_device.get(), m_context.getInstance());

	m_presentation.init(m_device, static_cast<uint32_t>(m_windowWidth), static_cast<uint32_t>(m_windowHeight));
	m_renderer.init(m_device, m_allocator, m_context, m_presentation, m_window);
}

void Application::initWindow() {
	if (!glfwInit())
		throw std::runtime_error("Failed to initialize GLFW");

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

	m_window = glfwCreateWindow(m_windowWidth, m_windowHeight, "3DGS Renderer (Vulkan)", nullptr, nullptr);
	if (!m_window)
		throw std::runtime_error("Failed to create GLFW window");
}

void Application::mainLoop() {
	while (!glfwWindowShouldClose(m_window)) {
		glfwPollEvents();
		updateInput();
		m_renderer.drawFrame();
	}
}

void Application::updateInput() {
	double mouseX = 0.0, mouseY = 0.0;
	glfwGetCursorPos(m_window, &mouseX, &mouseY);
	m_inputState.mousePosition = {
		static_cast<float>(mouseX) / static_cast<float>(m_windowWidth),
		static_cast<float>(mouseY) / static_cast<float>(m_windowHeight),
	};

	bool guiCapturesMouse = m_renderer.wantsCaptureMouse();
	m_inputState.mouseDown = !guiCapturesMouse &&
	                         (glfwGetMouseButton(m_window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS);

	bool guiCapturesKeyboard = m_renderer.wantsCaptureKeyboard();

	bool keyH = !guiCapturesKeyboard && (glfwGetKey(m_window, GLFW_KEY_H) == GLFW_PRESS);
	if (keyH && !m_prevGuiToggle)
		m_renderer.getSceneState().showGUI = !m_renderer.getSceneState().showGUI;
	m_prevGuiToggle = keyH;

	if (glfwGetKey(m_window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
		glfwSetWindowShouldClose(m_window, GLFW_TRUE);
}

void Application::shutdown() {
	m_renderer.shutdown();
	m_presentation.shutdown();
	m_allocator.shutdown();
	m_device.shutdown();
	m_context.shutdown();

	glfwDestroyWindow(m_window);
	glfwTerminate();
}

}        // namespace app
