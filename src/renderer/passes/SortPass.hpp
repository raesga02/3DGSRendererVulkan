#pragma once

#include "Pass.hpp"
#include "gpu/Buffer.hpp"

#include <array>
#include <vulkan/vulkan.h>

namespace core {
class Device;
}

namespace renderer {

// GPU LSD radix sort (4 x 8-bit passes) producing a back-to-front index order.
// Double-buffered per frame-in-flight so consecutive frames never alias.
class SortPass : public Pass {
  public:
	void init(RenderContext &renderCtx) override;
	void shutdown(RenderContext &renderCtx) override;

	// Dispatches key generation + the radix passes for the current frame.
	void record(RenderContext &renderCtx, const PassContext &passCtx) override;

	// (Re)allocates the per-frame sort resources and descriptor sets.
	void bind(VkBuffer gaussianBuffer, const std::array<VkBuffer, 2> &cameraBuffers, uint32_t count);

	// Sorted Gaussian indices for a given frame-in-flight (valid after record()).
	VkBuffer getSortedIndices(uint32_t frameIndex) const {
		return m_frames[frameIndex].values[0].get();
	}

  private:
	static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;
	static constexpr uint32_t kWorkgroupSize = 256;
	static constexpr uint32_t kItemsPerThread = 8;
	static constexpr uint32_t kBlockSize = kWorkgroupSize * kItemsPerThread;        // 2048
	static constexpr uint32_t kRadixPasses = 4;
	static constexpr uint32_t kBuckets = 256;

	struct FrameResources {
		gpu::Buffer keys[2];
		gpu::Buffer values[2];
		gpu::Buffer blockHist;
		gpu::Buffer globalEx;

		VkDescriptorSet keygenSet = VK_NULL_HANDLE;
		VkDescriptorSet histSet[2]{};
		VkDescriptorSet scanSet = VK_NULL_HANDLE;
		VkDescriptorSet scatterSet[2]{};
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

	VkDescriptorSetLayout m_keygenSetLayout = VK_NULL_HANDLE;
	VkDescriptorSetLayout m_histSetLayout = VK_NULL_HANDLE;
	VkDescriptorSetLayout m_scanSetLayout = VK_NULL_HANDLE;
	VkDescriptorSetLayout m_scatterSetLayout = VK_NULL_HANDLE;

	VkPipelineLayout m_keygenLayout = VK_NULL_HANDLE;
	VkPipelineLayout m_histLayout = VK_NULL_HANDLE;
	VkPipelineLayout m_scanLayout = VK_NULL_HANDLE;
	VkPipelineLayout m_scatterLayout = VK_NULL_HANDLE;

	VkPipeline m_keygenPipeline = VK_NULL_HANDLE;
	VkPipeline m_histPipeline = VK_NULL_HANDLE;
	VkPipeline m_scanPipeline = VK_NULL_HANDLE;
	VkPipeline m_scatterPipeline = VK_NULL_HANDLE;

	VkDescriptorPool m_pool = VK_NULL_HANDLE;

	std::array<FrameResources, MAX_FRAMES_IN_FLIGHT> m_frames;

	uint32_t m_count = 0;
	uint32_t m_paddedCount = 0;
	uint32_t m_groupCount = 0;
	uint32_t m_keygenGroups = 0;
	bool m_allocated = false;
};

}        // namespace renderer
