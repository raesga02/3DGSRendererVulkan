#pragma once

#include "Pass.hpp"
#include <array>
#include <vulkan/vulkan.h>

namespace renderer {

// Instanced-quad Gaussian splatting. Each Gaussian is drawn as a screen-space
// quad; the vertex shader performs the EWA projection and the fragment shader
// evaluates the Gaussian falloff. Gaussians are drawn back-to-front from a
// sorted index buffer.
class RasterizePass : public Pass {
  public:
	void init(RenderContext &renderCtx) override;
	void shutdown(RenderContext &renderCtx) override;
	void record(RenderContext &renderCtx, const PassContext &passCtx) override;

	// Points the descriptor sets at the current scene's buffers.
	void bind(VkBuffer gaussianBuffer,
	          const std::array<VkBuffer, 2> &indexBuffers,
	          const std::array<VkBuffer, 2> &cameraBuffers,
	          const std::array<VkBuffer, 2> &indirectBuffers,
	          uint32_t instanceCount);

  private:
	void createDescriptorSetLayout(RenderContext &renderCtx);
	void createDescriptorPool(RenderContext &renderCtx);
	void createDescriptorSets(RenderContext &renderCtx);
	void createPipelineLayout(RenderContext &renderCtx);
	void createPipeline(RenderContext &renderCtx);

  private:
	VkDevice m_device = VK_NULL_HANDLE;
	VkDescriptorSetLayout m_setLayout = VK_NULL_HANDLE;
	VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
	std::array<VkDescriptorSet, 2> m_sets{};

	VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
	VkPipeline m_pipeline = VK_NULL_HANDLE;

	std::array<VkBuffer, 2> m_indirectBuffers{};
	uint32_t m_instanceCount = 0;
};

}        // namespace renderer
