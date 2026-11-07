#include "FoveationDebugPass.hpp"

#include "core/Device.hpp"
#include "renderer/Foveation.hpp"
#include "renderer/RenderContext.hpp"
#include "utils/Shader.hpp"

#include <cstdint>
#include <glm/glm.hpp>
#include <stdexcept>

namespace renderer {

namespace {

struct PushConstants {
	glm::vec2 viewport;
	glm::vec2 texelSize;
	glm::vec2 foveaCenterPx;
	float halfExtent;
	float foveaRadius;
	float midRadius;
	int32_t peripheralRate;
};

}        // namespace

void FoveationDebugPass::init(RenderContext &renderCtx) {
	createPipelineLayout(renderCtx);
	createPipeline(renderCtx);
}

void FoveationDebugPass::shutdown(RenderContext &renderCtx) {
	VkDevice device = renderCtx.device->get();
	vkDestroyPipeline(device, m_pipeline, nullptr);
	vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
}

void FoveationDebugPass::createPipelineLayout(RenderContext &renderCtx) {
	VkPushConstantRange pushRange{
		.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
		.offset = 0,
		.size = sizeof(PushConstants),
	};

	VkPipelineLayoutCreateInfo layoutInfo{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 0,
		.pSetLayouts = nullptr,
		.pushConstantRangeCount = 1,
		.pPushConstantRanges = &pushRange,
	};

	if (vkCreatePipelineLayout(renderCtx.device->get(), &layoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS)
		throw std::runtime_error("Failed to create foveation debug pipeline layout");
}

void FoveationDebugPass::createPipeline(RenderContext &renderCtx) {
	core::Device &device = *renderCtx.device;

	VkShaderModule vert = utils::shader::createShaderModule(device, "shaders/foveation_debug.vert.spv");
	VkShaderModule frag = utils::shader::createShaderModule(device, "shaders/foveation_debug.frag.spv");

	VkPipelineShaderStageCreateInfo stages[] = {
		{ .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		  .stage = VK_SHADER_STAGE_VERTEX_BIT,
		  .module = vert,
		  .pName = "main" },
		{ .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		  .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
		  .module = frag,
		  .pName = "main" },
	};

	VkPipelineVertexInputStateCreateInfo vertexInput{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
	};

	VkPipelineInputAssemblyStateCreateInfo inputAssembly{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
	};

	VkPipelineViewportStateCreateInfo viewportState{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1,
		.scissorCount = 1,
	};

	VkPipelineRasterizationStateCreateInfo rasterizer{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.polygonMode = VK_POLYGON_MODE_FILL,
		.cullMode = VK_CULL_MODE_NONE,
		.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
		.lineWidth = 1.0f,
	};

	VkPipelineMultisampleStateCreateInfo multisampling{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
	};

	VkPipelineColorBlendAttachmentState blendAttachment{
		.blendEnable = VK_TRUE,
		.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
		.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
		.colorBlendOp = VK_BLEND_OP_ADD,
		.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
		.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
		.alphaBlendOp = VK_BLEND_OP_ADD,
		.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
		                  VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
	};

	VkPipelineColorBlendStateCreateInfo colorBlending{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.attachmentCount = 1,
		.pAttachments = &blendAttachment,
	};

	VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynamicState{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		.dynamicStateCount = 2,
		.pDynamicStates = dynamicStates,
	};

	VkPipelineRenderingCreateInfo renderingInfo{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
		.colorAttachmentCount = 1,
		.pColorAttachmentFormats = &renderCtx.format,
	};

	VkGraphicsPipelineCreateInfo pipelineInfo{
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.pNext = &renderingInfo,
		.stageCount = 2,
		.pStages = stages,
		.pVertexInputState = &vertexInput,
		.pInputAssemblyState = &inputAssembly,
		.pViewportState = &viewportState,
		.pRasterizationState = &rasterizer,
		.pMultisampleState = &multisampling,
		.pColorBlendState = &colorBlending,
		.pDynamicState = &dynamicState,
		.layout = m_pipelineLayout,
	};

	if (vkCreateGraphicsPipelines(device.get(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline) != VK_SUCCESS)
		throw std::runtime_error("Failed to create foveation debug pipeline");

	utils::shader::destroyShaderModule(device, vert);
	utils::shader::destroyShaderModule(device, frag);
}

void FoveationDebugPass::record(RenderContext &renderCtx, const PassContext &passCtx) {
	if (!renderCtx.foveationDebug || !renderCtx.foveationImages || !renderCtx.foveationImages->valid())
		return;
	if (passCtx.viewIndex >= 2)
		return;
	if (passCtx.renderArea.offset.x != 0 || passCtx.renderArea.offset.y != 0)
		return;

	const Foveation &foveation = *renderCtx.foveationImages;

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
		.renderArea = passCtx.renderArea,
		.layerCount = 1,
		.colorAttachmentCount = 1,
		.pColorAttachments = &colorAttachment,
	};

	vkCmdBeginRendering(passCtx.cmd, &renderingInfo);

	VkViewport viewport{
		.x = static_cast<float>(passCtx.renderArea.offset.x),
		.y = static_cast<float>(passCtx.renderArea.offset.y),
		.width = static_cast<float>(passCtx.renderArea.extent.width),
		.height = static_cast<float>(passCtx.renderArea.extent.height),
		.minDepth = 0.0f,
		.maxDepth = 1.0f,
	};
	VkRect2D scissor = passCtx.renderArea;
	vkCmdSetViewport(passCtx.cmd, 0, 1, &viewport);
	vkCmdSetScissor(passCtx.cmd, 0, 1, &scissor);

	PushConstants pc{
		.viewport = glm::vec2(passCtx.extent.width, passCtx.extent.height),
		.texelSize = glm::vec2(foveation.texelSize().width, foveation.texelSize().height),
		.foveaCenterPx = foveation.foveaCenterPx(passCtx.viewIndex),
		.halfExtent = foveation.halfExtent(),
		.foveaRadius = foveation.foveaRadius(),
		.midRadius = foveation.midRadius(),
		.peripheralRate = static_cast<int32_t>(foveation.peripheralRate()),
	};

	vkCmdBindPipeline(passCtx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
	vkCmdPushConstants(passCtx.cmd, m_pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
	vkCmdDraw(passCtx.cmd, 3, 1, 0, 0);

	vkCmdEndRendering(passCtx.cmd);
}

}        // namespace renderer
