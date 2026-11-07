#pragma once

#include "Pass.hpp"
#include <vulkan/vulkan.h>

namespace renderer {

// Optional debug overlay for fixed foveation. Draws a translucent per-texel
// colour map over the splat image: green = 1x1, yellow = 2x2, red = 4x4.
class FoveationDebugPass : public Pass {
  public:
	void init(RenderContext &renderCtx) override;
	void shutdown(RenderContext &renderCtx) override;
	void record(RenderContext &renderCtx, const PassContext &passCtx) override;

  private:
	void createPipelineLayout(RenderContext &renderCtx);
	void createPipeline(RenderContext &renderCtx);

  private:
	VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
	VkPipeline m_pipeline = VK_NULL_HANDLE;
};

}        // namespace renderer
