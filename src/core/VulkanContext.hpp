#pragma once

#include <VkBootstrap.h>
#include <vulkan/vulkan.h>

struct GLFWwindow;

namespace core {

// Owns the VkInstance and the platform surface.
class VulkanContext {
  public:
	void init(GLFWwindow *window);
	void shutdown();

	VkInstance getInstance() const {
		return m_vkbInstance.instance;
	}

	const vkb::Instance &getVkbInstance() const {
		return m_vkbInstance;
	}

	VkSurfaceKHR getSurface() const {
		return m_surface;
	}

  private:
	void createInstance();
	void createSurface(GLFWwindow *window);

  private:
	vkb::Instance m_vkbInstance{};
	VkSurfaceKHR m_surface = VK_NULL_HANDLE;
};

}        // namespace core
