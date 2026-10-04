#pragma once

#include <cstdint>
#include <vector>
#include <vulkan/vulkan.h>

namespace core {
class Device;
}

namespace renderer {

// Timestamp slots written once per frame. Durations are measured between
// consecutive slots.
namespace GpuSlot {
enum : uint32_t {
	FrameStart = 0,
	Background,
	Keygen,
	Radix,
	Raster,
	Gui,
	FrameEnd,
	Count,
};
}

// GPU timestamp profiler using a VkQueryPool. Results for a frame are read one
// frame later (after that frame's fence has signalled), so it never stalls.
class GpuProfiler {
  public:
	void init(core::Device &device, uint32_t framesInFlight, uint32_t slotsPerFrame);
	void shutdown();

	bool valid() const {
		return m_pool != VK_NULL_HANDLE;
	}

	// Resets this frame's query slots (call before writing timestamps).
	void beginFrame(VkCommandBuffer cmd, uint32_t frameIndex);
	void write(VkCommandBuffer cmd, uint32_t frameIndex, uint32_t slot);

	// Fills outMs with the duration of each interval [slot, slot+1], i.e.
	// (slotsPerFrame - 1) entries. Call after the frame's fence has signalled.
	bool read(uint32_t frameIndex, std::vector<float> &outMs);

  private:
	VkDevice m_device = VK_NULL_HANDLE;
	VkQueryPool m_pool = VK_NULL_HANDLE;
	uint32_t m_frames = 0;
	uint32_t m_slots = 0;
	float m_periodNs = 1.0f;
	uint32_t m_validBits = 64;
	std::vector<uint64_t> m_results;
	std::vector<bool> m_written;
};

}        // namespace renderer
