#include "Shader.hpp"
#include "core/Device.hpp"

#include <fstream>
#include <stdexcept>
#include <vector>

namespace utils::shader {

static std::vector<char> readFile(const std::string &filename) {
	std::ifstream file(filename, std::ios::ate | std::ios::binary);
	if (!file.is_open())
		throw std::runtime_error("Failed to open shader file: " + filename);

	size_t fileSize = (size_t) file.tellg();
	std::vector<char> buffer(fileSize);

	file.seekg(0);
	file.read(buffer.data(), fileSize);
	file.close();

	return buffer;
}

VkShaderModule createShaderModule(core::Device &device, const std::string &path) {
	auto code = readFile(path);

	VkShaderModuleCreateInfo createInfo{
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = code.size(),
		.pCode = reinterpret_cast<const uint32_t *>(code.data())
	};

	VkShaderModule shaderModule;
	if (vkCreateShaderModule(device.get(), &createInfo, nullptr, &shaderModule) != VK_SUCCESS)
		throw std::runtime_error("Failed to create shader module");

	return shaderModule;
}

void destroyShaderModule(core::Device &device, VkShaderModule shaderModule) {
	vkDestroyShaderModule(device.get(), shaderModule, nullptr);
}

}        // namespace utils::shader
