#pragma once

#include "Pass.hpp"
#include <vulkan/vulkan.h>

namespace renderer {

// Post-processes the offscreen eye image: samples a 3x3 blur and blends it over
// the sharp image with a smooth fovea mask. Fovea stays sharp, the intermediate
// ring lerps toward the blur, and the periphery is fully blurred.
class FoveationBlurPass : public Pass {
  public:
	void init(RenderContext &renderCtx) override;
	void shutdown(RenderContext &renderCtx) override;
	void record(RenderContext &renderCtx, const PassContext &passCtx) override;

  private:
	void createDescriptorSetLayout(RenderContext &renderCtx);
	void createDescriptorPool(RenderContext &renderCtx);
	void createDescriptorSets(RenderContext &renderCtx);
	void createSampler(RenderContext &renderCtx);
	void createPipelineLayout(RenderContext &renderCtx);
	void createPipeline(RenderContext &renderCtx);

  private:
	VkDevice m_device = VK_NULL_HANDLE;
	VkSampler m_sampler = VK_NULL_HANDLE;
	VkDescriptorSetLayout m_setLayout = VK_NULL_HANDLE;
	VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
	VkDescriptorSet m_sets[2]{};
	VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
	VkPipeline m_pipeline = VK_NULL_HANDLE;
};

}        // namespace renderer
