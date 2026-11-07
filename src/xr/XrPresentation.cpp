#include "XrPresentation.hpp"

#include "core/Device.hpp"
#include "utils/Log.hpp"

#include <algorithm>
#include <cmath>
#include <fmt/format.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <iterator>
#include <stdexcept>
#include <vector>

namespace xr {

namespace {

// The runtime recommends a very high per-eye resolution. The presentation
// starts at a reduced scale (0.5) so the first room-scale test is comfortably
// interactive; Application raises it via XrPresentation::setResolutionScale.

glm::mat4 localViewFromPose(const XrPosef &pose) {
	glm::quat orientation(pose.orientation.w, pose.orientation.x, pose.orientation.y, pose.orientation.z);
	const glm::mat4 eyeToLocal =
	    glm::translate(glm::mat4(1.0f), glm::vec3(pose.position.x, pose.position.y, pose.position.z)) *
	    glm::mat4_cast(orientation);
	return glm::inverse(eyeToLocal);
}

// Asymmetric FOV -> Vulkan projection (Y down, [0,1] depth). Mirrors the
// Khronos XrMatrix4x4f_CreateProjectionFov convention.
glm::mat4 projectionFromFov(const XrFovf &fov, float nearZ, float farZ) {
	const float tanLeft = std::tan(fov.angleLeft);
	const float tanRight = std::tan(fov.angleRight);
	const float tanDown = std::tan(fov.angleDown);
	const float tanUp = std::tan(fov.angleUp);

	const float tanWidth = tanRight - tanLeft;
	const float tanHeight = tanDown - tanUp;        // positive Y down for Vulkan

	glm::mat4 projection(0.0f);
	projection[0][0] = 2.0f / tanWidth;
	projection[1][1] = 2.0f / tanHeight;
	// Off-centre terms live in column 2 (row-major Khronos m[8], m[9]).
	projection[2][0] = (tanRight + tanLeft) / tanWidth;
	projection[2][1] = (tanUp + tanDown) / tanHeight;
	projection[2][2] = -farZ / (farZ - nearZ);
	projection[2][3] = -1.0f;
	projection[3][2] = -(farZ * nearZ) / (farZ - nearZ);
	return projection;
}

}        // namespace

void XrPresentation::init(core::Device &device, XrContext &context) {
	m_device = &device;
	m_context = &context;

	m_format = chooseFormat(context.session());

	// Keep both eyes at the same extent; the smaller recommendation wins if the
	// runtime reports slightly different sizes.
	const XrViewConfigurationView &left = context.viewConfig(0);
	const XrViewConfigurationView &right = context.viewConfig(1);
	const uint32_t recommendedWidth = std::min(left.recommendedImageRectWidth, right.recommendedImageRectWidth);
	const uint32_t recommendedHeight = std::min(left.recommendedImageRectHeight, right.recommendedImageRectHeight);

	m_extent.width = std::max(64u, static_cast<uint32_t>(recommendedWidth * m_resolutionScale)) & ~1u;
	m_extent.height = std::max(64u, static_cast<uint32_t>(recommendedHeight * m_resolutionScale)) & ~1u;

	utils::logInfo("OpenXR render resolution: {}x{} per eye (recommended {}x{}, scale {:.2f})",
	               m_extent.width, m_extent.height, recommendedWidth, recommendedHeight, m_resolutionScale);

	for (auto &eye : m_eyes)
		eye.init(context.session(), device.get(), m_extent, m_format);

	createFrameData(device);
}

VkFormat XrPresentation::chooseFormat(XrSession session) const {
	uint32_t formatCount = 0;
	if (xrEnumerateSwapchainFormats(session, 0, &formatCount, nullptr) != XR_SUCCESS)
		throw std::runtime_error("xrEnumerateSwapchainFormats failed");

	std::vector<int64_t> formats(formatCount);
	if (xrEnumerateSwapchainFormats(session, formatCount, &formatCount, formats.data()) != XR_SUCCESS)
		throw std::runtime_error("xrEnumerateSwapchainFormats failed");

	fmt::memory_buffer list;
	for (size_t i = 0; i < formats.size(); ++i) {
		if (i)
			fmt::format_to(std::back_inserter(list), ", ");
		fmt::format_to(std::back_inserter(list), "{}", formats[i]);
	}
	utils::logInfo("OpenXR swapchain formats: [{}]", fmt::to_string(list));

	// Match the desktop target (UNORM) first so colour handling is unchanged.
	const VkFormat preferred[] = {
		VK_FORMAT_B8G8R8A8_UNORM,
		VK_FORMAT_R8G8B8A8_UNORM,
		VK_FORMAT_B8G8R8A8_SRGB,
		VK_FORMAT_R8G8B8A8_SRGB,
		VK_FORMAT_R16G16B16A16_SFLOAT,
	};

	for (VkFormat candidate : preferred) {
		for (int64_t offered : formats) {
			if (offered == static_cast<int64_t>(candidate)) {
				utils::logInfo("Using OpenXR swapchain format {}", static_cast<int>(candidate));
				return candidate;
			}
		}
	}

	throw std::runtime_error("No supported OpenXR swapchain colour format");
}

void XrPresentation::createFrameData(core::Device &device) {
	VkDevice vkDevice = device.get();

	for (FrameData &frame : m_frames) {
		VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
		poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		poolInfo.queueFamilyIndex = device.getGraphicsQueueIndex();
		if (vkCreateCommandPool(vkDevice, &poolInfo, nullptr, &frame.pool) != VK_SUCCESS)
			throw std::runtime_error("Failed to create XR command pool");

		VkCommandBufferAllocateInfo allocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
		allocInfo.commandPool = frame.pool;
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocInfo.commandBufferCount = 1;
		if (vkAllocateCommandBuffers(vkDevice, &allocInfo, &frame.cmd) != VK_SUCCESS)
			throw std::runtime_error("Failed to allocate XR command buffer");

		VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
		fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
		if (vkCreateFence(vkDevice, &fenceInfo, nullptr, &frame.fence) != VK_SUCCESS)
			throw std::runtime_error("Failed to create XR fence");
	}
}

void XrPresentation::destroyFrameData(core::Device &device) {
	VkDevice vkDevice = device.get();
	for (FrameData &frame : m_frames) {
		if (frame.fence)
			vkDestroyFence(vkDevice, frame.fence, nullptr);
		if (frame.pool)
			vkDestroyCommandPool(vkDevice, frame.pool, nullptr);
		frame = FrameData{};
	}
}

void XrPresentation::beginFrame(app::PresentFrame &out) {
	out = app::PresentFrame{};
	m_frameBegun = false;
	m_lastFrameRendered = false;

	if (!m_loggedFirstWait) {
		utils::logInfo("OpenXR waiting for first frame");
		m_loggedFirstWait = true;
	}

	if (!m_context->waitFrame(m_frameState))
		return;

	if (!m_loggedFirstWaitReturn) {
		utils::logInfo("OpenXR first xrWaitFrame returned (shouldRender={})", m_frameState.shouldRender);
		m_loggedFirstWaitReturn = true;
	}

	if (m_frameState.shouldRender != m_lastShouldRender) {
		utils::logInfo("OpenXR shouldRender = {}", m_frameState.shouldRender);
		m_lastShouldRender = m_frameState.shouldRender;
	}

	m_frameBegun = true;

	if (!m_frameState.shouldRender)
		return;

	if (!m_context->locateViews(m_frameState.predictedDisplayTime, m_views))
		return;

	if (!m_loggedFirstFrame) {
		utils::logInfo("OpenXR first rendered frame: {}x{} per eye", m_extent.width, m_extent.height);
		m_loggedFirstFrame = true;
	}
	for (auto &eye : m_eyes)
		eye.acquire();

	const uint32_t slot = m_frameCounter % kMaxFrames;
	++m_frameCounter;

	FrameData &frame = m_frames[slot];
	VkDevice vkDevice = m_device->get();
	vkWaitForFences(vkDevice, 1, &frame.fence, VK_TRUE, UINT64_MAX);
	vkResetFences(vkDevice, 1, &frame.fence);
	vkResetCommandBuffer(frame.cmd, 0);

	VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	if (vkBeginCommandBuffer(frame.cmd, &beginInfo) != VK_SUCCESS)
		throw std::runtime_error("Failed to begin XR command buffer");

	out.frameIndex = slot;
	out.cmd = frame.cmd;
	out.viewCount = XrContext::kViewCount;
	m_lastFrameRendered = true;

	for (uint32_t i = 0; i < XrContext::kViewCount; ++i) {
		const XrView &view = m_views[i];
		const XrSwapchain &eye = m_eyes[i];

		app::PresentTarget &target = out.targets[i];
		target.image = eye.image();
		target.view = eye.imageView();
		target.index = eye.index();
		target.extent = m_extent;
		target.format = m_format;
		target.renderArea = { .offset = { 0, 0 }, .extent = m_extent };
		target.clear = true;
		target.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		app::PresentViewCamera &camera = out.viewCameras[i];
		camera.valid = true;
		camera.localView = localViewFromPose(view.pose);
		camera.localPosition = glm::vec3(view.pose.position.x, view.pose.position.y, view.pose.position.z);
		camera.projection = projectionFromFov(view.fov, 0.02f, 1000.0f);
	}
}

void XrPresentation::endFrame(const app::PresentFrame &frame) {
	if (!m_frameBegun)
		return;

	if (frame.viewCount > 0) {
		VkDevice vkDevice = m_device->get();
		FrameData &slot = m_frames[frame.frameIndex];

		if (vkEndCommandBuffer(frame.cmd) != VK_SUCCESS)
			throw std::runtime_error("Failed to end XR command buffer");

		VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &frame.cmd;
		if (vkQueueSubmit(m_device->getGraphicsQueue(), 1, &submitInfo, slot.fence) != VK_SUCCESS)
			throw std::runtime_error("Failed to submit XR command buffer");

		// The runtime has no visibility into our queue, so the image is only
		// safe to release once the GPU has finished writing it.
		vkWaitForFences(vkDevice, 1, &slot.fence, VK_TRUE, UINT64_MAX);

		for (auto &eye : m_eyes)
			eye.release();

		if (!m_loggedFirstLayer) {
			utils::logInfo("OpenXR submitting stereo layer ({}x{} per eye)", m_extent.width, m_extent.height);
			m_loggedFirstLayer = true;
		}
	}
	XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
	std::array<XrCompositionLayerProjectionView, XrContext::kViewCount> layerViews{};
	const XrCompositionLayerBaseHeader *baseLayer = nullptr;

	if (frame.viewCount > 0) {
		layer.space = m_context->localSpace();
		layer.viewCount = XrContext::kViewCount;
		layer.views = layerViews.data();

		for (uint32_t i = 0; i < XrContext::kViewCount; ++i) {
			XrCompositionLayerProjectionView &view = layerViews[i];
			view.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
			view.pose = m_views[i].pose;
			view.fov = m_views[i].fov;
			view.subImage.swapchain = m_eyes[i].handle();
			view.subImage.imageRect = { .offset = { 0, 0 },
				                        .extent = { static_cast<int32_t>(m_extent.width), static_cast<int32_t>(m_extent.height) } };
			view.subImage.imageArrayIndex = 0;
		}

		baseLayer = reinterpret_cast<const XrCompositionLayerBaseHeader *>(&layer);
	}

	m_context->endFrame(baseLayer);
	m_frameBegun = false;
}

void XrPresentation::shutdown() {
	if (!m_device)
		return;

	vkDeviceWaitIdle(m_device->get());

	destroyFrameData(*m_device);

	for (auto &eye : m_eyes)
		eye.shutdown();

	m_device = nullptr;
	m_context = nullptr;
}

}        // namespace xr
