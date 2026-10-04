#pragma once

#include <string>
#include <vulkan/vulkan.h>

namespace core {
class Device;
}

namespace utils::shader {

VkShaderModule createShaderModule(core::Device &device, const std::string &path);
void destroyShaderModule(core::Device &device, VkShaderModule shaderModule);

}        // namespace utils::shader
