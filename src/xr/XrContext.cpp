#include "XrContext.hpp"

#include "utils/Log.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <fmt/format.h>
#include <stdexcept>
#include <vector>

namespace xr {

namespace {

const char *sessionStateName(XrSessionState state) {
	switch (state) {
		case XR_SESSION_STATE_UNKNOWN:
			return "UNKNOWN";
		case XR_SESSION_STATE_IDLE:
			return "IDLE";
		case XR_SESSION_STATE_READY:
			return "READY";
		case XR_SESSION_STATE_SYNCHRONIZED:
			return "SYNCHRONIZED";
		case XR_SESSION_STATE_VISIBLE:
			return "VISIBLE";
		case XR_SESSION_STATE_FOCUSED:
			return "FOCUSED";
		case XR_SESSION_STATE_STOPPING:
			return "STOPPING";
		case XR_SESSION_STATE_LOSS_PENDING:
			return "LOSS_PENDING";
		case XR_SESSION_STATE_EXITING:
			return "EXITING";
		default:
			return "?";
	}
}

}        // namespace

void XrContext::init() {
	utils::logInfo("OpenXR: creating instance");
	createInstance();
	utils::logInfo("OpenXR: choosing system");
	createSystem();
	utils::logInfo("OpenXR: creating Vulkan objects");
	createVulkan();
	utils::logInfo("OpenXR: creating session");
	createSession();
	utils::logInfo("OpenXR: creating reference space");
	createReferenceSpace();

	utils::logInfo("OpenXR ready: {}.{}.{}", XR_VERSION_MAJOR(XR_CURRENT_API_VERSION),
	               XR_VERSION_MINOR(XR_CURRENT_API_VERSION), XR_VERSION_PATCH(XR_CURRENT_API_VERSION));
	utils::logInfo("OpenXR views: {}x{} per eye (recommended)",
	               m_viewConfigs[0].recommendedImageRectWidth, m_viewConfigs[0].recommendedImageRectHeight);
}

void XrContext::createInstance() {
	std::vector<const char *> extensions = {
		XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME,
	};

	XrInstanceCreateInfo createInfo{XR_TYPE_INSTANCE_CREATE_INFO};
	createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
	createInfo.enabledExtensionNames = extensions.data();
	createInfo.applicationInfo.apiVersion = XR_API_VERSION_1_0;
	std::strncpy(createInfo.applicationInfo.applicationName, "3DGS Renderer (Vulkan)", XR_MAX_APPLICATION_NAME_SIZE - 1);
	std::strncpy(createInfo.applicationInfo.engineName, "3DGSRendererVulkan", XR_MAX_ENGINE_NAME_SIZE - 1);
	createInfo.applicationInfo.applicationVersion = 1;
	createInfo.applicationInfo.engineVersion = 1;

	check(xrCreateInstance(&createInfo, &m_instance), "xrCreateInstance");

	loadProc("xrCreateVulkanInstanceKHR", m_pfnCreateVulkanInstance);
	loadProc("xrCreateVulkanDeviceKHR", m_pfnCreateVulkanDevice);
	loadProc("xrGetVulkanGraphicsDevice2KHR", m_pfnGetVulkanDevice);
	loadProc("xrGetVulkanGraphicsRequirements2KHR", m_pfnGetVulkanRequirements);
}

void XrContext::createSystem() {
	XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};
	systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
	check(xrGetSystem(m_instance, &systemInfo, &m_systemId), "xrGetSystem");

	uint32_t viewCount = 0;
	check(xrEnumerateViewConfigurationViews(m_instance, m_systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
	                                        0, &viewCount, nullptr),
	      "xrEnumerateViewConfigurationViews");
	if (viewCount != kViewCount)
		throw std::runtime_error(fmt::format("Expected {} views, runtime reported {}", kViewCount, viewCount));

	for (auto &view : m_viewConfigs)
		view.type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
	check(xrEnumerateViewConfigurationViews(m_instance, m_systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
	                                        viewCount, &viewCount, m_viewConfigs.data()),
	      "xrEnumerateViewConfigurationViews");
}

void XrContext::createVulkan() {
	XrGraphicsRequirementsVulkanKHR requirements{XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN_KHR};
	check(m_pfnGetVulkanRequirements(m_instance, m_systemId, &requirements), "xrGetVulkanGraphicsRequirements2KHR");

	const uint64_t minVersion = requirements.minApiVersionSupported;
	const uint64_t maxVersion = requirements.maxApiVersionSupported;
	utils::logInfo("OpenXR Vulkan API range (raw): 0x{:x} - 0x{:x}", minVersion, maxVersion);

	// Monado/WiVRn reports XR-encoded versions here, while the Vulkan spec wants
	// VK_MAKE_VERSION values. Only clamp when the value is clearly Vulkan-style;
	// otherwise request 1.3 and verify the device features below.
	uint32_t apiVersion = VK_MAKE_API_VERSION(0, 1, 3, 0);
	if (maxVersion <= 0xFFFFFFFFull && maxVersion >= VK_MAKE_API_VERSION(0, 1, 0, 0) && maxVersion < apiVersion)
		apiVersion = static_cast<uint32_t>(maxVersion);
	utils::logInfo("OpenXR Vulkan API version: {}.{}.{}", VK_API_VERSION_MAJOR(apiVersion),
	               VK_API_VERSION_MINOR(apiVersion), VK_API_VERSION_PATCH(apiVersion));

	VkApplicationInfo appInfo{VK_STRUCTURE_TYPE_APPLICATION_INFO};
	appInfo.pApplicationName = "3DGS Renderer (Vulkan)";
	appInfo.applicationVersion = 1;
	appInfo.pEngineName = "3DGSRendererVulkan";
	appInfo.engineVersion = 1;
	appInfo.apiVersion = apiVersion;

	VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
	instanceInfo.pApplicationInfo = &appInfo;

	XrVulkanInstanceCreateInfoKHR xrInstanceInfo{XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR};
	xrInstanceInfo.systemId = m_systemId;
	xrInstanceInfo.pfnGetInstanceProcAddr = &vkGetInstanceProcAddr;
	xrInstanceInfo.vulkanCreateInfo = &instanceInfo;

	VkResult vkResult = VK_SUCCESS;
	check(m_pfnCreateVulkanInstance(m_instance, &xrInstanceInfo, &m_vkInstance, &vkResult), "xrCreateVulkanInstanceKHR");
	if (vkResult != VK_SUCCESS)
		throw std::runtime_error(fmt::format("vkCreateInstance failed ({})", static_cast<int>(vkResult)));

	XrVulkanGraphicsDeviceGetInfoKHR deviceInfo{XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR};
	deviceInfo.systemId = m_systemId;
	deviceInfo.vulkanInstance = m_vkInstance;
	check(m_pfnGetVulkanDevice(m_instance, &deviceInfo, &m_vkPhysicalDevice), "xrGetVulkanGraphicsDevice2KHR");

	VkPhysicalDeviceProperties properties{};
	vkGetPhysicalDeviceProperties(m_vkPhysicalDevice, &properties);
	utils::logInfo("OpenXR Vulkan device: {} ({}.{}.{})", properties.deviceName,
	               VK_API_VERSION_MAJOR(properties.apiVersion), VK_API_VERSION_MINOR(properties.apiVersion),
	               VK_API_VERSION_PATCH(properties.apiVersion));

	// Find a queue family that can do graphics and compute (the renderer uses
	// both the instanced graphics path and the compute radix sort).
	uint32_t queueFamilyCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(m_vkPhysicalDevice, &queueFamilyCount, nullptr);
	std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
	vkGetPhysicalDeviceQueueFamilyProperties(m_vkPhysicalDevice, &queueFamilyCount, queueFamilies.data());

	m_queueFamilyIndex = UINT32_MAX;
	for (uint32_t i = 0; i < queueFamilyCount; ++i) {
		const VkQueueFlags flags = queueFamilies[i].queueFlags;
		if ((flags & VK_QUEUE_GRAPHICS_BIT) && (flags & VK_QUEUE_COMPUTE_BIT)) {
			m_queueFamilyIndex = i;
			break;
		}
	}
	if (m_queueFamilyIndex == UINT32_MAX) {
		for (uint32_t i = 0; i < queueFamilyCount; ++i) {
			if ((queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
				m_queueFamilyIndex = i;
				break;
			}
		}
	}
	if (m_queueFamilyIndex == UINT32_MAX)
		throw std::runtime_error("No graphics queue family on the OpenXR device");

	// The renderer relies on dynamic rendering (core 1.3).
	VkPhysicalDeviceVulkan13Features supported13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
	VkPhysicalDeviceFeatures2 supportedFeatures{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
	supportedFeatures.pNext = &supported13;
	vkGetPhysicalDeviceFeatures2(m_vkPhysicalDevice, &supportedFeatures);
	if (!supported13.dynamicRendering)
		throw std::runtime_error("OpenXR device does not support dynamic rendering");

	VkPhysicalDeviceVulkan13Features enabled13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
	enabled13.dynamicRendering = VK_TRUE;
	enabled13.synchronization2 = supported13.synchronization2 ? VK_TRUE : VK_FALSE;

	// Optional VK_KHR_fragment_shading_rate (used by the VR foveation path).
	uint32_t deviceExtensionCount = 0;
	vkEnumerateDeviceExtensionProperties(m_vkPhysicalDevice, nullptr, &deviceExtensionCount, nullptr);
	std::vector<VkExtensionProperties> deviceExtensions(deviceExtensionCount);
	vkEnumerateDeviceExtensionProperties(m_vkPhysicalDevice, nullptr, &deviceExtensionCount, deviceExtensions.data());
	bool hasFragmentShadingRateExt = false;
	for (const auto &ext : deviceExtensions) {
		if (std::strcmp(ext.extensionName, VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME) == 0) {
			hasFragmentShadingRateExt = true;
			break;
		}
	}

	VkPhysicalDeviceFragmentShadingRateFeaturesKHR vrsSupported{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_FEATURES_KHR};
	VkPhysicalDeviceFeatures2 vrsFeatureQuery{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
		.pNext = &vrsSupported};
	vkGetPhysicalDeviceFeatures2(m_vkPhysicalDevice, &vrsFeatureQuery);

	m_fragmentShadingRateSupported =
	    hasFragmentShadingRateExt && vrsSupported.pipelineFragmentShadingRate;

	VkPhysicalDeviceFragmentShadingRateFeaturesKHR vrsFeatures{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_FEATURES_KHR,
		.pipelineFragmentShadingRate = m_fragmentShadingRateSupported ? VK_TRUE : VK_FALSE,
		.attachmentFragmentShadingRate = m_fragmentShadingRateSupported && vrsSupported.attachmentFragmentShadingRate ? VK_TRUE : VK_FALSE,
	};
	if (m_fragmentShadingRateSupported)
		enabled13.pNext = &vrsFeatures;

	utils::logInfo("Vulkan fragment shading rate: {}", m_fragmentShadingRateSupported ? "supported" : "unsupported");

	float queuePriority = 1.0f;
	VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
	queueInfo.queueFamilyIndex = m_queueFamilyIndex;
	queueInfo.queueCount = 1;
	queueInfo.pQueuePriorities = &queuePriority;

	const char *enabledDeviceExtensions[] = { VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME };

	VkDeviceCreateInfo deviceCreateInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
	deviceCreateInfo.pNext = &enabled13;
	deviceCreateInfo.queueCreateInfoCount = 1;
	deviceCreateInfo.pQueueCreateInfos = &queueInfo;
	if (m_fragmentShadingRateSupported) {
		deviceCreateInfo.enabledExtensionCount = 1;
		deviceCreateInfo.ppEnabledExtensionNames = enabledDeviceExtensions;
	}

	XrVulkanDeviceCreateInfoKHR xrDeviceInfo{XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR};
	xrDeviceInfo.systemId = m_systemId;
	xrDeviceInfo.pfnGetInstanceProcAddr = &vkGetInstanceProcAddr;
	xrDeviceInfo.vulkanPhysicalDevice = m_vkPhysicalDevice;
	xrDeviceInfo.vulkanCreateInfo = &deviceCreateInfo;

	check(m_pfnCreateVulkanDevice(m_instance, &xrDeviceInfo, &m_vkDevice, &vkResult), "xrCreateVulkanDeviceKHR");
	if (vkResult != VK_SUCCESS)
		throw std::runtime_error(fmt::format("vkCreateDevice failed ({})", static_cast<int>(vkResult)));

	vkGetDeviceQueue(m_vkDevice, m_queueFamilyIndex, 0, &m_vkQueue);
}

void XrContext::createSession() {
	XrGraphicsBindingVulkan2KHR binding{XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR};
	binding.instance = m_vkInstance;
	binding.physicalDevice = m_vkPhysicalDevice;
	binding.device = m_vkDevice;
	binding.queueFamilyIndex = m_queueFamilyIndex;
	binding.queueIndex = 0;

	XrSessionCreateInfo sessionInfo{XR_TYPE_SESSION_CREATE_INFO};
	sessionInfo.next = &binding;
	sessionInfo.systemId = m_systemId;
	check(xrCreateSession(m_instance, &sessionInfo, &m_session), "xrCreateSession");

	createActions();
}

void XrContext::createActions() {
	XrActionSetCreateInfo setInfo{XR_TYPE_ACTION_SET_CREATE_INFO};
	std::strncpy(setInfo.actionSetName, "app_actions", sizeof(setInfo.actionSetName) - 1);
	std::strncpy(setInfo.localizedActionSetName, "Application Actions", sizeof(setInfo.localizedActionSetName) - 1);
	check(xrCreateActionSet(m_instance, &setInfo, &m_actionSet), "xrCreateActionSet");

	auto createBooleanAction = [&](const char *name, const char *localized, XrAction &action) {
		XrActionCreateInfo info{XR_TYPE_ACTION_CREATE_INFO};
		info.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
		std::strncpy(info.actionName, name, sizeof(info.actionName) - 1);
		std::strncpy(info.localizedActionName, localized, sizeof(info.localizedActionName) - 1);
		check(xrCreateAction(m_actionSet, &info, &action), "xrCreateAction");
	};

	createBooleanAction("toggle_foveation_debug", "Toggle foveation debug", m_toggleDebugAction);
	createBooleanAction("take_screenshot", "Take screenshot", m_screenshotAction);

	XrPath aPath = XR_NULL_PATH;
	XrPath bPath = XR_NULL_PATH;
	check(xrStringToPath(m_instance, "/user/hand/right/input/a/click", &aPath), "xrStringToPath");
	check(xrStringToPath(m_instance, "/user/hand/right/input/b/click", &bPath), "xrStringToPath");

	XrActionSuggestedBinding bindings[] = {
		{ m_toggleDebugAction, aPath },
		{ m_screenshotAction, bPath },
	};

	auto suggestProfile = [&](const char *profile) {
		XrPath profilePath = XR_NULL_PATH;
		if (XR_FAILED(xrStringToPath(m_instance, profile, &profilePath)))
			return;
		XrInteractionProfileSuggestedBinding suggested{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
		suggested.interactionProfile = profilePath;
		suggested.suggestedBindings = bindings;
		suggested.countSuggestedBindings = 2;
		const XrResult result = xrSuggestInteractionProfileBindings(m_instance, &suggested);
		if (XR_FAILED(result))
			utils::logWarn("Could not suggest bindings for {} ({})", profile, static_cast<int>(result));
	};
	suggestProfile("/interaction_profiles/oculus/touch_controller");

	XrSessionActionSetsAttachInfo attach{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
	attach.countActionSets = 1;
	attach.actionSets = &m_actionSet;
	check(xrAttachSessionActionSets(m_session, &attach), "xrAttachSessionActionSets");

	utils::logInfo("OpenXR actions: right A toggles foveation debug, right B takes a screenshot");
}

XrContext::InputActions XrContext::pollActions() {
	InputActions result;
	if (!m_sessionRunning || m_actionSet == XR_NULL_HANDLE)
		return result;

	XrActiveActionSet activeActionSet{ m_actionSet, XR_NULL_PATH };
	XrActionsSyncInfo syncInfo{XR_TYPE_ACTIONS_SYNC_INFO};
	syncInfo.countActiveActionSets = 1;
	syncInfo.activeActionSets = &activeActionSet;
	if (XR_FAILED(xrSyncActions(m_session, &syncInfo)))
		return result;

	auto pressed = [&](XrAction action, bool &previous) {
		XrActionStateGetInfo getInfo{XR_TYPE_ACTION_STATE_GET_INFO};
		getInfo.action = action;
		XrActionStateBoolean state{XR_TYPE_ACTION_STATE_BOOLEAN};
		if (XR_FAILED(xrGetActionStateBoolean(m_session, &getInfo, &state)))
			return false;
		const bool now = state.isActive == XR_TRUE && state.currentState == XR_TRUE;
		const bool edge = now && !previous;
		previous = now;
		return edge;
	};

	result.toggleFoveationDebug = pressed(m_toggleDebugAction, m_prevToggleDebug);
	result.takeScreenshot = pressed(m_screenshotAction, m_prevScreenshot);
	return result;
}

void XrContext::createReferenceSpace() {
	XrReferenceSpaceCreateInfo spaceInfo{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
	spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
	spaceInfo.poseInReferenceSpace.orientation.w = 1.0f;
	check(xrCreateReferenceSpace(m_session, &spaceInfo, &m_localSpace), "xrCreateReferenceSpace");
}

bool XrContext::pollEvents() {
	while (true) {
		XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
		const XrResult result = xrPollEvent(m_instance, &event);
		if (result == XR_EVENT_UNAVAILABLE)
			break;
		if (XR_FAILED(result)) {
			utils::logWarn("xrPollEvent failed ({})", static_cast<int>(result));
			break;
		}
		handleEvent(event);
	}
	return !m_quit;
}

void XrContext::handleEvent(const XrEventDataBuffer &event) {
	switch (event.type) {
		case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
			const auto &changed = reinterpret_cast<const XrEventDataSessionStateChanged &>(event);
			m_sessionState = changed.state;
			utils::logInfo("OpenXR session state: {}", sessionStateName(m_sessionState));

			switch (m_sessionState) {
				case XR_SESSION_STATE_READY: {
					if (!m_sessionRunning) {
						XrSessionBeginInfo beginInfo{XR_TYPE_SESSION_BEGIN_INFO};
						beginInfo.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
						check(xrBeginSession(m_session, &beginInfo), "xrBeginSession");
						m_sessionRunning = true;
					}
					break;
				}
				case XR_SESSION_STATE_STOPPING: {
					if (m_sessionRunning) {
						check(xrEndSession(m_session), "xrEndSession");
						m_sessionRunning = false;
					}
					break;
				}
				case XR_SESSION_STATE_EXITING:
				case XR_SESSION_STATE_LOSS_PENDING: {
					m_quit = true;
					break;
				}
				default:
					break;
			}
			break;
		}
		case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING: {
			m_quit = true;
			break;
		}
		default:
			break;
	}
}

bool XrContext::waitFrame(XrFrameState &out) {
	if (!m_sessionRunning)
		return false;

	out = XrFrameState{XR_TYPE_FRAME_STATE};
	XrFrameWaitInfo waitInfo{XR_TYPE_FRAME_WAIT_INFO};
	XrResult result = xrWaitFrame(m_session, &waitInfo, &out);
	if (XR_FAILED(result)) {
		utils::logWarn("xrWaitFrame failed ({})", static_cast<int>(result));
		return false;
	}

	m_displayTime = out.predictedDisplayTime;

	XrFrameBeginInfo beginInfo{XR_TYPE_FRAME_BEGIN_INFO};
	result = xrBeginFrame(m_session, &beginInfo);
	if (XR_FAILED(result)) {
		utils::logWarn("xrBeginFrame failed ({})", static_cast<int>(result));
		return false;
	}
	m_frameBegun = true;
	return true;
}

void XrContext::endFrame(const XrCompositionLayerBaseHeader *layer) {
	if (!m_frameBegun)
		return;

	XrFrameEndInfo endInfo{XR_TYPE_FRAME_END_INFO};
	endInfo.displayTime = m_displayTime;
	endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
	endInfo.layerCount = layer ? 1 : 0;
	endInfo.layers = layer ? &layer : nullptr;

	const XrResult result = xrEndFrame(m_session, &endInfo);
	if (XR_FAILED(result))
		utils::logWarn("xrEndFrame failed ({})", static_cast<int>(result));
	m_frameBegun = false;
}

bool XrContext::locateViews(XrTime displayTime, std::array<XrView, kViewCount> &views) const {
	for (auto &view : views)
		view = XrView{XR_TYPE_VIEW};

	XrViewLocateInfo locateInfo{XR_TYPE_VIEW_LOCATE_INFO};
	locateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
	locateInfo.displayTime = displayTime;
	locateInfo.space = m_localSpace;

	XrViewState viewState{XR_TYPE_VIEW_STATE};
	uint32_t viewCount = 0;
	const XrResult result = xrLocateViews(m_session, &locateInfo, &viewState, kViewCount, &viewCount, views.data());
	if (XR_FAILED(result) || viewCount != kViewCount) {
		utils::logWarn("xrLocateViews failed ({})", static_cast<int>(result));
		return false;
	}
	return true;
}

void XrContext::shutdown() {
	// The runtime drives session shutdown through the STOPPING event. On app
	// exit we just destroy the session; calling xrEndSession here is not valid
	// for every session state.
	m_sessionRunning = false;

	if (m_actionSet)
		xrDestroyActionSet(m_actionSet);
	m_actionSet = XR_NULL_HANDLE;

	if (m_localSpace)
		xrDestroySpace(m_localSpace);
	m_localSpace = XR_NULL_HANDLE;

	if (m_session)
		xrDestroySession(m_session);
	m_session = XR_NULL_HANDLE;

	if (m_instance)
		xrDestroyInstance(m_instance);
	m_instance = XR_NULL_HANDLE;

	if (m_vkDevice) {
		vkDeviceWaitIdle(m_vkDevice);
		vkDestroyDevice(m_vkDevice, nullptr);
	}
	m_vkDevice = VK_NULL_HANDLE;

	if (m_vkInstance)
		vkDestroyInstance(m_vkInstance, nullptr);
	m_vkInstance = VK_NULL_HANDLE;
}

void XrContext::check(XrResult result, const char *what) const {
	if (XR_SUCCEEDED(result))
		return;

	char message[XR_MAX_RESULT_STRING_SIZE] = {};
	if (m_instance)
		xrResultToString(m_instance, result, message);
	throw std::runtime_error(fmt::format("{} failed: {} ({})", what, static_cast<int>(result), message));
}

}        // namespace xr
