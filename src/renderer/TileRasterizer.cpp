#include "TileRasterizer.hpp"

#include "core/Device.hpp"
#include "gpu/Allocator.hpp"
#include "renderer/GpuProfiler.hpp"
#include "renderer/RenderContext.hpp"
#include "renderer/SceneState.hpp"
#include "renderer/passes/Pass.hpp"
#include "utils/Shader.hpp"
#include "utils/VulkanUtils.hpp"

#include <cstdint>
#include <stdexcept>
#include <vector>

namespace renderer {

namespace {

constexpr uint32_t kProjectedStride = 56;        // vec2 + vec3 + vec3 + float + uint + 2*ivec2

VkDescriptorSetLayout makeSetLayout(VkDevice device, const std::vector<VkDescriptorSetLayoutBinding> &bindings) {
	VkDescriptorSetLayoutCreateInfo info{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = static_cast<uint32_t>(bindings.size()),
		.pBindings = bindings.data(),
	};
	VkDescriptorSetLayout layout = VK_NULL_HANDLE;
	if (vkCreateDescriptorSetLayout(device, &info, nullptr, &layout) != VK_SUCCESS)
		throw std::runtime_error("Failed to create tile descriptor set layout");
	return layout;
}

VkPipelineLayout makePipelineLayout(VkDevice device, VkDescriptorSetLayout setLayout, uint32_t pushSize) {
	VkPushConstantRange range{
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
		.offset = 0,
		.size = pushSize,
	};
	VkPipelineLayoutCreateInfo info{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1,
		.pSetLayouts = &setLayout,
		.pushConstantRangeCount = pushSize > 0 ? 1u : 0u,
		.pPushConstantRanges = &range,
	};
	VkPipelineLayout layout = VK_NULL_HANDLE;
	if (vkCreatePipelineLayout(device, &info, nullptr, &layout) != VK_SUCCESS)
		throw std::runtime_error("Failed to create tile pipeline layout");
	return layout;
}

void computeBarrier(VkCommandBuffer cmd) {
	VkMemoryBarrier barrier{
		.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
		.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
	};
	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
}

struct ProjectPush {
	uint32_t count;
	uint32_t tilesX;
	uint32_t tilesY;
	uint32_t depthBits;
	float splatScale;
	float maxRadiusFraction;
	int32_t shDegree;
	float minOpacity;
	glm::vec4 sceneCenterRadius;        // xyz = center, w = radius
	glm::vec4 worldCull;                // x = max world scale fraction, y = max distance factor
};
struct BinPush {
	uint32_t maxProjected;
	uint32_t maxEntries;
	uint32_t tilesX;
	uint32_t depthBits;
};
struct HistPush {
	uint32_t maxEntries;
	uint32_t depthBits;
};
struct ScanPush {
	uint32_t tileCount;
};
struct RasterPush {
	uint32_t tilesX;
	uint32_t tilesY;
	float vpX;
	float vpY;
	float bgTop[4];
	float bgBottom[4];
};

}        // namespace

void TileRasterizer::init(core::Device &device, gpu::Allocator &allocator, RenderContext &renderCtx, uint32_t width, uint32_t height) {
	m_device = &device;
	m_vkDevice = device.get();
	m_allocator = &allocator;
	m_width = width;
	m_height = height;

	m_tilesX = (width + kTileSize - 1) / kTileSize;
	m_tilesY = (height + kTileSize - 1) / kTileSize;
	m_tileCount = m_tilesX * m_tilesY;

	uint32_t tileBits = 1;
	while ((1u << tileBits) < m_tileCount)
		++tileBits;
	m_depthBits = 32 - tileBits;

	m_radix.init(device, allocator, renderCtx, kMaxFrames);

	VkSamplerCreateInfo samplerInfo{
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter = VK_FILTER_NEAREST,
		.minFilter = VK_FILTER_NEAREST,
		.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
		.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
	};
	if (vkCreateSampler(m_vkDevice, &samplerInfo, nullptr, &m_sampler) != VK_SUCCESS)
		throw std::runtime_error("Failed to create tile sampler");

	createLayouts(renderCtx);
	createPipelines(renderCtx);

	VkDescriptorPoolSize poolSizes[] = {
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 48 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 4 },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4 },
	};
	VkDescriptorPoolCreateInfo poolInfo{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
		.maxSets = 16,
		.poolSizeCount = 4,
		.pPoolSizes = poolSizes,
	};
	if (vkCreateDescriptorPool(m_vkDevice, &poolInfo, nullptr, &m_pool) != VK_SUCCESS)
		throw std::runtime_error("Failed to create tile descriptor pool");
}

void TileRasterizer::createLayouts(RenderContext &renderCtx) {
	(void) renderCtx;
	const VkShaderStageFlags cs = VK_SHADER_STAGE_COMPUTE_BIT;

	m_projectSetLayout = makeSetLayout(m_vkDevice, {
	    { .binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 1, .stageFlags = cs },
	    { .binding = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = cs },
	    { .binding = 2, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = cs },
	    { .binding = 3, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = cs },
	});
	m_binSetLayout = makeSetLayout(m_vkDevice, {
	    { .binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = cs },
	    { .binding = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = cs },
	    { .binding = 2, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = cs },
	    { .binding = 3, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = cs },
	    { .binding = 4, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = cs },
	});
	m_histSetLayout = makeSetLayout(m_vkDevice, {
	    { .binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = cs },
	    { .binding = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = cs },
	    { .binding = 2, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = cs },
	});
	m_scanSetLayout = makeSetLayout(m_vkDevice, {
	    { .binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = cs },
	    { .binding = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = cs },
	});
	m_rasterSetLayout = makeSetLayout(m_vkDevice, {
	    { .binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = cs },
	    { .binding = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = cs },
	    { .binding = 2, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = cs },
	    { .binding = 3, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .descriptorCount = 1, .stageFlags = cs },
	});
	m_compositeSetLayout = makeSetLayout(m_vkDevice, {
	    { .binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT },
	});

	m_projectLayout = makePipelineLayout(m_vkDevice, m_projectSetLayout, sizeof(ProjectPush));
	m_binLayout = makePipelineLayout(m_vkDevice, m_binSetLayout, sizeof(BinPush));
	m_histLayout = makePipelineLayout(m_vkDevice, m_histSetLayout, sizeof(HistPush));
	m_scanLayout = makePipelineLayout(m_vkDevice, m_scanSetLayout, sizeof(ScanPush));
	m_rasterLayout = makePipelineLayout(m_vkDevice, m_rasterSetLayout, sizeof(RasterPush));
	m_compositeLayout = makePipelineLayout(m_vkDevice, m_compositeSetLayout, 0);
}

VkPipeline TileRasterizer::createComputePipeline(RenderContext &renderCtx, const char *spvPath, VkPipelineLayout layout) {
	VkShaderModule module = utils::shader::createShaderModule(*renderCtx.device, spvPath);
	VkPipelineShaderStageCreateInfo stage{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		.stage = VK_SHADER_STAGE_COMPUTE_BIT,
		.module = module,
		.pName = "main",
	};
	VkComputePipelineCreateInfo info{
		.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
		.stage = stage,
		.layout = layout,
	};
	VkPipeline pipeline = VK_NULL_HANDLE;
	if (vkCreateComputePipelines(m_vkDevice, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline) != VK_SUCCESS)
		throw std::runtime_error("Failed to create tile compute pipeline");
	utils::shader::destroyShaderModule(*renderCtx.device, module);
	return pipeline;
}

void TileRasterizer::createPipelines(RenderContext &renderCtx) {
	m_projectPipeline = createComputePipeline(renderCtx, "shaders/project.comp.spv", m_projectLayout);
	m_binPipeline = createComputePipeline(renderCtx, "shaders/tile_bin.comp.spv", m_binLayout);
	m_histPipeline = createComputePipeline(renderCtx, "shaders/tile_histogram.comp.spv", m_histLayout);
	m_scanPipeline = createComputePipeline(renderCtx, "shaders/tile_scan.comp.spv", m_scanLayout);
	m_rasterPipeline = createComputePipeline(renderCtx, "shaders/tile_raster.comp.spv", m_rasterLayout);

	// Composite graphics pipeline (fullscreen triangle sampling the tile output).
	core::Device &device = *renderCtx.device;
	VkShaderModule vert = utils::shader::createShaderModule(device, "shaders/composite.vert.spv");
	VkShaderModule frag = utils::shader::createShaderModule(device, "shaders/composite.frag.spv");
	VkPipelineShaderStageCreateInfo stages[] = {
		{ .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vert, .pName = "main" },
		{ .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = frag, .pName = "main" },
	};
	VkPipelineVertexInputStateCreateInfo vertexInput{ .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
	VkPipelineInputAssemblyStateCreateInfo inputAssembly{ .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST };
	VkPipelineViewportStateCreateInfo viewportState{ .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .scissorCount = 1 };
	VkPipelineRasterizationStateCreateInfo rasterizer{ .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, .polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE, .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE, .lineWidth = 1.0f };
	VkPipelineMultisampleStateCreateInfo multisampling{ .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT };
	VkPipelineColorBlendAttachmentState blendAttachment{ .blendEnable = VK_FALSE, .colorWriteMask = 0xF };
	VkPipelineColorBlendStateCreateInfo colorBlending{ .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, .attachmentCount = 1, .pAttachments = &blendAttachment };
	VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynamicState{ .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, .dynamicStateCount = 2, .pDynamicStates = dynamicStates };
	VkPipelineRenderingCreateInfo renderingInfo{ .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO, .colorAttachmentCount = 1, .pColorAttachmentFormats = &renderCtx.format };
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
		.layout = m_compositeLayout,
	};
	if (vkCreateGraphicsPipelines(m_vkDevice, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_compositePipeline) != VK_SUCCESS)
		throw std::runtime_error("Failed to create composite pipeline");
	utils::shader::destroyShaderModule(device, vert);
	utils::shader::destroyShaderModule(device, frag);
}

void TileRasterizer::releaseFrameBuffers() {
	if (!m_allocated)
		return;
	for (auto &frame : m_frames) {
		frame.projected.shutdown(m_allocator->get());
		frame.projectedCount.shutdown(m_allocator->get());
		frame.entryCount.shutdown(m_allocator->get());
		frame.tileCounts.shutdown(m_allocator->get());
		frame.tileStart.shutdown(m_allocator->get());
		frame.output.shutdown(m_allocator->get());
	}
	m_allocated = false;
}

void TileRasterizer::bind(VkBuffer gaussianBuffer, const std::array<VkBuffer, 2> &cameraBuffers, uint32_t count) {
	vkDeviceWaitIdle(m_vkDevice);
	releaseFrameBuffers();

	m_count = count;
	if (count == 0)
		return;

	m_maxEntries = count * 4;
	m_radix.resize(m_maxEntries);
	vkResetDescriptorPool(m_vkDevice, m_pool, 0);

	VkDeviceSize projectedBytes = static_cast<VkDeviceSize>(count) * kProjectedStride;

	for (auto &frame : m_frames) {
		frame.projected.init(*m_device, m_allocator->get(), projectedBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
		frame.projectedCount.init(*m_device, m_allocator->get(), sizeof(uint32_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
		frame.entryCount.init(*m_device, m_allocator->get(), sizeof(uint32_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
		frame.tileCounts.init(*m_device, m_allocator->get(), static_cast<VkDeviceSize>(m_tileCount) * sizeof(uint32_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
		frame.tileStart.init(*m_device, m_allocator->get(), static_cast<VkDeviceSize>(m_tileCount + 1) * sizeof(uint32_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
		frame.output.init(*m_device, m_allocator->get(), { m_width, m_height }, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT);

		VkDescriptorSetLayout layouts[] = {
			m_projectSetLayout, m_binSetLayout, m_histSetLayout, m_scanSetLayout, m_rasterSetLayout, m_compositeSetLayout,
		};
		VkDescriptorSet sets[6]{};
		VkDescriptorSetAllocateInfo allocInfo{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.descriptorPool = m_pool,
			.descriptorSetCount = 6,
			.pSetLayouts = layouts,
		};
		if (vkAllocateDescriptorSets(m_vkDevice, &allocInfo, sets) != VK_SUCCESS)
			throw std::runtime_error("Failed to allocate tile descriptor sets");
		frame.projectSet = sets[0];
		frame.binSet = sets[1];
		frame.histSet = sets[2];
		frame.scanSet = sets[3];
		frame.rasterSet = sets[4];
		frame.compositeSet = sets[5];
	}
	m_allocated = true;

	for (uint32_t f = 0; f < kMaxFrames; ++f)
		m_radix.setCountSource(f, m_frames[f].entryCount.get());

	writeDescriptorSets(gaussianBuffer, cameraBuffers);
}

void TileRasterizer::writeDescriptorSets(VkBuffer gaussianBuffer, const std::array<VkBuffer, 2> &cameraBuffers) {
	for (uint32_t f = 0; f < kMaxFrames; ++f) {
		FrameResources &frame = m_frames[f];

		VkDescriptorBufferInfo ubo{ cameraBuffers[f], 0, VK_WHOLE_SIZE };
		VkDescriptorBufferInfo gauss{ gaussianBuffer, 0, VK_WHOLE_SIZE };
		VkDescriptorBufferInfo projected{ frame.projected.get(), 0, VK_WHOLE_SIZE };
		VkDescriptorBufferInfo projCount{ frame.projectedCount.get(), 0, VK_WHOLE_SIZE };
		VkDescriptorBufferInfo entryCount{ frame.entryCount.get(), 0, VK_WHOLE_SIZE };
		VkDescriptorBufferInfo keys{ m_radix.keys(f), 0, VK_WHOLE_SIZE };
		VkDescriptorBufferInfo values{ m_radix.values(f), 0, VK_WHOLE_SIZE };
		VkDescriptorBufferInfo tileCounts{ frame.tileCounts.get(), 0, VK_WHOLE_SIZE };
		VkDescriptorBufferInfo tileStart{ frame.tileStart.get(), 0, VK_WHOLE_SIZE };

		auto writeBuf = [&](VkDescriptorSet set, uint32_t binding, VkDescriptorBufferInfo *info) {
			VkWriteDescriptorSet w{
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = set,
				.dstBinding = binding,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				.pBufferInfo = info,
			};
			vkUpdateDescriptorSets(m_vkDevice, 1, &w, 0, nullptr);
		};
		auto writeUbo = [&](VkDescriptorSet set, uint32_t binding, VkDescriptorBufferInfo *info) {
			VkWriteDescriptorSet w{
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = set,
				.dstBinding = binding,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				.pBufferInfo = info,
			};
			vkUpdateDescriptorSets(m_vkDevice, 1, &w, 0, nullptr);
		};

		writeUbo(frame.projectSet, 0, &ubo);
		writeBuf(frame.projectSet, 1, &gauss);
		writeBuf(frame.projectSet, 2, &projected);
		writeBuf(frame.projectSet, 3, &projCount);

		writeBuf(frame.binSet, 0, &projected);
		writeBuf(frame.binSet, 1, &projCount);
		writeBuf(frame.binSet, 2, &keys);
		writeBuf(frame.binSet, 3, &values);
		writeBuf(frame.binSet, 4, &entryCount);

		writeBuf(frame.histSet, 0, &keys);
		writeBuf(frame.histSet, 1, &entryCount);
		writeBuf(frame.histSet, 2, &tileCounts);

		writeBuf(frame.scanSet, 0, &tileCounts);
		writeBuf(frame.scanSet, 1, &tileStart);

		writeBuf(frame.rasterSet, 0, &projected);
		writeBuf(frame.rasterSet, 1, &values);
		writeBuf(frame.rasterSet, 2, &tileStart);
		{
			VkDescriptorImageInfo img{ .imageView = frame.output.getView(), .imageLayout = VK_IMAGE_LAYOUT_GENERAL };
			VkWriteDescriptorSet w{ .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = frame.rasterSet, .dstBinding = 3, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .pImageInfo = &img };
			vkUpdateDescriptorSets(m_vkDevice, 1, &w, 0, nullptr);
		}

		{
			VkDescriptorImageInfo img{ .sampler = m_sampler, .imageView = frame.output.getView(), .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
			VkWriteDescriptorSet w{ .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = frame.compositeSet, .dstBinding = 0, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = &img };
			vkUpdateDescriptorSets(m_vkDevice, 1, &w, 0, nullptr);
		}
	}
}

void TileRasterizer::record(RenderContext &renderCtx, const PassContext &passCtx) {
	if (!m_allocated || m_count == 0)
		return;

	uint32_t f = passCtx.frameIndex;
	FrameResources &frame = m_frames[f];
	VkCommandBuffer cmd = passCtx.cmd;

	// Idle-skip: only rebuild the projected/bin/sorted data when needed.
	const bool rebuild = renderCtx.sceneState->sortDirty;
	if (rebuild) {
	// Reset per-frame counters and the key buffer (padding = 0xFFFFFFFF).
	vkCmdFillBuffer(cmd, frame.projectedCount.get(), 0, sizeof(uint32_t), 0);
	vkCmdFillBuffer(cmd, frame.entryCount.get(), 0, sizeof(uint32_t), 0);
	vkCmdFillBuffer(cmd, frame.tileCounts.get(), 0, static_cast<VkDeviceSize>(m_tileCount) * sizeof(uint32_t), 0);
	vkCmdFillBuffer(cmd, m_radix.keys(f), 0, VK_WHOLE_SIZE, 0xFFFFFFFFu);
	VkMemoryBarrier fillBarrier{
		.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
		.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
	};
	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &fillBarrier, 0, nullptr, 0, nullptr);

	uint32_t projectGroups = (m_count + 255) / 256;
	uint32_t histGroups = (m_maxEntries + 255) / 256;

	// 1) Project
	ProjectPush pp{ m_count, m_tilesX, m_tilesY, m_depthBits, renderCtx.sceneState->splatScale,
		            renderCtx.sceneState->cullLargeSplats ? renderCtx.sceneState->maxSplatFraction : 0.0f,
		            renderCtx.sceneState->shDegree,
		            renderCtx.sceneState->minOpacity,
		            glm::vec4(renderCtx.sceneState->sceneCenter, renderCtx.sceneState->sceneRadius),
		            glm::vec4(renderCtx.sceneState->maxWorldScaleFraction,
		                      renderCtx.sceneState->maxDistanceFactor, 0.0f, 0.0f) };
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_projectPipeline);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_projectLayout, 0, 1, &frame.projectSet, 0, nullptr);
	vkCmdPushConstants(cmd, m_projectLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pp), &pp);
	vkCmdDispatch(cmd, projectGroups, 1, 1);
	computeBarrier(cmd);
	if (renderCtx.profiler)
		renderCtx.profiler->write(cmd, f, GpuSlot::Background);

	// 2) Bin
	BinPush bp{ m_count, m_maxEntries, m_tilesX, m_depthBits };
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_binPipeline);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_binLayout, 0, 1, &frame.binSet, 0, nullptr);
	vkCmdPushConstants(cmd, m_binLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(bp), &bp);
	vkCmdDispatch(cmd, projectGroups, 1, 1);
	computeBarrier(cmd);
	if (renderCtx.profiler)
		renderCtx.profiler->write(cmd, f, GpuSlot::Keygen);

	// 3) Sort entries by tile key
	m_radix.record(cmd, f, false);
	if (renderCtx.profiler)
		renderCtx.profiler->write(cmd, f, GpuSlot::Radix);

	// 4) Tile ranges
	HistPush hp{ m_maxEntries, m_depthBits };
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_histPipeline);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_histLayout, 0, 1, &frame.histSet, 0, nullptr);
	vkCmdPushConstants(cmd, m_histLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(hp), &hp);
	vkCmdDispatch(cmd, histGroups, 1, 1);
	computeBarrier(cmd);

	ScanPush sp{ m_tileCount };
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_scanPipeline);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_scanLayout, 0, 1, &frame.scanSet, 0, nullptr);
	vkCmdPushConstants(cmd, m_scanLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(sp), &sp);
	vkCmdDispatch(cmd, 1, 1, 1);
	computeBarrier(cmd);
	} else if (renderCtx.profiler) {
		renderCtx.profiler->write(cmd, f, GpuSlot::Background);
		renderCtx.profiler->write(cmd, f, GpuSlot::Keygen);
		renderCtx.profiler->write(cmd, f, GpuSlot::Radix);
	}

	// 5) Tile raster
	frame.output.transition(cmd, VK_IMAGE_LAYOUT_GENERAL);
	RasterPush rp{ m_tilesX, m_tilesY,
		           static_cast<float>(m_width), static_cast<float>(m_height),
		           { renderCtx.sceneState->backgroundColorTop.r, renderCtx.sceneState->backgroundColorTop.g, renderCtx.sceneState->backgroundColorTop.b, 1.0f },
		           { renderCtx.sceneState->backgroundColorBottom.r, renderCtx.sceneState->backgroundColorBottom.g, renderCtx.sceneState->backgroundColorBottom.b, 1.0f } };
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_rasterPipeline);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_rasterLayout, 0, 1, &frame.rasterSet, 0, nullptr);
	vkCmdPushConstants(cmd, m_rasterLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(rp), &rp);
	vkCmdDispatch(cmd, m_tileCount, 1, 1);
	if (renderCtx.profiler)
		renderCtx.profiler->write(cmd, f, GpuSlot::Raster);

	// 6) Composite to the presentation target
	frame.output.transition(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	// Discard prior contents (loadOp = CLEAR fully covers the target).
	utils::vk::transitionImage(cmd, passCtx.targetImage,
	                           VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	                           0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
	                           VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

	VkRenderingAttachmentInfo colorAttachment{
		.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
		.imageView = passCtx.targetView,
		.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		.clearValue = { .color = { { 0.0f, 0.0f, 0.0f, 1.0f } } },
	};
	VkRenderingInfo renderingInfo{
		.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
		.renderArea = { { 0, 0 }, passCtx.extent },
		.layerCount = 1,
		.colorAttachmentCount = 1,
		.pColorAttachments = &colorAttachment,
	};
	vkCmdBeginRendering(cmd, &renderingInfo);
	VkViewport viewport{ 0.0f, 0.0f, static_cast<float>(passCtx.extent.width), static_cast<float>(passCtx.extent.height), 0.0f, 1.0f };
	VkRect2D scissor{ { 0, 0 }, passCtx.extent };
	vkCmdSetViewport(cmd, 0, 1, &viewport);
	vkCmdSetScissor(cmd, 0, 1, &scissor);
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_compositePipeline);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_compositeLayout, 0, 1, &frame.compositeSet, 0, nullptr);
	vkCmdDraw(cmd, 3, 1, 0, 0);
	vkCmdEndRendering(cmd);
	if (renderCtx.profiler)
		renderCtx.profiler->write(cmd, f, GpuSlot::Gui);
}

uint64_t TileRasterizer::gpuBytes() const {
	if (!m_allocated)
		return 0;
	uint64_t projected = static_cast<uint64_t>(m_count) * kProjectedStride;
	uint64_t small = 3 * sizeof(uint32_t) + static_cast<uint64_t>(m_tileCount + 1) * sizeof(uint32_t);
	uint64_t output = static_cast<uint64_t>(m_width) * m_height * 4;
	uint64_t perFrame = projected + small + output;
	return static_cast<uint64_t>(kMaxFrames) * perFrame + m_radix.gpuBytes();
}

void TileRasterizer::shutdown() {
	vkDeviceWaitIdle(m_vkDevice);
	releaseFrameBuffers();
	m_radix.shutdown();

	vkDestroySampler(m_vkDevice, m_sampler, nullptr);
	vkDestroyPipeline(m_vkDevice, m_projectPipeline, nullptr);
	vkDestroyPipeline(m_vkDevice, m_binPipeline, nullptr);
	vkDestroyPipeline(m_vkDevice, m_histPipeline, nullptr);
	vkDestroyPipeline(m_vkDevice, m_scanPipeline, nullptr);
	vkDestroyPipeline(m_vkDevice, m_rasterPipeline, nullptr);
	vkDestroyPipeline(m_vkDevice, m_compositePipeline, nullptr);
	vkDestroyPipelineLayout(m_vkDevice, m_projectLayout, nullptr);
	vkDestroyPipelineLayout(m_vkDevice, m_binLayout, nullptr);
	vkDestroyPipelineLayout(m_vkDevice, m_histLayout, nullptr);
	vkDestroyPipelineLayout(m_vkDevice, m_scanLayout, nullptr);
	vkDestroyPipelineLayout(m_vkDevice, m_rasterLayout, nullptr);
	vkDestroyPipelineLayout(m_vkDevice, m_compositeLayout, nullptr);
	vkDestroyDescriptorSetLayout(m_vkDevice, m_projectSetLayout, nullptr);
	vkDestroyDescriptorSetLayout(m_vkDevice, m_binSetLayout, nullptr);
	vkDestroyDescriptorSetLayout(m_vkDevice, m_histSetLayout, nullptr);
	vkDestroyDescriptorSetLayout(m_vkDevice, m_scanSetLayout, nullptr);
	vkDestroyDescriptorSetLayout(m_vkDevice, m_rasterSetLayout, nullptr);
	vkDestroyDescriptorSetLayout(m_vkDevice, m_compositeSetLayout, nullptr);
	vkDestroyDescriptorPool(m_vkDevice, m_pool, nullptr);
}

}        // namespace renderer
