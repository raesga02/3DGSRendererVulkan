#pragma once

#include "Pass.hpp"
#include "gpu/Buffer.hpp"
#include "renderer/RadixSorter.hpp"

#include <array>
#include <cstdint>
#include <vulkan/vulkan.h>

namespace core {
class Device;
}

namespace renderer {

// Computes per-Gaussian depth keys (with frustum culling), counts the visible
// ones, sorts them back-to-front via RadixSorter, and builds the indirect draw
// command. Double-buffered per frame-in-flight.
class SortPass : public Pass {
  public:
	void init(RenderContext &renderCtx) override;
	void shutdown(RenderContext &renderCtx) override;

	void record(RenderContext &renderCtx, const PassContext &passCtx) override;

	// (Re)allocates the per-frame sort resources and descriptor sets.
	void bind(VkBuffer gaussianBuffer, const std::array<VkBuffer, 2> &cameraBuffers, uint32_t count);

	VkBuffer getSortedIndices(uint32_t frameIndex) const {
		return m_radix.values(frameIndex);
	}
	VkBuffer getIndirectBuffer(uint32_t frameIndex) const {
		return m_frames[frameIndex].indirect.get();
	}

	static uint64_t getGpuBytesForCount(uint32_t count);
	uint64_t getGpuBytes() const;

  private:
	static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;
	static constexpr uint32_t kBlockSize = RadixSorter::kBlockSize;

	struct FrameResources {
		gpu::Buffer visibleCount;
		gpu::Buffer indirect;

		VkDescriptorSet keygenSet = VK_NULL_HANDLE;
		VkDescriptorSet indirectSet = VK_NULL_HANDLE;
	};

	void createLayouts(RenderContext &renderCtx);
	void createPipelines(RenderContext &renderCtx);
	VkPipeline createComputePipeline(RenderContext &renderCtx, const char *spvPath, VkPipelineLayout layout);
	void releaseFrameBuffers();
	void writeDescriptorSets(VkBuffer gaussianBuffer, const std::array<VkBuffer, 2> &cameraBuffers);

  private:
	VkDevice m_vkDevice = VK_NULL_HANDLE;
	core::Device *m_device = nullptr;
	gpu::Allocator *m_allocator = nullptr;

	RadixSorter m_radix;

	VkDescriptorSetLayout m_keygenSetLayout = VK_NULL_HANDLE;
	VkDescriptorSetLayout m_indirectSetLayout = VK_NULL_HANDLE;
	VkPipelineLayout m_keygenLayout = VK_NULL_HANDLE;
	VkPipelineLayout m_indirectLayout = VK_NULL_HANDLE;
	VkPipeline m_keygenPipeline = VK_NULL_HANDLE;
	VkPipeline m_indirectPipeline = VK_NULL_HANDLE;
	VkDescriptorPool m_pool = VK_NULL_HANDLE;

	std::array<FrameResources, MAX_FRAMES_IN_FLIGHT> m_frames;

	uint32_t m_count = 0;
	uint32_t m_paddedCount = 0;
	uint32_t m_keygenGroups = 0;
	bool m_allocated = false;
};

}        // namespace renderer
