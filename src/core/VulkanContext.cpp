#include "VulkanContext.hpp"

#include <GLFW/glfw3.h>
#include <VkBootstrap.h>
#include <stdexcept>

namespace core {

void VulkanContext::init(GLFWwindow *window) {
	createInstance();
	createSurface(window);
}

void VulkanContext::shutdown() {
	vkb::destroy_surface(m_vkbInstance, m_surface);
	vkb::destroy_instance(m_vkbInstance);
}

void VulkanContext::createInstance() {
	vkb::InstanceBuilder builder;

	auto inst_ret = builder
	                    .set_app_name("3DGS Renderer (Vulkan)")
	                    .request_validation_layers(true)
	                    .use_default_debug_messenger()
	                    .require_api_version(1, 3)
	                    .build();

	if (!inst_ret)
		throw std::runtime_error("Failed to create Vulkan instance");

	m_vkbInstance = inst_ret.value();
}

void VulkanContext::createSurface(GLFWwindow *window) {
	if (glfwCreateWindowSurface(
	        m_vkbInstance.instance,
	        window,
	        nullptr,
	        &m_surface) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create window surface");
	}
}

}        // namespace core
