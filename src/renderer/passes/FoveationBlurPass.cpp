#include "FoveationBlurPass.hpp"

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
	glm::vec2 viewOffset;
	glm::vec2 foveaCenterPx;
	float halfExtent;
	float foveaRadius;
	float ringWidth;
};

}        // namespace

void FoveationBlurPass::init(RenderContext &renderCtx) {
	m_device = renderCtx.device->get();
	createDescriptorSetLayout(renderCtx);
	createDescriptorPool(renderCtx);
	createSampler(renderCtx);
	createDescriptorSets(renderCtx);
	createPipelineLayout(renderCtx);
	createPipeline(renderCtx);
}

void FoveationBlurPass::shutdown(RenderContext &renderCtx) {
	VkDevice device = renderCtx.device->get();
	vkDestroyPipeline(device, m_pipeline, nullptr);
	vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
	vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);
	vkDestroyDescriptorSetLayout(device, m_setLayout, nullptr);
	vkDestroySampler(device, m_sampler, nullptr);
	m_device = VK_NULL_HANDLE;
}

void FoveationBlurPass::createDescriptorSetLayout(RenderContext &renderCtx) {
	VkDescriptorSetLayoutBinding binding{
		.binding = 0,
		.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
	};

	VkDescriptorSetLayoutCreateInfo info{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 1,
		.pBindings = &binding,
	};

	if (vkCreateDescriptorSetLayout(m_device, &info, nullptr, &m_setLayout) != VK_SUCCESS)
		throw std::runtime_error("Failed to create foveation blur descriptor set layout");
}

void FoveationBlurPass::createDescriptorPool(RenderContext &renderCtx) {
	VkDescriptorPoolSize size{
		.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		.descriptorCount = 2,
	};

	VkDescriptorPoolCreateInfo info{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.maxSets = 2,
		.poolSizeCount = 1,
		.pPoolSizes = &size,
	};

	if (vkCreateDescriptorPool(m_device, &info, nullptr, &m_descriptorPool) != VK_SUCCESS)
		throw std::runtime_error("Failed to create foveation blur descriptor pool");
}

void FoveationBlurPass::createSampler(RenderContext &renderCtx) {
	VkSamplerCreateInfo info{
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter = VK_FILTER_NEAREST,
		.minFilter = VK_FILTER_NEAREST,
		.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
		.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.maxLod = 0.0f,
	};

	if (vkCreateSampler(m_device, &info, nullptr, &m_sampler) != VK_SUCCESS)
		throw std::runtime_error("Failed to create foveation blur sampler");
}

void FoveationBlurPass::createDescriptorSets(RenderContext &renderCtx) {
	VkDescriptorSetLayout layouts[2] = { m_setLayout, m_setLayout };
	VkDescriptorSetAllocateInfo allocInfo{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = m_descriptorPool,
		.descriptorSetCount = 2,
		.pSetLayouts = layouts,
	};
	if (vkAllocateDescriptorSets(m_device, &allocInfo, m_sets) != VK_SUCCESS)
		throw std::runtime_error("Failed to allocate foveation blur descriptor sets");

	for (uint32_t eye = 0; eye < 2; ++eye) {
		VkDescriptorImageInfo imageInfo{
			.sampler = m_sampler,
			.imageView = renderCtx.offscreenViews[eye],
			.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		};
		VkWriteDescriptorSet write{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = m_sets[eye],
			.dstBinding = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.pImageInfo = &imageInfo,
		};
		vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
	}
}

void FoveationBlurPass::createPipelineLayout(RenderContext &renderCtx) {
	VkPushConstantRange pushRange{
		.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
		.offset = 0,
		.size = sizeof(PushConstants),
	};

	VkPipelineLayoutCreateInfo info{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1,
		.pSetLayouts = &m_setLayout,
		.pushConstantRangeCount = 1,
		.pPushConstantRanges = &pushRange,
	};

	if (vkCreatePipelineLayout(m_device, &info, nullptr, &m_pipelineLayout) != VK_SUCCESS)
		throw std::runtime_error("Failed to create foveation blur pipeline layout");
}

void FoveationBlurPass::createPipeline(RenderContext &renderCtx) {
	core::Device &device = *renderCtx.device;

	VkShaderModule vert = utils::shader::createShaderModule(device, "shaders/foveation_blur.vert.spv");
	VkShaderModule frag = utils::shader::createShaderModule(device, "shaders/foveation_blur.frag.spv");

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
		.blendEnable = VK_FALSE,
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

	if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline) != VK_SUCCESS)
		throw std::runtime_error("Failed to create foveation blur pipeline");

	utils::shader::destroyShaderModule(device, vert);
	utils::shader::destroyShaderModule(device, frag);
}

void FoveationBlurPass::record(RenderContext &renderCtx, const PassContext &passCtx) {
	if (!renderCtx.foveationBlur || !renderCtx.foveationImages || !renderCtx.foveationImages->valid())
		return;
	if (passCtx.viewIndex >= 2)
		return;

	const Foveation &foveation = *renderCtx.foveationImages;

	VkRenderingAttachmentInfo colorAttachment{
		.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
		.imageView = passCtx.targetView,
		.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
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
		.viewport = glm::vec2(foveation.viewExtent().width, foveation.viewExtent().height),
		.viewOffset = glm::vec2(passCtx.renderArea.offset.x, passCtx.renderArea.offset.y),
		.foveaCenterPx = foveation.foveaCenterPx(passCtx.viewIndex),
		.halfExtent = foveation.halfExtent(),
		.foveaRadius = foveation.foveaRadius(),
		.ringWidth = foveation.midRadius() - foveation.foveaRadius(),
	};

	vkCmdBindPipeline(passCtx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
	vkCmdBindDescriptorSets(passCtx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout,
	                        0, 1, &m_sets[passCtx.viewIndex], 0, nullptr);
	vkCmdPushConstants(passCtx.cmd, m_pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
	vkCmdDraw(passCtx.cmd, 3, 1, 0, 0);

	vkCmdEndRendering(passCtx.cmd);
}

}        // namespace renderer
