#pragma once

#include <array>
#include <cstdint>
#include <stdexcept>

#include <vulkan/vulkan.h>
#define XR_USE_GRAPHICS_API_VULKAN
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

namespace xr {

// Owns the OpenXR instance/system/session plus the Vulkan objects the runtime
// asks us to use. The renderer only ever sees the resulting VkDevice and the
// per-eye cameras; all OpenXR details stay behind this class.
class XrContext {
  public:
	static constexpr uint32_t kViewCount = 2;

	void init();
	void shutdown();

	// Polls OpenXR events. Returns false when the application should exit.
	bool pollEvents();

	bool sessionRunning() const {
		return m_sessionRunning;
	}
	bool quitRequested() const {
		return m_quit;
	}

	// Blocks until the runtime is ready for a frame and calls xrBeginFrame.
	// The caller must call endFrame() afterwards.
	bool waitFrame(XrFrameState &out);
	void endFrame(const XrCompositionLayerBaseHeader *layer);

	bool locateViews(XrTime displayTime, std::array<XrView, kViewCount> &views) const;

	XrInstance instance() const {
		return m_instance;
	}
	XrSystemId systemId() const {
		return m_systemId;
	}
	XrSession session() const {
		return m_session;
	}
	XrSpace localSpace() const {
		return m_localSpace;
	}
	const XrViewConfigurationView &viewConfig(uint32_t view) const {
		return m_viewConfigs[view];
	}

	VkInstance vulkanInstance() const {
		return m_vkInstance;
	}
	VkPhysicalDevice physicalDevice() const {
		return m_vkPhysicalDevice;
	}
	VkDevice device() const {
		return m_vkDevice;
	}
	VkQueue queue() const {
		return m_vkQueue;
	}
	uint32_t queueFamilyIndex() const {
		return m_queueFamilyIndex;
	}
	uint32_t queueIndex() const {
		return 0;
	}

  private:
	void createInstance();
	void createSystem();
	void createVulkan();
	void createSession();
	void createReferenceSpace();

	void handleEvent(const XrEventDataBuffer &event);
	void check(XrResult result, const char *what) const;

	template <typename T>
	void loadProc(const char *name, T &fn) {
		PFN_xrVoidFunction raw = nullptr;
		check(xrGetInstanceProcAddr(m_instance, name, &raw), name);
		fn = reinterpret_cast<T>(raw);
		if (!fn)
			throw std::runtime_error(name);
	}

  private:
	XrInstance m_instance = XR_NULL_HANDLE;
	XrSystemId m_systemId = XR_NULL_SYSTEM_ID;
	XrSession m_session = XR_NULL_HANDLE;
	XrSpace m_localSpace = XR_NULL_HANDLE;
	XrSessionState m_sessionState = XR_SESSION_STATE_UNKNOWN;
	bool m_sessionRunning = false;
	bool m_frameBegun = false;
	bool m_quit = false;
	XrTime m_displayTime = 0;

	std::array<XrViewConfigurationView, kViewCount> m_viewConfigs{};

	VkInstance m_vkInstance = VK_NULL_HANDLE;
	VkPhysicalDevice m_vkPhysicalDevice = VK_NULL_HANDLE;
	VkDevice m_vkDevice = VK_NULL_HANDLE;
	VkQueue m_vkQueue = VK_NULL_HANDLE;
	uint32_t m_queueFamilyIndex = 0;

	PFN_xrCreateVulkanInstanceKHR m_pfnCreateVulkanInstance = nullptr;
	PFN_xrCreateVulkanDeviceKHR m_pfnCreateVulkanDevice = nullptr;
	PFN_xrGetVulkanGraphicsDevice2KHR m_pfnGetVulkanDevice = nullptr;
	PFN_xrGetVulkanGraphicsRequirements2KHR m_pfnGetVulkanRequirements = nullptr;
};

}        // namespace xr
