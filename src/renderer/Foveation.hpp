#pragma once

#include "gpu/Image.hpp"

#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>
#include <vulkan/vulkan.h>

namespace core {
class Device;
}

namespace gpu {
class Allocator;
}

namespace renderer {

// Fixed-foveated fragment shading rate map for the splat pass.
//
// Each eye gets a small R8_UINT image whose texels encode the shading rate
// (1x1, 2x2, 4x4). The high-rate region is centred on that eye's optical axis
// and stays fixed to the eye image (head-locked, not gaze-tracked).
class Foveation {
  public:
	void init(core::Device &device, gpu::Allocator &allocator, VkExtent2D viewExtent,
	          const glm::mat4 &leftProjection, const glm::mat4 &rightProjection, uint32_t peripheralRate);
	void shutdown();

	bool valid() const {
		return m_valid;
	}
	VkImageView view(uint32_t eye) const {
		return eye < m_images.size() ? m_images[eye].getView() : VK_NULL_HANDLE;
	}
	VkExtent2D texelSize() const {
		return m_texelSize;
	}
	VkImageLayout layout() const {
		return VK_IMAGE_LAYOUT_FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR;
	}

  private:
	std::vector<uint8_t> buildRateMap(VkExtent2D viewExtent, const glm::mat4 &projection, uint32_t peripheralRate) const;

  private:
	core::Device *m_device = nullptr;
	gpu::Allocator *m_allocator = nullptr;

	std::array<gpu::Image, 2> m_images;
	VkExtent2D m_texelSize{ 1, 1 };
	VkExtent2D m_rateExtent{};
	bool m_valid = false;
};

}        // namespace renderer
