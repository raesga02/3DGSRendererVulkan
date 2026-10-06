#pragma once

#include "RadixSorter.hpp"
#include "gpu/Buffer.hpp"
#include "gpu/Image.hpp"

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
struct PassContext;
}

namespace renderer {

// Compute tile-based rasterizer with front-to-back per-pixel early-out.
// Pipeline: project -> bin -> radix sort -> tile ranges -> tile raster ->
// composite to the presentation target.
class TileRasterizer {
  public:
	static constexpr uint32_t kTileSize = 16;
	static constexpr uint32_t kMaxFrames = 2;

	void init(core::Device &device, gpu::Allocator &allocator, RenderContext &renderCtx, uint32_t width, uint32_t height);
	void shutdown();

	void bind(VkBuffer gaussianBuffer, const std::array<VkBuffer, 2> &cameraBuffers, uint32_t count);
	void record(RenderContext &renderCtx, const PassContext &passCtx);

	uint64_t gpuBytes() const;

  private:
	struct FrameResources {
		gpu::Buffer projected;
		gpu::Buffer projectedCount;
		gpu::Buffer entryCount;
		gpu::Buffer tileCounts;
		gpu::Buffer tileStart;
		gpu::Image output;

		VkDescriptorSet projectSet = VK_NULL_HANDLE;
		VkDescriptorSet binSet = VK_NULL_HANDLE;
		VkDescriptorSet histSet = VK_NULL_HANDLE;
		VkDescriptorSet scanSet = VK_NULL_HANDLE;
		VkDescriptorSet rasterSet = VK_NULL_HANDLE;
		VkDescriptorSet compositeSet = VK_NULL_HANDLE;
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

	uint32_t m_width = 0;
	uint32_t m_height = 0;
	uint32_t m_tilesX = 0;
	uint32_t m_tilesY = 0;
	uint32_t m_tileCount = 0;
	uint32_t m_depthBits = 0;
	uint32_t m_count = 0;
	uint32_t m_maxEntries = 0;

	RadixSorter m_radix;

	VkSampler m_sampler = VK_NULL_HANDLE;

	VkDescriptorSetLayout m_projectSetLayout = VK_NULL_HANDLE;
	VkDescriptorSetLayout m_binSetLayout = VK_NULL_HANDLE;
	VkDescriptorSetLayout m_histSetLayout = VK_NULL_HANDLE;
	VkDescriptorSetLayout m_scanSetLayout = VK_NULL_HANDLE;
	VkDescriptorSetLayout m_rasterSetLayout = VK_NULL_HANDLE;
	VkDescriptorSetLayout m_compositeSetLayout = VK_NULL_HANDLE;

	VkPipelineLayout m_projectLayout = VK_NULL_HANDLE;
	VkPipelineLayout m_binLayout = VK_NULL_HANDLE;
	VkPipelineLayout m_histLayout = VK_NULL_HANDLE;
	VkPipelineLayout m_scanLayout = VK_NULL_HANDLE;
	VkPipelineLayout m_rasterLayout = VK_NULL_HANDLE;
	VkPipelineLayout m_compositeLayout = VK_NULL_HANDLE;

	VkPipeline m_projectPipeline = VK_NULL_HANDLE;
	VkPipeline m_binPipeline = VK_NULL_HANDLE;
	VkPipeline m_histPipeline = VK_NULL_HANDLE;
	VkPipeline m_scanPipeline = VK_NULL_HANDLE;
	VkPipeline m_rasterPipeline = VK_NULL_HANDLE;
	VkPipeline m_compositePipeline = VK_NULL_HANDLE;

	VkDescriptorPool m_pool = VK_NULL_HANDLE;

	std::array<FrameResources, kMaxFrames> m_frames;
	bool m_allocated = false;
};

}        // namespace renderer
