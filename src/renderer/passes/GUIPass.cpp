#include "GUIPass.hpp"

#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"
#include "core/Device.hpp"
#include "core/VulkanContext.hpp"
#include "imgui.h"
#include "renderer/RenderContext.hpp"
#include "renderer/RenderStats.hpp"
#include "renderer/SceneState.hpp"

#include <fmt/format.h>
#include <stdexcept>
#include <string>

namespace renderer {

namespace {

std::string formatBytes(uint64_t bytes) {
	static const char *units[] = { "B", "KiB", "MiB", "GiB" };
	double value = static_cast<double>(bytes);
	int unit = 0;
	while (value >= 1024.0 && unit < 3) {
		value /= 1024.0;
		++unit;
	}
	return fmt::format("{:.1f} {}", value, units[unit]);
}

}        // namespace

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
	ImGui::SetNextWindowSize(ImVec2(340.0f, 0.0f), ImGuiCond_FirstUseEver);
	ImGui::Begin("3DGS Renderer (Vulkan)");

	ImGui::Text("Scene: %s", m_sceneState->currentSceneName.empty() ? "(none)" : m_sceneState->currentSceneName.c_str());
	ImGui::Text("Gaussians: %u", m_sceneState->gaussianCount);
	ImGui::Text("SH degree: %d", m_sceneState->shDegree);
	ImGui::Text("Frame: %.2f ms (%.1f FPS)", stats.cpuFrameMs, stats.fps);

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Text("GPU timings (ms)");
	if (stats.gpuTimingsValid) {
		const bool tiled = m_sceneState->renderMode == RenderMode::Tiled;
		ImGui::Text("Total   %.2f", stats.gpuTotalMs);
		ImGui::Text("%-7s %.2f", tiled ? "Project" : "Back", stats.gpuBackgroundMs);
		ImGui::Text("%-7s %.2f", tiled ? "Bin" : "Keygen", stats.gpuKeygenMs);
		ImGui::Text("%-7s %.2f", tiled ? "Sort" : "Radix", stats.gpuRadixMs);
		ImGui::Text("%-7s %.2f", tiled ? "Tiles" : "Raster", stats.gpuRasterMs);
		ImGui::Text("%-7s %.2f", tiled ? "Comp" : "GUI", stats.gpuGuiMs);
	} else {
		ImGui::TextDisabled("unavailable");
	}

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Text("Scene");

	if (!m_sceneState->availableScenes.empty()) {
		const char *preview = m_sceneState->selectedScene >= 0
		                          ? m_sceneState->availableScenes[m_sceneState->selectedScene].c_str()
		                          : m_sceneState->currentSceneName.c_str();
		if (ImGui::BeginCombo("Load", preview)) {
			for (int i = 0; i < static_cast<int>(m_sceneState->availableScenes.size()); ++i) {
				bool selected = (m_sceneState->selectedScene == i);

				std::string label = m_sceneState->availableScenes[i];
				if (i < static_cast<int>(m_sceneState->sceneEstimatedBytes.size()))
					label += fmt::format("  ({})", formatBytes(m_sceneState->sceneEstimatedBytes[i]));

				if (ImGui::Selectable(label.c_str(), selected)) {
					m_sceneState->selectedScene = i;
					m_sceneState->requestedScenePath = m_sceneState->scenePaths[i];
				}
			}
			ImGui::EndCombo();
		}
	} else {
		ImGui::TextDisabled("No scenes found in assets/models/");
	}

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Text("Rendering");
	int renderMode = static_cast<int>(m_sceneState->renderMode);
	if (ImGui::Combo("Rasterizer", &renderMode, "Instanced\0Tiled\0"))
		m_sceneState->renderMode = static_cast<RenderMode>(renderMode);
	ImGui::SliderFloat("Splat scale", &m_sceneState->splatScale, 0.1f, 3.0f, "%.2f");
	if (m_sceneState->maxShDegree > 0) {
		ImGui::SliderInt("SH degree", &m_sceneState->shDegree, 0, m_sceneState->maxShDegree);
	}
	ImGui::Checkbox("Depth sort", &m_sceneState->sortEnabled);
	ImGui::Checkbox("Cull floaters", &m_sceneState->cullLargeSplats);
	if (m_sceneState->cullLargeSplats)
		ImGui::SliderFloat("Max splat size", &m_sceneState->maxSplatFraction, 0.02f, 1.0f, "%.2f");

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Text("Memory (GPU, estimated)");
	ImGui::Text("Total      %s", formatBytes(stats.gpuTotalBytes).c_str());
	ImGui::Text("Scene      %s", formatBytes(stats.gpuSceneBytes).c_str());
	ImGui::Text("Sort       %s", formatBytes(stats.gpuSortBytes).c_str());
	ImGui::Text("Frame      %s", formatBytes(stats.gpuFrameBytes).c_str());
	ImGui::Text("Swapchain  %s", formatBytes(stats.gpuSwapchainBytes).c_str());
	ImGui::TextDisabled("%llu allocations", static_cast<unsigned long long>(stats.gpuAllocations));

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Text("Camera: %s", m_sceneState->cameraMode == 0 ? "Orbit" : "Fly");
	if (ImGui::Button("Toggle camera (Tab)"))
		m_sceneState->cameraToggleRequested = true;

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Text("Background");
	ImGui::ColorEdit3("Top", &m_sceneState->backgroundColorTop.x);
	ImGui::ColorEdit3("Bottom", &m_sceneState->backgroundColorBottom.x);

	ImGui::Spacing();
	ImGui::Separator();
	if (m_sceneState->cameraMode == 0)
		ImGui::TextWrapped("Orbit: drag to rotate, scroll to zoom.");
	else
		ImGui::TextWrapped("Fly: right-drag to look, WASD move, Q/E down/up, Shift = fast.");
	ImGui::TextWrapped("[Tab] camera   [H] GUI   [Esc] quit");

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
