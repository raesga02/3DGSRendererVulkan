#pragma once

#include <atomic>
#include <cstdint>

namespace gpu::memory {

// Lightweight, non-interfering accounting of GPU memory requested through our
// buffer/image wrappers. No driver queries; just atomics updated on alloc/free.
// This is a prediction from requested sizes (VMA block rounding, ImGui and the
// swapchain are not included).

inline std::atomic<uint64_t> g_bufferBytes{ 0 };
inline std::atomic<uint64_t> g_imageBytes{ 0 };
inline std::atomic<uint64_t> g_allocationCount{ 0 };

inline void addBuffer(uint64_t bytes) {
	g_bufferBytes.fetch_add(bytes, std::memory_order_relaxed);
}
inline void subBuffer(uint64_t bytes) {
	g_bufferBytes.fetch_sub(bytes, std::memory_order_relaxed);
}
inline void addImage(uint64_t bytes) {
	g_imageBytes.fetch_add(bytes, std::memory_order_relaxed);
}
inline void subImage(uint64_t bytes) {
	g_imageBytes.fetch_sub(bytes, std::memory_order_relaxed);
}

inline void addAllocation() {
	g_allocationCount.fetch_add(1, std::memory_order_relaxed);
}
inline void subAllocation() {
	g_allocationCount.fetch_sub(1, std::memory_order_relaxed);
}

inline uint64_t bufferBytes() {
	return g_bufferBytes.load(std::memory_order_relaxed);
}
inline uint64_t imageBytes() {
	return g_imageBytes.load(std::memory_order_relaxed);
}
inline uint64_t totalBytes() {
	return bufferBytes() + imageBytes();
}
inline uint64_t allocations() {
	return g_allocationCount.load(std::memory_order_relaxed);
}

}        // namespace gpu::memory
