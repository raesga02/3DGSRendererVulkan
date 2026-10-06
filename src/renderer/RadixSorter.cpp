#include "RadixSorter.hpp"

#include "core/Device.hpp"
#include "gpu/Allocator.hpp"
#include "renderer/RenderContext.hpp"
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
		throw std::runtime_error("Failed to create radix descriptor set layout");
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
		throw std::runtime_error("Failed to create radix pipeline layout");
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

void RadixSorter::init(core::Device &device, gpu::Allocator &allocator, RenderContext &renderCtx, uint32_t framesInFlight) {
	m_device = &device;
	m_vkDevice = device.get();
	m_allocator = &allocator;
	m_framesInFlight = framesInFlight;

	const VkShaderStageFlags cs = VK_SHADER_STAGE_COMPUTE_BIT;

	m_histSetLayout = makeSetLayout(m_vkDevice, {
	    { .binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = cs },
	    { .binding = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = cs },
	});
	m_scanSetLayout = makeSetLayout(m_vkDevice, {
	    { .binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = cs },
	    { .binding = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = cs },
	    { .binding = 2, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = cs },
	});
	m_scatterSetLayout = makeSetLayout(m_vkDevice, {
	    { .binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = cs },
	    { .binding = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = cs },
	    { .binding = 2, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = cs },
	    { .binding = 3, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = cs },
	    { .binding = 4, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = cs },
	    { .binding = 5, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = cs },
	});
	m_dispatchSetLayout = makeSetLayout(m_vkDevice, {
	    { .binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = cs },
	    { .binding = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = cs },
	});

	m_histLayout = makePipelineLayout(m_vkDevice, m_histSetLayout);
	m_scanLayout = makePipelineLayout(m_vkDevice, m_scanSetLayout);
	m_scatterLayout = makePipelineLayout(m_vkDevice, m_scatterSetLayout);
	m_dispatchLayout = makePipelineLayout(m_vkDevice, m_dispatchSetLayout);

	m_histPipeline = createComputePipeline(renderCtx, "shaders/radix_histogram.comp.spv", m_histLayout);
	m_scanPipeline = createComputePipeline(renderCtx, "shaders/radix_scan.comp.spv", m_scanLayout);
	m_scatterPipeline = createComputePipeline(renderCtx, "shaders/radix_scatter.comp.spv", m_scatterLayout);
	m_dispatchPipeline = createComputePipeline(renderCtx, "shaders/build_dispatch.comp.spv", m_dispatchLayout);

	VkDescriptorPoolSize poolSizes[] = {
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 64 },
	};
	VkDescriptorPoolCreateInfo poolInfo{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
		.maxSets = 8 * m_framesInFlight,
		.poolSizeCount = 1,
		.pPoolSizes = poolSizes,
	};
	if (vkCreateDescriptorPool(m_vkDevice, &poolInfo, nullptr, &m_pool) != VK_SUCCESS)
		throw std::runtime_error("Failed to create radix descriptor pool");
}

VkPipeline RadixSorter::createComputePipeline(RenderContext &renderCtx, const char *spvPath, VkPipelineLayout layout) {
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
		throw std::runtime_error("Failed to create radix compute pipeline");
	utils::shader::destroyShaderModule(*renderCtx.device, module);
	return pipeline;
}

void RadixSorter::resize(uint32_t maxElements) {
	vkDeviceWaitIdle(m_vkDevice);

	if (m_allocated) {
		for (auto &frame : m_frames) {
			for (auto &b : frame.keys)
				b.shutdown(m_allocator->get());
			for (auto &b : frame.values)
				b.shutdown(m_allocator->get());
			frame.blockHist.shutdown(m_allocator->get());
			frame.globalEx.shutdown(m_allocator->get());
			frame.dispatchArgs.shutdown(m_allocator->get());
		}
		m_allocated = false;
	}

	m_maxElements = maxElements;
	if (maxElements == 0)
		return;

	m_groupCount = (maxElements + kBlockSize - 1) / kBlockSize;
	uint32_t padded = m_groupCount * kBlockSize;

	VkDeviceSize elementBytes = static_cast<VkDeviceSize>(padded) * sizeof(uint32_t);
	VkDeviceSize histBytes = static_cast<VkDeviceSize>(m_groupCount) * kBuckets * sizeof(uint32_t);

	vkResetDescriptorPool(m_vkDevice, m_pool, 0);

	for (auto &frame : m_frames) {
		for (auto &b : frame.keys)
			b.init(*m_device, m_allocator->get(), elementBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
		for (auto &b : frame.values)
			b.init(*m_device, m_allocator->get(), elementBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
		frame.blockHist.init(*m_device, m_allocator->get(), histBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
		frame.globalEx.init(*m_device, m_allocator->get(), kBuckets * sizeof(uint32_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
		frame.dispatchArgs.init(*m_device, m_allocator->get(), 4 * sizeof(uint32_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
	}
	m_allocated = true;
	bind();
}

void RadixSorter::bind() {
	for (uint32_t f = 0; f < m_framesInFlight; ++f) {
		FrameResources &frame = m_frames[f];

		VkDescriptorSetLayout layouts[] = {
			m_histSetLayout, m_histSetLayout, m_scanSetLayout, m_scatterSetLayout, m_scatterSetLayout, m_dispatchSetLayout,
		};
		VkDescriptorSet sets[6]{};
		VkDescriptorSetAllocateInfo allocInfo{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.descriptorPool = m_pool,
			.descriptorSetCount = 6,
			.pSetLayouts = layouts,
		};
		if (vkAllocateDescriptorSets(m_vkDevice, &allocInfo, sets) != VK_SUCCESS)
			throw std::runtime_error("Failed to allocate radix descriptor sets");

		frame.histSet[0] = sets[0];
		frame.histSet[1] = sets[1];
		frame.scanSet = sets[2];
		frame.scatterSet[0] = sets[3];
		frame.scatterSet[1] = sets[4];
		frame.dispatchSet = sets[5];

		VkDescriptorBufferInfo k0{ frame.keys[0].get(), 0, VK_WHOLE_SIZE };
		VkDescriptorBufferInfo k1{ frame.keys[1].get(), 0, VK_WHOLE_SIZE };
		VkDescriptorBufferInfo v0{ frame.values[0].get(), 0, VK_WHOLE_SIZE };
		VkDescriptorBufferInfo v1{ frame.values[1].get(), 0, VK_WHOLE_SIZE };
		VkDescriptorBufferInfo hist{ frame.blockHist.get(), 0, VK_WHOLE_SIZE };
		VkDescriptorBufferInfo gex{ frame.globalEx.get(), 0, VK_WHOLE_SIZE };
		VkDescriptorBufferInfo args{ frame.dispatchArgs.get(), 0, VK_WHOLE_SIZE };

		auto write = [&](VkDescriptorSet set, uint32_t binding, VkDescriptorBufferInfo *info) {
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

		write(frame.histSet[0], 0, &k0);
		write(frame.histSet[0], 1, &hist);
		write(frame.histSet[1], 0, &k1);
		write(frame.histSet[1], 1, &hist);
		write(frame.scanSet, 0, &hist);
		write(frame.scanSet, 1, &gex);
		write(frame.scanSet, 2, &args);

		write(frame.scatterSet[0], 0, &k0);
		write(frame.scatterSet[0], 1, &v0);
		write(frame.scatterSet[0], 2, &k1);
		write(frame.scatterSet[0], 3, &v1);
		write(frame.scatterSet[0], 4, &hist);
		write(frame.scatterSet[0], 5, &gex);
		write(frame.scatterSet[1], 0, &k1);
		write(frame.scatterSet[1], 1, &v1);
		write(frame.scatterSet[1], 2, &k0);
		write(frame.scatterSet[1], 3, &v0);
		write(frame.scatterSet[1], 4, &hist);
		write(frame.scatterSet[1], 5, &gex);

		write(frame.dispatchSet, 1, &args);
	}
}

void RadixSorter::writeDispatchSet(uint32_t frameIndex, VkBuffer countBuffer) {
	FrameResources &frame = m_frames[frameIndex];
	VkDescriptorBufferInfo count{ countBuffer, 0, VK_WHOLE_SIZE };
	VkWriteDescriptorSet w{
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = frame.dispatchSet,
		.dstBinding = 0,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		.pBufferInfo = &count,
	};
	vkUpdateDescriptorSets(m_vkDevice, 1, &w, 0, nullptr);
}

void RadixSorter::setCountSource(uint32_t frameIndex, VkBuffer countBuffer) {
	writeDispatchSet(frameIndex, countBuffer);
}

void RadixSorter::record(VkCommandBuffer cmd, uint32_t frameIndex, bool clearKeys) {
	if (!m_allocated)
		return;

	FrameResources &frame = m_frames[frameIndex];

	if (clearKeys) {
		vkCmdFillBuffer(cmd, frame.keys[0].get(), 0, VK_WHOLE_SIZE, 0xFFFFFFFFu);
		VkMemoryBarrier barrier{
			.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
			.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
		};
		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
	}

	// Build the indirect dispatch command from the element count.
	uint32_t blockSize = kBlockSize;
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_dispatchPipeline);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_dispatchLayout, 0, 1, &frame.dispatchSet, 0, nullptr);
	vkCmdPushConstants(cmd, m_dispatchLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t), &blockSize);
	vkCmdDispatch(cmd, 1, 1, 1);

	VkMemoryBarrier argsBarrier{
		.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
		.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT,
	};
	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
	                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
	                     0, 1, &argsBarrier, 0, nullptr, 0, nullptr);

	for (uint32_t pass = 0; pass < kPasses; ++pass) {
		uint32_t shift = pass * 8u;
		uint32_t src = pass % 2u;

		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_histPipeline);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_histLayout, 0, 1, &frame.histSet[src], 0, nullptr);
		vkCmdPushConstants(cmd, m_histLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t), &shift);
		vkCmdDispatchIndirect(cmd, frame.dispatchArgs.get(), 0);
		computeBarrier(cmd);

		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_scanPipeline);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_scanLayout, 0, 1, &frame.scanSet, 0, nullptr);
		vkCmdDispatch(cmd, 1, 1, 1);
		computeBarrier(cmd);

		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_scatterPipeline);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_scatterLayout, 0, 1, &frame.scatterSet[src], 0, nullptr);
		vkCmdPushConstants(cmd, m_scatterLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t), &shift);
		vkCmdDispatchIndirect(cmd, frame.dispatchArgs.get(), 0);
		computeBarrier(cmd);
	}
}

uint64_t RadixSorter::gpuBytes() const {
	if (!m_allocated)
		return 0;
	uint32_t padded = m_groupCount * kBlockSize;
	uint64_t elementBytes = static_cast<uint64_t>(padded) * sizeof(uint32_t);
	uint64_t histBytes = static_cast<uint64_t>(m_groupCount) * kBuckets * sizeof(uint32_t);
	uint64_t perFrame = 4 * elementBytes + histBytes + static_cast<uint64_t>(kBuckets) * sizeof(uint32_t) + 4 * sizeof(uint32_t);
	return static_cast<uint64_t>(m_framesInFlight) * perFrame;
}

void RadixSorter::shutdown() {
	vkDeviceWaitIdle(m_vkDevice);
	if (m_allocated) {
		for (auto &frame : m_frames) {
			for (auto &b : frame.keys)
				b.shutdown(m_allocator->get());
			for (auto &b : frame.values)
				b.shutdown(m_allocator->get());
			frame.blockHist.shutdown(m_allocator->get());
			frame.globalEx.shutdown(m_allocator->get());
			frame.dispatchArgs.shutdown(m_allocator->get());
		}
		m_allocated = false;
	}
	vkDestroyPipeline(m_vkDevice, m_histPipeline, nullptr);
	vkDestroyPipeline(m_vkDevice, m_scanPipeline, nullptr);
	vkDestroyPipeline(m_vkDevice, m_scatterPipeline, nullptr);
	vkDestroyPipeline(m_vkDevice, m_dispatchPipeline, nullptr);
	vkDestroyPipelineLayout(m_vkDevice, m_histLayout, nullptr);
	vkDestroyPipelineLayout(m_vkDevice, m_scanLayout, nullptr);
	vkDestroyPipelineLayout(m_vkDevice, m_scatterLayout, nullptr);
	vkDestroyPipelineLayout(m_vkDevice, m_dispatchLayout, nullptr);
	vkDestroyDescriptorSetLayout(m_vkDevice, m_histSetLayout, nullptr);
	vkDestroyDescriptorSetLayout(m_vkDevice, m_scanSetLayout, nullptr);
	vkDestroyDescriptorSetLayout(m_vkDevice, m_scatterSetLayout, nullptr);
	vkDestroyDescriptorSetLayout(m_vkDevice, m_dispatchSetLayout, nullptr);
	vkDestroyDescriptorPool(m_vkDevice, m_pool, nullptr);
}

}        // namespace renderer
