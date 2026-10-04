#pragma once

#include "Pass.hpp"
#include <vulkan/vulkan.h>

struct GLFWwindow;

namespace core {
class Device;
class VulkanContext;
struct SwapchainInfo;
}

namespace gpu {
class Allocator;
}

namespace renderer {

struct SceneState;
struct RenderStats;

// Dear ImGui overlay. Phase 0 exposes background controls and frame stats;
// later phases add splat/sort/debug controls.
class GUIPass : public Pass {
  public:
	GUIPass() = default;
	~GUIPass() = default;

	void init(RenderContext &renderCtx) override;
	void shutdown(RenderContext &renderCtx) override;
	void record(RenderContext &renderCtx, const PassContext &passCtx) override;

	// Requires the window/context/allocator, so it is separate from init().
	void initVulkan(GLFWwindow *window, core::VulkanContext &vkContext, core::Device &device, gpu::Allocator &allocator, const core::SwapchainInfo &swapchainInfo, SceneState &sceneState);

	void newFrame(const RenderStats &stats);

	bool wantsCaptureMouse() const;
	bool wantsCaptureKeyboard() const;

  private:
	VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
	SceneState *m_sceneState = nullptr;
};

}        // namespace renderer
