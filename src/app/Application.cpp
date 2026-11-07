#include "Application.hpp"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "utils/Log.hpp"

#include <algorithm>
#include <cstdlib>
#include <csignal>
#include <stdexcept>
#include <thread>

namespace app {

namespace {
volatile std::sig_atomic_t g_vrInterrupted = 0;

void vrSignalHandler(int) {
	g_vrInterrupted = 1;
}
}        // namespace

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
		} else if (arg == "--vr") {
			m_vrMode = true;
		} else if (arg == "--frames" && i + 1 < argc) {
			m_maxVrFrames = std::atoi(argv[++i]);
		} else if (arg == "--vr-world-scale" && i + 1 < argc) {
			m_vrWorldScale = std::strtof(argv[++i], nullptr);
		} else if (arg == "--no-cull") {
			m_noCull = true;
		} else if (arg == "--max-splat-fraction" && i + 1 < argc) {
			m_maxSplatFraction = std::strtof(argv[++i], nullptr);
		} else if (arg == "--min-opacity" && i + 1 < argc) {
			m_minOpacity = std::strtof(argv[++i], nullptr);
		} else if (arg == "--max-world-splat" && i + 1 < argc) {
			m_maxWorldScale = std::strtof(argv[++i], nullptr);
		} else if (arg == "--max-distance" && i + 1 < argc) {
			m_maxDistanceFactor = std::strtof(argv[++i], nullptr);
		} else if (arg == "--screenshot-eye" && i + 1 < argc) {
			m_screenshotEye = static_cast<uint32_t>(std::max(0, std::atoi(argv[++i])));
		} else if (arg == "--vr-shading-rate" && i + 1 < argc) {
			const int rate = std::atoi(argv[++i]);
			m_shadingRate = (rate == 2 || rate == 4) ? static_cast<uint32_t>(rate) : 1;
		} else if (arg == "--vr-foveation" && i + 1 < argc) {
			const int rate = std::atoi(argv[++i]);
			m_foveationRate = (rate == 2 || rate == 4) ? static_cast<uint32_t>(rate) : 0;
		} else if (initialScene.empty()) {
			initialScene = arg;
		}
	}

	if (m_vrMode) {
#ifndef HAS_OPENXR
		throw std::runtime_error("--vr requires a build with OpenXR support (see ENABLE_OPENXR/OPENXR_SDK_ROOT)");
#else
		runVr(initialScene);
		return;
#endif
	}

	m_renderer.setShadingRate(m_shadingRate);
	m_renderer.setFoveation(m_foveationRate);
	init(initialScene);
	m_presentation.setStereoTest(m_stereoTest);
	applyRendererOverrides();
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
	m_renderer.init(m_device, m_allocator, &m_context, m_presentation, m_window, initialScene);

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

void Application::runVr(const std::string &initialScene) {
#ifdef HAS_OPENXR
	m_xrContext = std::make_unique<xr::XrContext>();
	m_xrContext->init();

	m_device.initFromHandles(m_xrContext->physicalDevice(), m_xrContext->device(),
	                         m_xrContext->queue(), m_xrContext->queueFamilyIndex());
	m_device.setFragmentShadingRateSupported(m_xrContext->fragmentShadingRateSupported());
	m_allocator.init(m_xrContext->physicalDevice(), m_xrContext->device(),
	                 m_xrContext->vulkanInstance());

	m_xrPresentation = std::make_unique<xr::XrPresentation>();
	m_xrPresentation->init(m_device, *m_xrContext);

	// No window/GUI in VR; keep the instanced rasterizer (the tile path is
	// single-view for now).
	m_renderer.setShadingRate(m_shadingRate);
	m_renderer.setFoveation(m_foveationRate);
	m_renderer.init(m_device, m_allocator, nullptr, *m_xrPresentation, nullptr, initialScene);
	m_renderer.getSceneState().showGUI = false;
	m_renderer.getSceneState().renderMode = renderer::RenderMode::Instanced;
	m_renderer.setWorldScale(m_vrWorldScale);
	applyRendererOverrides();
	if (m_noSort)
		m_renderer.getSceneState().sortEnabled = false;

	m_lastFrameTime = std::chrono::steady_clock::now();

	g_vrInterrupted = 0;
	std::signal(SIGINT, vrSignalHandler);
	std::signal(SIGTERM, vrSignalHandler);

	uint64_t renderedFrames = 0;
	while (m_xrContext->pollEvents()) {
		if (g_vrInterrupted || m_xrContext->quitRequested())
			break;

		if (!m_xrContext->sessionRunning()) {
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
			continue;
		}

		const auto now = std::chrono::steady_clock::now();
		const float dt = std::chrono::duration<float>(now - m_lastFrameTime).count();
		m_lastFrameTime = now;

		InputState input;
		m_renderer.update(input, dt);
		m_renderer.drawFrame();

		if (m_xrPresentation->lastFrameRendered()) {
			++renderedFrames;
			if (renderedFrames == 1 || renderedFrames % 300 == 0)
				utils::logInfo("VR frames rendered: {}", renderedFrames);
		} else {
			// The runtime did not ask for a frame (not visible/focused). Do not
			// busy-spin: that floods the IPC and logs and can freeze the stream.
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
		}

		if (m_screenshotMode) {
			if (renderedFrames == 3)
				m_renderer.getSceneState().screenshotRequested = true;
			if (renderedFrames >= 6)
				break;
		}

		if (m_maxVrFrames > 0 && renderedFrames >= static_cast<uint64_t>(m_maxVrFrames))
			break;
	}

	if (renderedFrames > 0)
		m_renderer.logGpuTimings();

	std::signal(SIGINT, SIG_DFL);
	std::signal(SIGTERM, SIG_DFL);

	shutdownVr();
#else
	(void) initialScene;
	throw std::runtime_error("--vr requires a build with OpenXR support");
#endif
}

void Application::shutdownVr() {
#ifdef HAS_OPENXR
	m_renderer.shutdown();
	if (m_xrPresentation) {
		m_xrPresentation->shutdown();
		m_xrPresentation.reset();
	}
	m_allocator.shutdown();
	m_device.shutdown();        // releases the transient pool; the device belongs to XrContext
	if (m_xrContext) {
		m_xrContext->shutdown();
		m_xrContext.reset();
	}
#endif
}

void Application::applyRendererOverrides() {
	auto &state = m_renderer.getSceneState();
	m_renderer.setScreenshotEye(m_screenshotEye);

	if (m_noCull) {
		state.cullLargeSplats = false;
		state.minOpacity = 0.0f;
		state.maxWorldScaleFraction = 0.0f;
		state.maxDistanceFactor = 0.0f;
	} else {
		if (m_maxSplatFraction > 0.0f) {
			state.cullLargeSplats = true;
			state.maxSplatFraction = m_maxSplatFraction;
		} else if (m_vrMode) {
			// Close-up VR content produces very large legitimate splats; the
			// desktop default (0.20) removes too many of them.
			state.cullLargeSplats = true;
			state.maxSplatFraction = 0.5f;
		}

		state.minOpacity = std::max(0.0f, m_minOpacity);
		state.maxWorldScaleFraction = std::max(0.0f, m_maxWorldScale);
		state.maxDistanceFactor = std::max(0.0f, m_maxDistanceFactor);
	}

	if (m_vrMode)
		utils::logInfo("Floater cull: {} (screen {:.2f}, min opacity {:.2f}, world size {:.3f}, distance {:.2f})",
		               state.cullLargeSplats ? "on" : "off", state.maxSplatFraction, state.minOpacity,
		               state.maxWorldScaleFraction, state.maxDistanceFactor);
}

}        // namespace app
