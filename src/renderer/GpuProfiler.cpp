#include "GpuProfiler.hpp"

#include "core/Device.hpp"

#include <stdexcept>

namespace renderer {

void GpuProfiler::init(core::Device &device, uint32_t framesInFlight, uint32_t slotsPerFrame) {
	m_device = device.get();
	m_frames = framesInFlight;
	m_slots = slotsPerFrame;

	VkPhysicalDeviceProperties props{};
	vkGetPhysicalDeviceProperties(device.getPhysicalDevice(), &props);
	m_periodNs = props.limits.timestampPeriod;

	uint32_t familyCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(device.getPhysicalDevice(), &familyCount, nullptr);
	std::vector<VkQueueFamilyProperties> families(familyCount);
	vkGetPhysicalDeviceQueueFamilyProperties(device.getPhysicalDevice(), &familyCount, families.data());

	uint32_t graphicsIndex = device.getGraphicsQueueIndex();
	m_validBits = (graphicsIndex < familyCount) ? families[graphicsIndex].timestampValidBits : 0;

	if (m_periodNs <= 0.0f || m_validBits == 0) {
		// Timestamps unsupported on this queue; stay disabled (no-op).
		return;
	}

	VkQueryPoolCreateInfo poolInfo{
		.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
		.queryType = VK_QUERY_TYPE_TIMESTAMP,
		.queryCount = m_frames * m_slots,
	};
	if (vkCreateQueryPool(m_device, &poolInfo, nullptr, &m_pool) != VK_SUCCESS)
		throw std::runtime_error("Failed to create timestamp query pool");

	m_results.resize(static_cast<size_t>(m_frames) * m_slots);
	m_written.assign(m_frames, false);
}

void GpuProfiler::shutdown() {
	if (m_pool != VK_NULL_HANDLE) {
		vkDestroyQueryPool(m_device, m_pool, nullptr);
		m_pool = VK_NULL_HANDLE;
	}
}

void GpuProfiler::beginFrame(VkCommandBuffer cmd, uint32_t frameIndex) {
	if (m_pool == VK_NULL_HANDLE)
		return;
	vkCmdResetQueryPool(cmd, m_pool, frameIndex * m_slots, m_slots);
}

void GpuProfiler::write(VkCommandBuffer cmd, uint32_t frameIndex, uint32_t slot) {
	if (m_pool == VK_NULL_HANDLE)
		return;
	vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_pool, frameIndex * m_slots + slot);
	m_written[frameIndex] = true;
}

bool GpuProfiler::read(uint32_t frameIndex, std::vector<float> &outMs) {
	if (m_pool == VK_NULL_HANDLE || !m_written[frameIndex])
		return false;

	uint64_t *base = m_results.data() + static_cast<size_t>(frameIndex) * m_slots;
	VkResult result = vkGetQueryPoolResults(
	    m_device, m_pool, frameIndex * m_slots, m_slots,
	    sizeof(uint64_t) * m_slots, base, sizeof(uint64_t),
	    VK_QUERY_RESULT_64_BIT);
	if (result != VK_SUCCESS)
		return false;

	const uint64_t mask = (m_validBits >= 64) ? ~0ull : ((1ull << m_validBits) - 1ull);
	outMs.resize(m_slots - 1);
	for (uint32_t i = 0; i + 1 < m_slots; ++i) {
		uint64_t a = base[i] & mask;
		uint64_t b = base[i + 1] & mask;
		uint64_t delta = (b >= a) ? (b - a) : (mask - a + b);        // handle wrap
		outMs[i] = static_cast<float>(static_cast<double>(delta) * m_periodNs / 1.0e6);
	}
	return true;
}

}        // namespace renderer
