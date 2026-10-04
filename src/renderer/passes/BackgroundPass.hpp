#pragma once

#include "Pass.hpp"
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

namespace renderer {

// Draws a fullscreen gradient into the presentation target and clears it.
// For Phase 0 this is the entire "scene"; once splats land, this becomes the
// background layer behind them.
class BackgroundPass : public Pass {
  public:
	void init(RenderContext &renderCtx) override;
	void shutdown(RenderContext &renderCtx) override;
	void record(RenderContext &renderCtx, const PassContext &passCtx) override;

  private:
	void createPipelineLayout(RenderContext &renderCtx);
	void createPipeline(RenderContext &renderCtx);

  private:
	struct PushConstants {
		glm::vec4 top;
		glm::vec4 bottom;
	};

	VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
	VkPipeline m_pipeline = VK_NULL_HANDLE;
};

}        // namespace renderer
