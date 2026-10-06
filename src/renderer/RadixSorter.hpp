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
class RadixSorter {
  public:
	static constexpr uint32_t kWorkgroupSize = 256;
	static constexpr uint32_t kItemsPerThread = 8;
	static constexpr uint32_t kBlockSize = kWorkgroupSize * kItemsPerThread;        // 2048
	static constexpr uint32_t kPasses = 4;
	static constexpr uint32_t kBuckets = 256;

	void init(core::Device &device, gpu::Allocator &allocator, RenderContext &renderCtx, uint32_t framesInFlight);
	void shutdown();

	// (Re)allocates all buffers for up to maxElements pairs and (re)binds sets.
	void resize(uint32_t maxElements);
	void bind();

	// elementCount must be <= the size passed to resize. When clearKeys is set,
	// keys() is filled with 0xFFFFFFFF first (needed when the caller writes only
	// [0, elementCount) of a fixed-size buffer).
	void record(VkCommandBuffer cmd, uint32_t frameIndex, uint32_t elementCount, bool clearKeys);

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
	uint32_t groupCount() const {
		return m_groupCount;
	}

  private:
	struct FrameResources {
		gpu::Buffer keys[2];
		gpu::Buffer values[2];
		gpu::Buffer blockHist;
		gpu::Buffer globalEx;

		VkDescriptorSet histSet[2]{};
		VkDescriptorSet scanSet = VK_NULL_HANDLE;
		VkDescriptorSet scatterSet[2]{};
	};

	void createLayouts(RenderContext &renderCtx);
	void createPipelines(RenderContext &renderCtx);
	VkPipeline createComputePipeline(RenderContext &renderCtx, const char *spvPath, VkPipelineLayout layout);

  private:
	VkDevice m_vkDevice = VK_NULL_HANDLE;
	core::Device *m_device = nullptr;
	gpu::Allocator *m_allocator = nullptr;
	uint32_t m_framesInFlight = 2;

	VkDescriptorSetLayout m_histSetLayout = VK_NULL_HANDLE;
	VkDescriptorSetLayout m_scanSetLayout = VK_NULL_HANDLE;
	VkDescriptorSetLayout m_scatterSetLayout = VK_NULL_HANDLE;

	VkPipelineLayout m_histLayout = VK_NULL_HANDLE;
	VkPipelineLayout m_scanLayout = VK_NULL_HANDLE;
	VkPipelineLayout m_scatterLayout = VK_NULL_HANDLE;

	VkPipeline m_histPipeline = VK_NULL_HANDLE;
	VkPipeline m_scanPipeline = VK_NULL_HANDLE;
	VkPipeline m_scatterPipeline = VK_NULL_HANDLE;

	VkDescriptorPool m_pool = VK_NULL_HANDLE;

	std::array<FrameResources, 2> m_frames;

	uint32_t m_maxElements = 0;
	uint32_t m_groupCount = 0;
	bool m_allocated = false;
};

}        // namespace renderer
