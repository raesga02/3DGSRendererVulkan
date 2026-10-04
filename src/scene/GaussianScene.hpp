#pragma once

#include "gpu/Buffer.hpp"
#include "io/GpuSplatData.hpp"
#include "io/PlyLoader.hpp"
#include "scene/GaussianData.hpp"

#include <string>
#include <vector>
#include <vulkan/vulkan.h>

namespace core {
class Device;
}

namespace gpu {
class Allocator;
}

namespace scene {

// Owns a loaded splat scene on the CPU and its packed GPU buffer.
class GaussianScene {
  public:
	void init(core::Device &device, gpu::Allocator &allocator, const std::string &plyPath);
	void shutdown(gpu::Allocator &allocator);

	bool isLoaded() const {
		return m_loaded;
	}
	uint32_t getCount() const {
		return m_metadata.count;
	}
	VkBuffer getGaussianBuffer() const {
		return m_gaussianBuffer.get();
	}
	uint64_t getGpuBytes() const {
		return static_cast<uint64_t>(m_metadata.count) * sizeof(io::GpuGaussian);
	}
	const io::SceneMetadata &getMetadata() const {
		return m_metadata;
	}
	const std::vector<GaussianData> &getCpuGaussians() const {
		return m_cpu;
	}

  private:
	core::Device *m_device = nullptr;
	gpu::Buffer m_gaussianBuffer;
	std::vector<GaussianData> m_cpu;
	io::SceneMetadata m_metadata;
	bool m_loaded = false;
};

}        // namespace scene
