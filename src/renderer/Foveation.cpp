#include "Foveation.hpp"

#include "core/Device.hpp"
#include "gpu/Allocator.hpp"
#include "gpu/Buffer.hpp"
#include "utils/Log.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace renderer {

namespace {

// Fragment shading rate attachment texel encoding:
//   value = (log2(width) << 2) | log2(height)
// 1x1 = 0, 2x2 = 5, 4x4 = 10.
constexpr uint8_t kRate1x1 = 0;
constexpr uint8_t kRate2x2 = (1u << 2) | 1u;
constexpr uint8_t kRate4x4 = (2u << 2) | 2u;

}        // namespace

void Foveation::init(core::Device &device, gpu::Allocator &allocator, VkExtent2D viewExtent,
                     const glm::mat4 &leftProjection, const glm::mat4 &rightProjection,
                     uint32_t peripheralRate, float foveaRadius, float ringWidth) {
	m_device = &device;
	m_allocator = &allocator;
	m_viewExtent = viewExtent;
	m_peripheralRate = peripheralRate;
	m_foveaRadius = std::clamp(foveaRadius, 0.10f, 0.90f);
	// The 2x2 ring spans `ringWidth` (fraction of half the viewport) beyond the
	// 1x1 fovea; everything outside it is 4x4 for peripheralRate=4.
	m_midRadius = std::clamp(m_foveaRadius + std::max(0.05f, ringWidth), m_foveaRadius + 0.05f, 2.0f);
	m_halfExtent = 0.5f * static_cast<float>(std::min(viewExtent.width, viewExtent.height));
	if (m_halfExtent <= 0.0f)
		m_halfExtent = 1.0f;

	auto centerPx = [&](const glm::mat4 &projection) {
		const glm::vec2 centerNdc(-projection[2][0], -projection[2][1]);
		return (centerNdc * 0.5f + 0.5f) * glm::vec2(viewExtent.width, viewExtent.height);
	};
	m_foveaCenterPx[0] = centerPx(leftProjection);
	m_foveaCenterPx[1] = centerPx(rightProjection);

	VkPhysicalDeviceFragmentShadingRatePropertiesKHR properties{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_PROPERTIES_KHR};
	VkPhysicalDeviceProperties2 properties2{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
		.pNext = &properties};
	vkGetPhysicalDeviceProperties2(device.getPhysicalDevice(), &properties2);

	m_texelSize = properties.maxFragmentShadingRateAttachmentTexelSize;
	if (m_texelSize.width == 0 || m_texelSize.height == 0)
		m_texelSize = { 1, 1 };

	m_rateExtent = {
		(viewExtent.width + m_texelSize.width - 1) / m_texelSize.width,
		(viewExtent.height + m_texelSize.height - 1) / m_texelSize.height,
	};

	for (auto &image : m_images) {
		image.init(device, allocator.get(), m_rateExtent, VK_FORMAT_R8_UINT,
		           VK_IMAGE_USAGE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
		           VK_IMAGE_ASPECT_COLOR_BIT);
	}

	const std::vector<uint8_t> leftMap = buildRateMap(m_foveaCenterPx[0], peripheralRate);
	const std::vector<uint8_t> rightMap = buildRateMap(m_foveaCenterPx[1], peripheralRate);
	const VkDeviceSize mapBytes = static_cast<VkDeviceSize>(leftMap.size());

	gpu::Buffer staging;
	staging.init(device, allocator.get(), mapBytes * 2, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
	             VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
	             VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);
	std::memcpy(staging.getMappedData(), leftMap.data(), static_cast<size_t>(mapBytes));
	std::memcpy(static_cast<uint8_t *>(staging.getMappedData()) + mapBytes, rightMap.data(), static_cast<size_t>(mapBytes));

	VkCommandBuffer cmd = device.beginSingleTimeCommands();
	for (uint32_t eye = 0; eye < m_images.size(); ++eye) {
		gpu::Image &image = m_images[eye];
		image.transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

		VkBufferImageCopy region{
			.bufferOffset = mapBytes * eye,
			.bufferRowLength = 0,
			.bufferImageHeight = 0,
			.imageSubresource = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1 },
			.imageOffset = { 0, 0, 0 },
			.imageExtent = { m_rateExtent.width, m_rateExtent.height, 1 },
		};
		vkCmdCopyBufferToImage(cmd, staging.get(), image.get(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
		image.transition(cmd, VK_IMAGE_LAYOUT_FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR);
	}
	device.endSingleTimeCommands(cmd);

	staging.shutdown(allocator.get());
	m_valid = true;

	utils::logInfo("Fixed foveation: {}x{} view, {}x{} texels ({}x{} texel), fovea radius {:.2f}, mid {:.2f}, peripheral rate {}x{}",
	               viewExtent.width, viewExtent.height, m_rateExtent.width, m_rateExtent.height,
	               m_texelSize.width, m_texelSize.height, m_foveaRadius, m_midRadius, peripheralRate, peripheralRate);
}

std::vector<uint8_t> Foveation::buildRateMap(const glm::vec2 &centerPx, uint32_t peripheralRate) const {
	std::vector<uint8_t> map(static_cast<size_t>(m_rateExtent.width) * m_rateExtent.height);

	for (uint32_t y = 0; y < m_rateExtent.height; ++y) {
		for (uint32_t x = 0; x < m_rateExtent.width; ++x) {
			const glm::vec2 texelCenter(
			    (static_cast<float>(x) + 0.5f) * m_texelSize.width,
			    (static_cast<float>(y) + 0.5f) * m_texelSize.height);
			const float radius = glm::length(texelCenter - centerPx) / m_halfExtent;

			uint8_t rate = kRate1x1;
			if (radius > m_foveaRadius)
				rate = (radius > m_midRadius && peripheralRate >= 4) ? kRate4x4 : kRate2x2;

			map[static_cast<size_t>(y) * m_rateExtent.width + x] = rate;
		}
	}

	return map;
}

void Foveation::shutdown() {
	if (!m_valid)
		return;

	for (auto &image : m_images)
		image.shutdown(m_allocator->get());

	m_valid = false;
	m_device = nullptr;
	m_allocator = nullptr;
}

}        // namespace renderer
