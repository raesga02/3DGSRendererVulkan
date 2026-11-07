#include "Application.hpp"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <stdexcept>

namespace app {

namespace {
float g_scrollDelta = 0.0f;

void scrollCallback(GLFWwindow *, double, double yOffset) {
	g_scrollDelta += static_cast<float>(yOffset);
}
}        // namespace

void Application::run(int argc, char **argv) {
	std::string initialScene;
	for (int i = 1; i < argc; ++i) {
		std::string arg = argv[i];
		if (arg == "--screenshot") {
			m_screenshotMode = true;
		} else if (arg == "--profile") {
			m_profileMode = true;
		} else if (arg == "--tiled") {
			m_tiledMode = true;
		} else if (arg == "--nosort") {
			m_noSort = true;
		} else if (arg == "--noidle") {
			m_noIdle = true;
		} else if (arg == "--stereo-test") {
			m_stereoTest = true;
		} else if (initialScene.empty()) {
			initialScene = arg;
		}
	}
	init(initialScene);
	m_presentation.setStereoTest(m_stereoTest);
	if (m_tiledMode)
		m_renderer.getSceneState().renderMode = renderer::RenderMode::Tiled;
	if (m_noSort)
		m_renderer.getSceneState().sortEnabled = false;
	if (m_noIdle)
		m_renderer.getSceneState().forceSort = true;
	mainLoop();
	shutdown();
}

void Application::init(const std::string &initialScene) {
	initWindow();

	m_context.init(m_window);
	m_device.init(m_context);
	m_allocator.init(m_device.getPhysicalDevice(), m_device.get(), m_context.getInstance());

	m_presentation.init(m_device, static_cast<uint32_t>(m_windowWidth), static_cast<uint32_t>(m_windowHeight));
	m_renderer.init(m_device, m_allocator, m_context, m_presentation, m_window, initialScene);

	double mouseX = 0.0, mouseY = 0.0;
	glfwGetCursorPos(m_window, &mouseX, &mouseY);
	m_lastMouseX = mouseX;
	m_lastMouseY = mouseY;

	m_lastFrameTime = std::chrono::steady_clock::now();
}

void Application::initWindow() {
	if (!glfwInit())
		throw std::runtime_error("Failed to initialize GLFW");

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

	m_window = glfwCreateWindow(m_windowWidth, m_windowHeight, "3DGS Renderer (Vulkan)", nullptr, nullptr);
	if (!m_window)
		throw std::runtime_error("Failed to create GLFW window");

	glfwSetScrollCallback(m_window, scrollCallback);
}

void Application::mainLoop() {
	while (!glfwWindowShouldClose(m_window)) {
		glfwPollEvents();

		auto now = std::chrono::steady_clock::now();
		float dt = std::chrono::duration<float>(now - m_lastFrameTime).count();
		m_lastFrameTime = now;

		updateInput();
		m_renderer.update(m_inputState, dt);
		m_renderer.drawFrame();

		if (m_screenshotMode) {
			if (m_screenshotFrame == 30)
				m_renderer.getSceneState().screenshotRequested = true;
			if (m_screenshotFrame > 33)
				glfwSetWindowShouldClose(m_window, GLFW_TRUE);
			++m_screenshotFrame;
		}

		if (m_profileMode) {
			if (m_profileFrame > 90) {
				m_renderer.logGpuTimings();
				glfwSetWindowShouldClose(m_window, GLFW_TRUE);
			}
			++m_profileFrame;
		}
	}
}

void Application::updateInput() {
	double mouseX = 0.0, mouseY = 0.0;
	glfwGetCursorPos(m_window, &mouseX, &mouseY);

	float dx = static_cast<float>(mouseX - m_lastMouseX);
	float dy = static_cast<float>(mouseY - m_lastMouseY);
	m_lastMouseX = mouseX;
	m_lastMouseY = mouseY;

	bool guiCapturesMouse = m_renderer.wantsCaptureMouse();
	bool guiCapturesKeyboard = m_renderer.wantsCaptureKeyboard();

	m_inputState.mousePosition = {
		static_cast<float>(mouseX) / static_cast<float>(m_windowWidth),
		static_cast<float>(mouseY) / static_cast<float>(m_windowHeight),
	};
	m_inputState.mouseDelta = guiCapturesMouse ? glm::vec2(0.0f) : glm::vec2(dx, dy);
	m_inputState.scrollDelta = guiCapturesMouse ? 0.0f : g_scrollDelta;
	g_scrollDelta = 0.0f;

	m_inputState.mouseDown = !guiCapturesMouse &&
	                         (glfwGetMouseButton(m_window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS);
	m_inputState.rightMouseDown = !guiCapturesMouse &&
	                              (glfwGetMouseButton(m_window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS);

	auto keyPressed = [&](int key) {
		return !guiCapturesKeyboard && glfwGetKey(m_window, key) == GLFW_PRESS;
	};

	m_inputState.keyW = keyPressed(GLFW_KEY_W);
	m_inputState.keyA = keyPressed(GLFW_KEY_A);
	m_inputState.keyS = keyPressed(GLFW_KEY_S);
	m_inputState.keyD = keyPressed(GLFW_KEY_D);
	m_inputState.keyQ = keyPressed(GLFW_KEY_Q);
	m_inputState.keyE = keyPressed(GLFW_KEY_E);
	m_inputState.keyShift = keyPressed(GLFW_KEY_LEFT_SHIFT) || keyPressed(GLFW_KEY_RIGHT_SHIFT);

	bool toggleCamera = keyPressed(GLFW_KEY_TAB);
	m_inputState.toggleCamera = toggleCamera && !m_prevToggleCamera;
	m_prevToggleCamera = toggleCamera;

	bool guiToggle = keyPressed(GLFW_KEY_H);
	if (guiToggle && !m_prevGuiToggle)
		m_renderer.getSceneState().showGUI = !m_renderer.getSceneState().showGUI;
	m_prevGuiToggle = guiToggle;

	bool screenshot = keyPressed(GLFW_KEY_F12);
	if (screenshot && !m_prevScreenshot)
		m_renderer.getSceneState().screenshotRequested = true;
	m_prevScreenshot = screenshot;

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
