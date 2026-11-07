#include "RasterizePass.hpp"

#include "app/IPresentation.hpp"
#include "core/Device.hpp"
#include "renderer/RenderContext.hpp"
#include "renderer/SceneState.hpp"
#include "utils/Shader.hpp"

#include <cstdint>
#include <stdexcept>
#include <vector>

namespace renderer {

void RasterizePass::init(RenderContext &renderCtx) {
	m_device = renderCtx.device->get();
	createDescriptorSetLayout(renderCtx);
	createDescriptorPool(renderCtx);
	createDescriptorSets(renderCtx);
	createPipelineLayout(renderCtx);
	createPipeline(renderCtx);
}

void RasterizePass::shutdown(RenderContext &renderCtx) {
	VkDevice device = renderCtx.device->get();
	vkDestroyPipeline(device, m_pipeline, nullptr);
	vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
	vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);
	vkDestroyDescriptorSetLayout(device, m_setLayout, nullptr);
}

void RasterizePass::createDescriptorSetLayout(RenderContext &renderCtx) {
	VkDescriptorSetLayoutBinding bindings[] = {
		{ .binding = 0,
		  .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		  .descriptorCount = 1,
		  .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT },
		{ .binding = 1,
		  .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		  .descriptorCount = 1,
		  .stageFlags = VK_SHADER_STAGE_VERTEX_BIT },
		{ .binding = 2,
		  .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		  .descriptorCount = 1,
		  .stageFlags = VK_SHADER_STAGE_VERTEX_BIT },
	};

	VkDescriptorSetLayoutCreateInfo layoutInfo{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 3,
		.pBindings = bindings,
	};

	if (vkCreateDescriptorSetLayout(renderCtx.device->get(), &layoutInfo, nullptr, &m_setLayout) != VK_SUCCESS)
		throw std::runtime_error("Failed to create rasterize descriptor set layout");
}

void RasterizePass::createDescriptorPool(RenderContext &renderCtx) {
	const uint32_t setCount = app::kMaxFramesInFlight * app::kMaxViews;
	VkDescriptorPoolSize poolSizes[] = {
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, setCount },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, setCount * 2 },
	};

	VkDescriptorPoolCreateInfo poolInfo{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.maxSets = setCount,
		.poolSizeCount = 2,
		.pPoolSizes = poolSizes,
	};

	if (vkCreateDescriptorPool(renderCtx.device->get(), &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS)
		throw std::runtime_error("Failed to create rasterize descriptor pool");
}

void RasterizePass::createDescriptorSets(RenderContext &renderCtx) {
	std::array<VkDescriptorSetLayout, app::kMaxFramesInFlight * app::kMaxViews> layouts;
	layouts.fill(m_setLayout);

	VkDescriptorSetAllocateInfo allocInfo{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = m_descriptorPool,
		.descriptorSetCount = static_cast<uint32_t>(layouts.size()),
		.pSetLayouts = layouts.data(),
	};

	// Descriptor sets are stored as [frame][view], which is the same linear
	// order the allocation uses.
	if (vkAllocateDescriptorSets(renderCtx.device->get(), &allocInfo, &m_sets[0][0]) != VK_SUCCESS)
		throw std::runtime_error("Failed to allocate rasterize descriptor sets");
}

void RasterizePass::createPipelineLayout(RenderContext &renderCtx) {
	VkPushConstantRange pushRange{
		.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
		.offset = 0,
		.size = sizeof(float) * 4 + sizeof(glm::vec4) * 2,
	};

	VkPipelineLayoutCreateInfo layoutInfo{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1,
		.pSetLayouts = &m_setLayout,
		.pushConstantRangeCount = 1,
		.pPushConstantRanges = &pushRange,
	};

	if (vkCreatePipelineLayout(renderCtx.device->get(), &layoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS)
		throw std::runtime_error("Failed to create rasterize pipeline layout");
}

void RasterizePass::createPipeline(RenderContext &renderCtx) {
	core::Device &device = *renderCtx.device;

	VkShaderModule vert = utils::shader::createShaderModule(device, "shaders/splat.vert.spv");
	VkShaderModule frag = utils::shader::createShaderModule(device, "shaders/splat.frag.spv");

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

	// Standard back-to-front alpha blending.
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
		throw std::runtime_error("Failed to create rasterize pipeline");

	utils::shader::destroyShaderModule(device, vert);
	utils::shader::destroyShaderModule(device, frag);
}

void RasterizePass::bind(VkBuffer gaussianBuffer,
                         const std::array<VkBuffer, 2> &indexBuffers,
                         const std::array<std::array<VkBuffer, app::kMaxViews>, 2> &cameraBuffers,
                         const std::array<VkBuffer, 2> &indirectBuffers,
                         uint32_t instanceCount) {
	m_instanceCount = instanceCount;
	m_indirectBuffers = indirectBuffers;

	std::array<std::array<VkDescriptorBufferInfo, app::kMaxViews>, app::kMaxFramesInFlight> uboInfo;
	std::array<std::array<VkDescriptorBufferInfo, app::kMaxViews>, app::kMaxFramesInFlight> gaussianInfo;
	std::array<std::array<VkDescriptorBufferInfo, app::kMaxViews>, app::kMaxFramesInFlight> indexInfo;
	std::vector<VkWriteDescriptorSet> writes;
	writes.reserve(app::kMaxFramesInFlight * app::kMaxViews * 3);

	for (uint32_t f = 0; f < app::kMaxFramesInFlight; ++f) {
		for (uint32_t v = 0; v < app::kMaxViews; ++v) {
			uboInfo[f][v] = { .buffer = cameraBuffers[f][v], .offset = 0, .range = VK_WHOLE_SIZE };
			gaussianInfo[f][v] = { .buffer = gaussianBuffer, .offset = 0, .range = VK_WHOLE_SIZE };
			indexInfo[f][v] = { .buffer = indexBuffers[f], .offset = 0, .range = VK_WHOLE_SIZE };

			VkDescriptorSet set = m_sets[f][v];
			writes.push_back({ .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			                   .dstSet = set,
			                   .dstBinding = 0,
			                   .descriptorCount = 1,
			                   .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			                   .pBufferInfo = &uboInfo[f][v] });
			writes.push_back({ .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			                   .dstSet = set,
			                   .dstBinding = 1,
			                   .descriptorCount = 1,
			                   .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			                   .pBufferInfo = &gaussianInfo[f][v] });
			writes.push_back({ .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			                   .dstSet = set,
			                   .dstBinding = 2,
			                   .descriptorCount = 1,
			                   .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			                   .pBufferInfo = &indexInfo[f][v] });
		}
	}

	vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

void RasterizePass::record(RenderContext &renderCtx, const PassContext &passCtx) {
	if (m_instanceCount == 0)
		return;

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

	struct PushConstants {
		float splatScale;
		float maxRadiusFraction;
		int32_t shDegree;
		float minOpacity;
		glm::vec4 sceneCenterRadius;        // xyz = center, w = radius
		glm::vec4 worldCull;                // x = max world scale fraction, y = max distance factor
	};

	PushConstants pc{
		.splatScale = renderCtx.sceneState->splatScale,
		.maxRadiusFraction = renderCtx.sceneState->cullLargeSplats ? renderCtx.sceneState->maxSplatFraction : 0.0f,
		.shDegree = renderCtx.sceneState->shDegree,
		.minOpacity = renderCtx.sceneState->minOpacity,
		.sceneCenterRadius = glm::vec4(renderCtx.sceneState->sceneCenter, renderCtx.sceneState->sceneRadius),
		.worldCull = glm::vec4(renderCtx.sceneState->maxWorldScaleFraction,
		                       renderCtx.sceneState->maxDistanceFactor, 0.0f, 0.0f),
	};

	vkCmdBindPipeline(passCtx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
	vkCmdBindDescriptorSets(passCtx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &m_sets[passCtx.frameIndex][passCtx.viewIndex], 0, nullptr);
	vkCmdPushConstants(passCtx.cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);

	// When sorting is enabled the GPU has culled off-screen Gaussians and built
	// an indirect draw command with the visible instance count.
	VkBuffer indirect = m_indirectBuffers[passCtx.frameIndex];
	if (renderCtx.sceneState->sortEnabled && indirect != VK_NULL_HANDLE) {
		vkCmdDrawIndirect(passCtx.cmd, indirect, 0, 1, sizeof(VkDrawIndirectCommand));
	} else {
		vkCmdDraw(passCtx.cmd, 6, m_instanceCount, 0, 0);
	}

	vkCmdEndRendering(passCtx.cmd);
}

}        // namespace renderer
