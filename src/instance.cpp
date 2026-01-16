module;
#include <vk_mem_alloc.h>
#include <GLFW/glfw3.h>

module velo;
import std;
import vulkan_hpp;

void Velo::init_window() {
	// optional for renderdoc debugging
	if (config.enabled_x11)
		glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
	glfwInit();
	glfwWindowHint(GLFW_CLIENT_API,  GLFW_NO_API);
	window = glfwCreateWindow(WIDTH, HEIGHT, "LVK", nullptr, nullptr);

	glfwSetWindowUserPointer(window, this);
	glfwSetFramebufferSizeCallback(window, frameBufferResizeCb);
	glfwSetInputMode(window, GLFW_STICKY_KEYS, GLFW_TRUE);
	if (glfwRawMouseMotionSupported()) {
		// glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
	}
}
void Velo::setup_debug_messenger() {
	if (!enableValidationLayers) return;

	vk::DebugUtilsMessageSeverityFlagsEXT severityFlags(vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError | vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning);
	vk::DebugUtilsMessageTypeFlagsEXT typeFlags(vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation);

	vk::DebugUtilsMessengerCreateInfoEXT messengerInfo{
		.messageSeverity = severityFlags,
		.messageType = typeFlags,
		.pfnUserCallback = &debug_callback
	};
	auto msgerExpected = gpu.instance.createDebugUtilsMessengerEXT(messengerInfo);
	if (!msgerExpected.has_value()) {
		handle_error("Failed to create DebugMessenger", msgerExpected.result);
	}
	debugMessenger = std::move(*msgerExpected);
	std::cout << "Successfully created DebugMessenger\n";
}

