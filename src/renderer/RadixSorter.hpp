#pragma once

#include "gpu/Buffer.hpp"

#include <array>
#include <cstdint>
#include <vulkan/vulkan.h>

namespace core {
class Device;
}
namespace gpu {
class Allocator;
}
namespace renderer {
struct RenderContext;
}

namespace renderer {

// Reusable GPU LSD radix sort (4 x 8-bit passes) over (key, value) pairs.
// Owns the per-frame ping-pong buffers, histograms and pipelines. The caller
// fills keys()/values() then calls record(); after record the sorted data is
// back in keys()/values() (4 passes = even).
//
// The number of elements to sort is read from a caller-provided count buffer
// (setCountSource) so the passes can be dispatched indirectly, avoiding work
// over unused capacity.
class RadixSorter {
  public:
	static constexpr uint32_t kWorkgroupSize = 256;
	static constexpr uint32_t kItemsPerThread = 8;
	static constexpr uint32_t kBlockSize = kWorkgroupSize * kItemsPerThread;        // 2048
	static constexpr uint32_t kPasses = 8;        // 8 x 4-bit digits
	static constexpr uint32_t kBuckets = 16;

	void init(core::Device &device, gpu::Allocator &allocator, RenderContext &renderCtx, uint32_t framesInFlight);
	void shutdown();

	// (Re)allocates all buffers for up to maxElements pairs.
	void resize(uint32_t maxElements);

	// Points the sorter at the buffer holding the element count (uint).
	void setCountSource(uint32_t frameIndex, VkBuffer countBuffer);

	// clearKeys: fill keys() with 0xFFFFFFFF first (needed when the caller only
	// writes [0, count) of a fixed-size buffer).
	void record(VkCommandBuffer cmd, uint32_t frameIndex, bool clearKeys);

	VkBuffer keys(uint32_t frameIndex) const {
		return m_frames[frameIndex].keys[0].get();
	}
	VkBuffer values(uint32_t frameIndex) const {
		return m_frames[frameIndex].values[0].get();
	}

	uint64_t gpuBytes() const;
	bool allocated() const {
		return m_allocated;
	}

  private:
	struct FrameResources {
		gpu::Buffer keys[2];
		gpu::Buffer values[2];
		gpu::Buffer blockHist;
		gpu::Buffer globalEx;
		gpu::Buffer dispatchArgs;

		VkDescriptorSet histSet[2]{};
		VkDescriptorSet scanSet = VK_NULL_HANDLE;
		VkDescriptorSet scatterSet[2]{};
		VkDescriptorSet dispatchSet = VK_NULL_HANDLE;
	};

	void createLayouts(RenderContext &renderCtx);
	void createPipelines(RenderContext &renderCtx);
	VkPipeline createComputePipeline(RenderContext &renderCtx, const char *spvPath, VkPipelineLayout layout);
	void bind();
	void writeDispatchSet(uint32_t frameIndex, VkBuffer countBuffer);

  private:
	VkDevice m_vkDevice = VK_NULL_HANDLE;
	core::Device *m_device = nullptr;
	gpu::Allocator *m_allocator = nullptr;
	uint32_t m_framesInFlight = 2;

	VkDescriptorSetLayout m_histSetLayout = VK_NULL_HANDLE;
	VkDescriptorSetLayout m_scanSetLayout = VK_NULL_HANDLE;
	VkDescriptorSetLayout m_scatterSetLayout = VK_NULL_HANDLE;
	VkDescriptorSetLayout m_dispatchSetLayout = VK_NULL_HANDLE;

	VkPipelineLayout m_histLayout = VK_NULL_HANDLE;
	VkPipelineLayout m_scanLayout = VK_NULL_HANDLE;
	VkPipelineLayout m_scatterLayout = VK_NULL_HANDLE;
	VkPipelineLayout m_dispatchLayout = VK_NULL_HANDLE;

	VkPipeline m_histPipeline = VK_NULL_HANDLE;
	VkPipeline m_scanPipeline = VK_NULL_HANDLE;
	VkPipeline m_scatterPipeline = VK_NULL_HANDLE;
	VkPipeline m_dispatchPipeline = VK_NULL_HANDLE;

	VkDescriptorPool m_pool = VK_NULL_HANDLE;

	std::array<FrameResources, 2> m_frames;

	uint32_t m_maxElements = 0;
	uint32_t m_groupCount = 0;
	bool m_allocated = false;
};

}        // namespace renderer
