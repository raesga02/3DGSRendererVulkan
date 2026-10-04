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

void computeBarrier(VkCommandBuffer cmd) {
	VkMemoryBarrier barrier{
		.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
		.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
	};
	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
}

}        // namespace

void SortPass::init(RenderContext &renderCtx) {
	m_device = renderCtx.device;
	m_vkDevice = renderCtx.device->get();
	m_allocator = renderCtx.allocator;

	createLayouts(renderCtx);
	createPipelines(renderCtx);

	VkDescriptorPoolSize poolSizes[] = {
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2 * MAX_FRAMES_IN_FLIGHT },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 64 },
	};
	VkDescriptorPoolCreateInfo poolInfo{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
		.maxSets = 8 * MAX_FRAMES_IN_FLIGHT,
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
	});

	m_histSetLayout = makeSetLayout(m_vkDevice, {
	    { .binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = cs },
	    { .binding = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = cs },
	});

	m_scanSetLayout = makeSetLayout(m_vkDevice, {
	    { .binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = cs },
	    { .binding = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = cs },
	});

	m_scatterSetLayout = makeSetLayout(m_vkDevice, {
	    { .binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = cs },
	    { .binding = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = cs },
	    { .binding = 2, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = cs },
	    { .binding = 3, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = cs },
	    { .binding = 4, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = cs },
	    { .binding = 5, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = cs },
	});

	m_keygenLayout = makePipelineLayout(m_vkDevice, m_keygenSetLayout);
	m_histLayout = makePipelineLayout(m_vkDevice, m_histSetLayout);
	m_scanLayout = makePipelineLayout(m_vkDevice, m_scanSetLayout);
	m_scatterLayout = makePipelineLayout(m_vkDevice, m_scatterSetLayout);
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
	m_histPipeline = createComputePipeline(renderCtx, "shaders/radix_histogram.comp.spv", m_histLayout);
	m_scanPipeline = createComputePipeline(renderCtx, "shaders/radix_scan.comp.spv", m_scanLayout);
	m_scatterPipeline = createComputePipeline(renderCtx, "shaders/radix_scatter.comp.spv", m_scatterLayout);
}

void SortPass::releaseFrameBuffers() {
	if (!m_allocated)
		return;
	for (auto &frame : m_frames) {
		for (auto &buffer : frame.keys)
			buffer.shutdown(m_allocator->get());
		for (auto &buffer : frame.values)
			buffer.shutdown(m_allocator->get());
		frame.blockHist.shutdown(m_allocator->get());
		frame.globalEx.shutdown(m_allocator->get());
	}
	m_allocated = false;
}

void SortPass::bind(VkBuffer gaussianBuffer, const std::array<VkBuffer, 2> &cameraBuffers, uint32_t count) {
	vkDeviceWaitIdle(m_vkDevice);
	releaseFrameBuffers();

	m_count = count;
	if (count == 0)
		return;

	m_groupCount = (count + kBlockSize - 1) / kBlockSize;
	m_paddedCount = m_groupCount * kBlockSize;
	m_keygenGroups = m_paddedCount / kWorkgroupSize;

	VkDeviceSize elementBytes = static_cast<VkDeviceSize>(m_paddedCount) * sizeof(uint32_t);
	VkDeviceSize histBytes = static_cast<VkDeviceSize>(m_groupCount) * kBuckets * sizeof(uint32_t);

	// Reset the descriptor pool and allocate fresh sets for this scene.
	vkResetDescriptorPool(m_vkDevice, m_pool, 0);

	// Allocate buffers (device-local storage).
	for (auto &frame : m_frames) {
		for (auto &buffer : frame.keys)
			buffer.init(*m_device, m_allocator->get(), elementBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
		for (auto &buffer : frame.values)
			buffer.init(*m_device, m_allocator->get(), elementBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
		frame.blockHist.init(*m_device, m_allocator->get(), histBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
		frame.globalEx.init(*m_device, m_allocator->get(), kBuckets * sizeof(uint32_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
	}
	m_allocated = true;

	// Allocate descriptor sets.
	for (auto &frame : m_frames) {
		VkDescriptorSetLayout layouts[] = {
			m_keygenSetLayout, m_histSetLayout, m_histSetLayout,
			m_scanSetLayout, m_scatterSetLayout, m_scatterSetLayout,
		};
		VkDescriptorSet sets[6]{};
		VkDescriptorSetAllocateInfo allocInfo{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.descriptorPool = m_pool,
			.descriptorSetCount = 6,
			.pSetLayouts = layouts,
		};
		if (vkAllocateDescriptorSets(m_vkDevice, &allocInfo, sets) != VK_SUCCESS)
			throw std::runtime_error("Failed to allocate sort descriptor sets");

		frame.keygenSet = sets[0];
		frame.histSet[0] = sets[1];
		frame.histSet[1] = sets[2];
		frame.scanSet = sets[3];
		frame.scatterSet[0] = sets[4];
		frame.scatterSet[1] = sets[5];
	}

	writeDescriptorSets(gaussianBuffer, cameraBuffers);
}

void SortPass::writeDescriptorSets(VkBuffer gaussianBuffer, const std::array<VkBuffer, 2> &cameraBuffers) {
	for (uint32_t f = 0; f < MAX_FRAMES_IN_FLIGHT; ++f) {
		FrameResources &frame = m_frames[f];

		VkDescriptorBufferInfo ubo{ cameraBuffers[f], 0, VK_WHOLE_SIZE };
		VkDescriptorBufferInfo gauss{ gaussianBuffer, 0, VK_WHOLE_SIZE };
		VkDescriptorBufferInfo k0{ frame.keys[0].get(), 0, VK_WHOLE_SIZE };
		VkDescriptorBufferInfo k1{ frame.keys[1].get(), 0, VK_WHOLE_SIZE };
		VkDescriptorBufferInfo v0{ frame.values[0].get(), 0, VK_WHOLE_SIZE };
		VkDescriptorBufferInfo v1{ frame.values[1].get(), 0, VK_WHOLE_SIZE };
		VkDescriptorBufferInfo hist{ frame.blockHist.get(), 0, VK_WHOLE_SIZE };
		VkDescriptorBufferInfo gex{ frame.globalEx.get(), 0, VK_WHOLE_SIZE };

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
		write(frame.keygenSet, 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &k0);
		write(frame.keygenSet, 3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &v0);

		write(frame.histSet[0], 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &k0);
		write(frame.histSet[0], 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &hist);
		write(frame.histSet[1], 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &k1);
		write(frame.histSet[1], 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &hist);

		write(frame.scanSet, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &hist);
		write(frame.scanSet, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &gex);

		write(frame.scatterSet[0], 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &k0);
		write(frame.scatterSet[0], 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &v0);
		write(frame.scatterSet[0], 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &k1);
		write(frame.scatterSet[0], 3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &v1);
		write(frame.scatterSet[0], 4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &hist);
		write(frame.scatterSet[0], 5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &gex);

		write(frame.scatterSet[1], 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &k1);
		write(frame.scatterSet[1], 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &v1);
		write(frame.scatterSet[1], 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &k0);
		write(frame.scatterSet[1], 3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &v0);
		write(frame.scatterSet[1], 4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &hist);
		write(frame.scatterSet[1], 5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &gex);
	}
}

void SortPass::record(RenderContext &renderCtx, const PassContext &passCtx) {
	if (!m_allocated || m_count == 0)
		return;

	FrameResources &frame = m_frames[passCtx.frameIndex];
	VkCommandBuffer cmd = passCtx.cmd;

	// 1) Depth keys + identity values.
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_keygenPipeline);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_keygenLayout, 0, 1, &frame.keygenSet, 0, nullptr);
	vkCmdPushConstants(cmd, m_keygenLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t), &m_count);
	vkCmdDispatch(cmd, m_keygenGroups, 1, 1);
	computeBarrier(cmd);

	if (renderCtx.profiler)
		renderCtx.profiler->write(cmd, passCtx.frameIndex, GpuSlot::Keygen);

	// 2) Four stable radix passes (skipped when sorting is disabled; values
	//    then stay as the identity permutation from keygen).
	if (renderCtx.sceneState->sortEnabled) {
		for (uint32_t pass = 0; pass < kRadixPasses; ++pass) {
			uint32_t shift = pass * 8u;
			uint32_t src = pass % 2u;

			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_histPipeline);
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_histLayout, 0, 1, &frame.histSet[src], 0, nullptr);
			vkCmdPushConstants(cmd, m_histLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t), &shift);
			vkCmdDispatch(cmd, m_groupCount, 1, 1);
			computeBarrier(cmd);

			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_scanPipeline);
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_scanLayout, 0, 1, &frame.scanSet, 0, nullptr);
			vkCmdPushConstants(cmd, m_scanLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t), &m_groupCount);
			vkCmdDispatch(cmd, 1, 1, 1);
			computeBarrier(cmd);

			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_scatterPipeline);
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_scatterLayout, 0, 1, &frame.scatterSet[src], 0, nullptr);
			vkCmdPushConstants(cmd, m_scatterLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t), &shift);
			vkCmdDispatch(cmd, m_groupCount, 1, 1);
			computeBarrier(cmd);
		}
	}

	if (renderCtx.profiler)
		renderCtx.profiler->write(cmd, passCtx.frameIndex, GpuSlot::Radix);

	// Make the sorted indices visible to the rasterizer's vertex shader.
	VkMemoryBarrier barrier{
		.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
		.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
	};
	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
}

uint64_t SortPass::getGpuBytesForCount(uint32_t count) {
	if (count == 0)
		return 0;

	uint32_t groupCount = (count + kBlockSize - 1) / kBlockSize;
	uint32_t paddedCount = groupCount * kBlockSize;

	uint64_t elementBytes = static_cast<uint64_t>(paddedCount) * sizeof(uint32_t);
	uint64_t histBytes = static_cast<uint64_t>(groupCount) * kBuckets * sizeof(uint32_t);

	// Per frame: keys[2] + values[2] + blockHist + globalEx.
	uint64_t perFrame = 4 * elementBytes + histBytes + static_cast<uint64_t>(kBuckets) * sizeof(uint32_t);
	return static_cast<uint64_t>(MAX_FRAMES_IN_FLIGHT) * perFrame;
}

uint64_t SortPass::getGpuBytes() const {
	return m_allocated ? getGpuBytesForCount(m_count) : 0;
}

void SortPass::shutdown(RenderContext &renderCtx) {
	(void) renderCtx;
	vkDeviceWaitIdle(m_vkDevice);

	releaseFrameBuffers();

	vkDestroyPipeline(m_vkDevice, m_keygenPipeline, nullptr);
	vkDestroyPipeline(m_vkDevice, m_histPipeline, nullptr);
	vkDestroyPipeline(m_vkDevice, m_scanPipeline, nullptr);
	vkDestroyPipeline(m_vkDevice, m_scatterPipeline, nullptr);

	vkDestroyPipelineLayout(m_vkDevice, m_keygenLayout, nullptr);
	vkDestroyPipelineLayout(m_vkDevice, m_histLayout, nullptr);
	vkDestroyPipelineLayout(m_vkDevice, m_scanLayout, nullptr);
	vkDestroyPipelineLayout(m_vkDevice, m_scatterLayout, nullptr);

	vkDestroyDescriptorSetLayout(m_vkDevice, m_keygenSetLayout, nullptr);
	vkDestroyDescriptorSetLayout(m_vkDevice, m_histSetLayout, nullptr);
	vkDestroyDescriptorSetLayout(m_vkDevice, m_scanSetLayout, nullptr);
	vkDestroyDescriptorSetLayout(m_vkDevice, m_scatterSetLayout, nullptr);

	vkDestroyDescriptorPool(m_vkDevice, m_pool, nullptr);
}

}        // namespace renderer
