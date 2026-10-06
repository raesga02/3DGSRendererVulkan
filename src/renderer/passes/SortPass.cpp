#include "SortPass.hpp"

#include "core/Device.hpp"
#include "gpu/Allocator.hpp"
#include "renderer/GpuProfiler.hpp"
#include "renderer/RenderContext.hpp"
#include "renderer/SceneState.hpp"
#include "utils/Shader.hpp"

#include <cstdint>
#include <stdexcept>
#include <vector>

namespace renderer {

namespace {

VkDescriptorSetLayout makeSetLayout(VkDevice device, const std::vector<VkDescriptorSetLayoutBinding> &bindings) {
	VkDescriptorSetLayoutCreateInfo info{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = static_cast<uint32_t>(bindings.size()),
		.pBindings = bindings.data(),
	};
	VkDescriptorSetLayout layout = VK_NULL_HANDLE;
	if (vkCreateDescriptorSetLayout(device, &info, nullptr, &layout) != VK_SUCCESS)
		throw std::runtime_error("Failed to create sort descriptor set layout");
	return layout;
}

VkPipelineLayout makePipelineLayout(VkDevice device, VkDescriptorSetLayout setLayout) {
	VkPushConstantRange range{
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
		.offset = 0,
		.size = sizeof(uint32_t),
	};
	VkPipelineLayoutCreateInfo info{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1,
		.pSetLayouts = &setLayout,
		.pushConstantRangeCount = 1,
		.pPushConstantRanges = &range,
	};
	VkPipelineLayout layout = VK_NULL_HANDLE;
	if (vkCreatePipelineLayout(device, &info, nullptr, &layout) != VK_SUCCESS)
		throw std::runtime_error("Failed to create sort pipeline layout");
	return layout;
}

}        // namespace

void SortPass::init(RenderContext &renderCtx) {
	m_device = renderCtx.device;
	m_vkDevice = renderCtx.device->get();
	m_allocator = renderCtx.allocator;

	m_radix.init(*renderCtx.device, *renderCtx.allocator, renderCtx, MAX_FRAMES_IN_FLIGHT);

	createLayouts(renderCtx);
	createPipelines(renderCtx);

	VkDescriptorPoolSize poolSizes[] = {
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, MAX_FRAMES_IN_FLIGHT },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 16 },
	};
	VkDescriptorPoolCreateInfo poolInfo{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
		.maxSets = 4 * MAX_FRAMES_IN_FLIGHT,
		.poolSizeCount = 2,
		.pPoolSizes = poolSizes,
	};
	if (vkCreateDescriptorPool(m_vkDevice, &poolInfo, nullptr, &m_pool) != VK_SUCCESS)
		throw std::runtime_error("Failed to create sort descriptor pool");
}

void SortPass::createLayouts(RenderContext &renderCtx) {
	(void) renderCtx;
	const VkShaderStageFlags cs = VK_SHADER_STAGE_COMPUTE_BIT;

	m_keygenSetLayout = makeSetLayout(m_vkDevice, {
	    { .binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 1, .stageFlags = cs },
	    { .binding = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = cs },
	    { .binding = 2, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = cs },
	    { .binding = 3, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = cs },
	    { .binding = 4, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = cs },
	});
	m_indirectSetLayout = makeSetLayout(m_vkDevice, {
	    { .binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = cs },
	    { .binding = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = cs },
	});

	m_keygenLayout = makePipelineLayout(m_vkDevice, m_keygenSetLayout);
	m_indirectLayout = makePipelineLayout(m_vkDevice, m_indirectSetLayout);
}

VkPipeline SortPass::createComputePipeline(RenderContext &renderCtx, const char *spvPath, VkPipelineLayout layout) {
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
		throw std::runtime_error("Failed to create sort compute pipeline");
	utils::shader::destroyShaderModule(*renderCtx.device, module);
	return pipeline;
}

void SortPass::createPipelines(RenderContext &renderCtx) {
	m_keygenPipeline = createComputePipeline(renderCtx, "shaders/keygen.comp.spv", m_keygenLayout);
	m_indirectPipeline = createComputePipeline(renderCtx, "shaders/build_indirect.comp.spv", m_indirectLayout);
}

void SortPass::releaseFrameBuffers() {
	if (!m_allocated)
		return;
	for (auto &frame : m_frames) {
		frame.visibleCount.shutdown(m_allocator->get());
		frame.indirect.shutdown(m_allocator->get());
	}
	m_countSource.shutdown(m_allocator->get());
	m_allocated = false;
}

void SortPass::bind(VkBuffer gaussianBuffer, const std::array<VkBuffer, 2> &cameraBuffers, uint32_t count) {
	vkDeviceWaitIdle(m_vkDevice);
	releaseFrameBuffers();

	m_count = count;
	if (count == 0)
		return;

	m_paddedCount = ((count + kBlockSize - 1) / kBlockSize) * kBlockSize;
	m_keygenGroups = m_paddedCount / 256;

	m_radix.resize(m_paddedCount);

	// The radix sort reads its element count from this buffer.
	m_countSource.init(*m_device, m_allocator->get(), sizeof(uint32_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_HOST, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);
	*static_cast<uint32_t *>(m_countSource.getMappedData()) = m_paddedCount;
	for (uint32_t f = 0; f < MAX_FRAMES_IN_FLIGHT; ++f)
		m_radix.setCountSource(f, m_countSource.get());

	vkResetDescriptorPool(m_vkDevice, m_pool, 0);

	for (auto &frame : m_frames) {
		frame.visibleCount.init(*m_device, m_allocator->get(), sizeof(uint32_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
		frame.indirect.init(*m_device, m_allocator->get(), sizeof(VkDrawIndirectCommand), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);

		VkDescriptorSetLayout layouts[] = { m_keygenSetLayout, m_indirectSetLayout };
		VkDescriptorSet sets[2]{};
		VkDescriptorSetAllocateInfo allocInfo{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.descriptorPool = m_pool,
			.descriptorSetCount = 2,
			.pSetLayouts = layouts,
		};
		if (vkAllocateDescriptorSets(m_vkDevice, &allocInfo, sets) != VK_SUCCESS)
			throw std::runtime_error("Failed to allocate sort descriptor sets");
		frame.keygenSet = sets[0];
		frame.indirectSet = sets[1];
	}
	m_allocated = true;

	writeDescriptorSets(gaussianBuffer, cameraBuffers);
}

void SortPass::writeDescriptorSets(VkBuffer gaussianBuffer, const std::array<VkBuffer, 2> &cameraBuffers) {
	for (uint32_t f = 0; f < MAX_FRAMES_IN_FLIGHT; ++f) {
		FrameResources &frame = m_frames[f];

		VkDescriptorBufferInfo ubo{ cameraBuffers[f], 0, VK_WHOLE_SIZE };
		VkDescriptorBufferInfo gauss{ gaussianBuffer, 0, VK_WHOLE_SIZE };
		VkDescriptorBufferInfo keys{ m_radix.keys(f), 0, VK_WHOLE_SIZE };
		VkDescriptorBufferInfo values{ m_radix.values(f), 0, VK_WHOLE_SIZE };
		VkDescriptorBufferInfo visible{ frame.visibleCount.get(), 0, VK_WHOLE_SIZE };
		VkDescriptorBufferInfo indirect{ frame.indirect.get(), 0, VK_WHOLE_SIZE };

		auto write = [&](VkDescriptorSet set, uint32_t binding, VkDescriptorType type, VkDescriptorBufferInfo *info) {
			VkWriteDescriptorSet w{
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = set,
				.dstBinding = binding,
				.descriptorCount = 1,
				.descriptorType = type,
				.pBufferInfo = info,
			};
			vkUpdateDescriptorSets(m_vkDevice, 1, &w, 0, nullptr);
		};

		write(frame.keygenSet, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &ubo);
		write(frame.keygenSet, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &gauss);
		write(frame.keygenSet, 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &keys);
		write(frame.keygenSet, 3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &values);
		write(frame.keygenSet, 4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &visible);

		write(frame.indirectSet, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &visible);
		write(frame.indirectSet, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &indirect);
	}
}

void SortPass::record(RenderContext &renderCtx, const PassContext &passCtx) {
	if (!m_allocated || m_count == 0)
		return;

	FrameResources &frame = m_frames[passCtx.frameIndex];
	VkCommandBuffer cmd = passCtx.cmd;

	// Reset the visible counter, then make the fill visible to the keygen atomic.
	vkCmdFillBuffer(cmd, frame.visibleCount.get(), 0, sizeof(uint32_t), 0);
	VkMemoryBarrier fillBarrier{
		.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
		.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
	};
	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &fillBarrier, 0, nullptr, 0, nullptr);

	// Depth keys + identity values (with frustum culling).
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_keygenPipeline);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_keygenLayout, 0, 1, &frame.keygenSet, 0, nullptr);
	vkCmdPushConstants(cmd, m_keygenLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t), &m_count);
	vkCmdDispatch(cmd, m_keygenGroups, 1, 1);

	VkMemoryBarrier keyBarrier{
		.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
		.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
	};
	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &keyBarrier, 0, nullptr, 0, nullptr);

	if (renderCtx.profiler)
		renderCtx.profiler->write(cmd, passCtx.frameIndex, GpuSlot::Keygen);

	if (renderCtx.sceneState->sortEnabled)
		m_radix.record(cmd, passCtx.frameIndex, false);

	if (renderCtx.profiler)
		renderCtx.profiler->write(cmd, passCtx.frameIndex, GpuSlot::Radix);

	// Build the indirect draw command from the visible count.
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_indirectPipeline);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_indirectLayout, 0, 1, &frame.indirectSet, 0, nullptr);
	vkCmdDispatch(cmd, 1, 1, 1);

	VkMemoryBarrier barrier{
		.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
		.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT,
	};
	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
	                     VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
	                     0, 1, &barrier, 0, nullptr, 0, nullptr);
}

uint64_t SortPass::getGpuBytesForCount(uint32_t count) {
	if (count == 0)
		return 0;
	uint32_t groupCount = (count + kBlockSize - 1) / kBlockSize;
	uint32_t padded = groupCount * kBlockSize;
	uint64_t elementBytes = static_cast<uint64_t>(padded) * sizeof(uint32_t);
	uint64_t histBytes = static_cast<uint64_t>(groupCount) * RadixSorter::kBuckets * sizeof(uint32_t);
	uint64_t radixPerFrame = 4 * elementBytes + histBytes + static_cast<uint64_t>(RadixSorter::kBuckets) * sizeof(uint32_t);
	uint64_t overheadPerFrame = sizeof(uint32_t) + sizeof(VkDrawIndirectCommand);
	return static_cast<uint64_t>(MAX_FRAMES_IN_FLIGHT) * (radixPerFrame + overheadPerFrame);
}

uint64_t SortPass::getGpuBytes() const {
	return m_allocated ? getGpuBytesForCount(m_count) : 0;
}

void SortPass::shutdown(RenderContext &renderCtx) {
	(void) renderCtx;
	vkDeviceWaitIdle(m_vkDevice);

	releaseFrameBuffers();
	m_radix.shutdown();

	vkDestroyPipeline(m_vkDevice, m_keygenPipeline, nullptr);
	vkDestroyPipeline(m_vkDevice, m_indirectPipeline, nullptr);
	vkDestroyPipelineLayout(m_vkDevice, m_keygenLayout, nullptr);
	vkDestroyPipelineLayout(m_vkDevice, m_indirectLayout, nullptr);
	vkDestroyDescriptorSetLayout(m_vkDevice, m_keygenSetLayout, nullptr);
	vkDestroyDescriptorSetLayout(m_vkDevice, m_indirectSetLayout, nullptr);
	vkDestroyDescriptorPool(m_vkDevice, m_pool, nullptr);
}

}        // namespace renderer
