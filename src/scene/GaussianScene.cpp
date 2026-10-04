#include "GaussianScene.hpp"

#include "core/Device.hpp"
#include "gpu/Allocator.hpp"
#include "io/CameraLoader.hpp"
#include "io/GpuSplatData.hpp"
#include "utils/Math.hpp"

#include <glm/gtc/quaternion.hpp>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <stdexcept>

namespace scene {

void GaussianScene::init(core::Device &device, gpu::Allocator &allocator, const std::string &plyPath) {
	m_device = &device;

	m_cpu = io::PlyLoader::load(plyPath, m_metadata);
	if (m_cpu.empty())
		throw std::runtime_error("Loaded PLY contains no Gaussians: " + plyPath);

	// Use the first training camera as the initial view when available.
	{
		std::filesystem::path cameras = std::filesystem::path(plyPath).parent_path() / "cameras.json";
		io::CameraPose pose;
		if (io::loadFirstCamera(cameras.string(), pose)) {
			m_metadata.hasCamera = true;
			m_metadata.cameraPosition = pose.position;
			m_metadata.cameraForward = pose.forward;
			m_metadata.cameraUp = pose.up;
		}
	}

	// Pack to the tightly-laid-out GPU representation.
	std::vector<io::GpuGaussian> packed(m_cpu.size());
	for (size_t i = 0; i < m_cpu.size(); ++i) {
		const GaussianData &g = m_cpu[i];
		io::GpuGaussian &out = packed[i];

		glm::vec3 scale(
		    std::exp(g.scale[0]),
		    std::exp(g.scale[1]),
		    std::exp(g.scale[2]));
		glm::vec4 rotation = glm::normalize(glm::vec4(g.rotation[0], g.rotation[1], g.rotation[2], g.rotation[3]));

		glm::mat3 cov = utils::math::computeCov3D(scale, rotation);

		out.position[0] = g.position[0];
		out.position[1] = g.position[1];
		out.position[2] = g.position[2];

		out.cov3D[0] = cov[0][0];
		out.cov3D[1] = cov[0][1];
		out.cov3D[2] = cov[0][2];
		out.cov3D[3] = cov[1][1];
		out.cov3D[4] = cov[1][2];
		out.cov3D[5] = cov[2][2];

		// SH coefficients, laid out [coeff][channel] with coeff 0 = DC.
		// The PLY stores f_rest as [higher-order coeff][channel].
		for (int c = 0; c < 3; ++c) {
			out.sh[0 * 3 + c] = g.f_dc[c];
			for (int k = 0; k < 15; ++k)
				out.sh[(k + 1) * 3 + c] = g.f_rest[k * 3 + c];
		}

		out.opacity = io::sigmoid(g.opacity);
	}

	VkDeviceSize size = static_cast<VkDeviceSize>(packed.size()) * sizeof(io::GpuGaussian);

	m_gaussianBuffer.init(
	    device, allocator.get(), size,
	    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
	    VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);

	gpu::Buffer staging;
	staging.init(
	    device, allocator.get(), size,
	    VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
	    VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
	    VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);
	std::memcpy(staging.getMappedData(), packed.data(), static_cast<size_t>(size));

	VkCommandBuffer cmd = m_device->beginSingleTimeCommands();
	VkBufferCopy copyRegion{ .srcOffset = 0, .dstOffset = 0, .size = size };
	vkCmdCopyBuffer(cmd, staging.get(), m_gaussianBuffer.get(), 1, &copyRegion);
	m_device->endSingleTimeCommands(cmd);

	staging.shutdown(allocator.get());

	m_loaded = true;
}

void GaussianScene::shutdown(gpu::Allocator &allocator) {
	if (m_loaded) {
		m_gaussianBuffer.shutdown(allocator.get());
		m_loaded = false;
	}
	m_cpu.clear();
}

}        // namespace scene
