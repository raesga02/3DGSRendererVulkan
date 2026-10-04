#include "GUIPass.hpp"

#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"
#include "core/Device.hpp"
#include "core/VulkanContext.hpp"
#include "imgui.h"
#include "renderer/RenderContext.hpp"
#include "renderer/RenderStats.hpp"
#include "renderer/SceneState.hpp"

#include <stdexcept>

namespace renderer {

void GUIPass::init(RenderContext &renderCtx) {
	m_sceneState = renderCtx.sceneState;
}

void GUIPass::initVulkan(GLFWwindow *window, core::VulkanContext &vkContext, core::Device &device, gpu::Allocator &allocator, const core::SwapchainInfo &swapchainInfo, SceneState &sceneState) {
	m_sceneState = &sceneState;

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();

	ImGuiIO &io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.Fonts->AddFontDefault();

	ImGui_ImplGlfw_InitForVulkan(window, true);

	VkDescriptorPoolSize poolSizes[] = {
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, IMGUI_IMPL_VULKAN_MINIMUM_IMAGE_SAMPLER_POOL_SIZE },
	};

	VkDescriptorPoolCreateInfo poolInfo{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
		.maxSets = 1000 * (uint32_t) (sizeof(poolSizes) / sizeof(poolSizes[0])),
		.poolSizeCount = (uint32_t) (sizeof(poolSizes) / sizeof(poolSizes[0])),
		.pPoolSizes = poolSizes,
	};

	if (vkCreateDescriptorPool(device.get(), &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS)
		throw std::runtime_error("Failed to create ImGui descriptor pool");

	VkPipelineRenderingCreateInfo pipelineRenderingInfo{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
		.colorAttachmentCount = 1,
		.pColorAttachmentFormats = &swapchainInfo.format,
	};

	ImGui_ImplVulkan_InitInfo initInfo{
		.ApiVersion = vkContext.getVkbInstance().api_version,
		.Instance = vkContext.getInstance(),
		.PhysicalDevice = device.getPhysicalDevice(),
		.Device = device.get(),
		.QueueFamily = device.getGraphicsQueueIndex(),
		.Queue = device.getGraphicsQueue(),
		.DescriptorPool = m_descriptorPool,
		.MinImageCount = 2,
		.ImageCount = swapchainInfo.imageCount,
		.PipelineInfoMain = { .PipelineRenderingCreateInfo = pipelineRenderingInfo },
		.UseDynamicRendering = true,
	};

	if (!ImGui_ImplVulkan_Init(&initInfo))
		throw std::runtime_error("Failed to initialize ImGui Vulkan backend");

	ImGui_ImplVulkan_CreateMainPipeline(&initInfo.PipelineInfoMain);
}

void GUIPass::shutdown(RenderContext &renderCtx) {
	ImGui_ImplVulkan_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();

	vkDestroyDescriptorPool(renderCtx.device->get(), m_descriptorPool, nullptr);
}

void GUIPass::newFrame(const RenderStats &stats) {
	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();

	ImGui::SetNextWindowPos(ImVec2(16.0f, 16.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(320.0f, 0.0f), ImGuiCond_FirstUseEver);
	ImGui::Begin("3DGS Renderer (Vulkan)");

	ImGui::Text("Phase 0 - scaffold");
	ImGui::Separator();
	ImGui::Text("Frame: %.2f ms (%.1f FPS)", stats.cpuFrameMs, stats.fps);

	ImGui::Spacing();
	ImGui::Text("Background");
	ImGui::ColorEdit3("Top", &m_sceneState->backgroundColorTop.x);
	ImGui::ColorEdit3("Bottom", &m_sceneState->backgroundColorBottom.x);

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::TextWrapped("Next: load an INRIA .ply and render Gaussians.");
	ImGui::TextWrapped("[H] toggle GUI    [Esc] quit");

	ImGui::End();
}

void GUIPass::record(RenderContext &renderCtx, const PassContext &passCtx) {
	ImGui::Render();

	VkRenderingAttachmentInfo colorAttachment{
		.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
		.imageView = passCtx.targetView,
		.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		.clearValue = { .color = { { 0.0f, 0.0f, 0.0f, 0.0f } } },
	};

	VkRenderingInfo renderingInfo{
		.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
		.renderArea = { { 0, 0 }, passCtx.extent },
		.layerCount = 1,
		.colorAttachmentCount = 1,
		.pColorAttachments = &colorAttachment,
	};

	vkCmdBeginRendering(passCtx.cmd, &renderingInfo);
	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), passCtx.cmd);
	vkCmdEndRendering(passCtx.cmd);
}

bool GUIPass::wantsCaptureMouse() const {
	return ImGui::GetIO().WantCaptureMouse;
}

bool GUIPass::wantsCaptureKeyboard() const {
	return ImGui::GetIO().WantCaptureKeyboard;
}

}        // namespace renderer
