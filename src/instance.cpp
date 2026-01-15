module;
#include <vk_mem_alloc.h>
#include <GLFW/glfw3.h>

//
#include <vector>
#include <stdexcept>
#include <iostream>
#include <algorithm>
#include <cstring>

module velo;
// import std;
import vulkan_hpp;

void Velo::init_window() {
	// optional for renderdoc debugging
	if (vcontext.enabled_x11)
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
void Velo::create_instance() {
	constexpr vk::ApplicationInfo appInfo {
		.pApplicationName = "LVK",
		.applicationVersion = VK_MAKE_API_VERSION(0, 1, 0, 0),
		.pEngineName = "No Engine",
		.engineVersion = VK_MAKE_API_VERSION(0, 1, 0, 0),
		.apiVersion = vk::ApiVersion14
	};

	auto requiredLayers = get_required_layers();
	auto requiredExtensions = get_required_extensions();

	vk::InstanceCreateInfo createInfo{
		.pApplicationInfo = &appInfo,
		.enabledLayerCount = static_cast<uint32_t>(requiredLayers.size()),
		.ppEnabledLayerNames = requiredLayers.data(),
		.enabledExtensionCount = static_cast<uint32_t>(requiredExtensions.size()),
		.ppEnabledExtensionNames = requiredExtensions.data(),
	};
	auto instExpected = context.createInstance(createInfo);
	if (!instExpected.has_value()) {
		handle_error("Failed to create instance", instExpected.result);
	}
	instance = std::move(*instExpected);
}

std::vector<char const*> Velo::get_required_extensions() {
	std::vector<char const*> requiredExtensions;
	// VK extensions
	auto propsExpected = context.enumerateInstanceExtensionProperties();
	if (!propsExpected.has_value()) {
		handle_error("Failed to enumerate instance extension properties", propsExpected.result);
	}
	auto extensionProperties = *propsExpected;
	vcontext.extensionProperties = extensionProperties;
	// GLFW extensions
	uint32_t glfwExtensionsCount = 0;
	auto* glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionsCount);
	std::span glfwExtensionsSpan(glfwExtensions, glfwExtensionsCount);
	vcontext.requiredGlfwExtensions = glfwExtensions;
	vcontext.glfwCount = glfwExtensionsCount;
	if (vcontext.fetch_infos) {
		vcontext.gather_extensions_info();
	}
	// GLFW check
	bool extensionsSupported = std::ranges::all_of(glfwExtensionsSpan, [&extensionProperties](auto const& glfwExtension) {
									return std::ranges::any_of(extensionProperties, [&glfwExtension](auto const& extensionProperty) {
										return std::strcmp(extensionProperty.extensionName, glfwExtension) == 0;
									});
								});
	if (!extensionsSupported) {
		throw std::runtime_error("Required glfw extension not supported!\n");
	}
	std::cout << "All required GLFW extensions available\n";

	requiredExtensions.append_range(glfwExtensionsSpan);
	if (enableValidationLayers) {
		requiredExtensions.push_back(vk::EXTDebugUtilsExtensionName);
	}
	return requiredExtensions;
}

std::vector<char const*> Velo::get_required_layers() {
	// required layers
	std::vector<char const*> requiredLayers;
	if (enableValidationLayers) {
		requiredLayers.assign_range(validationLayers);
	}
	// VK Layers
	auto layersExpected = context.enumerateInstanceLayerProperties();
	if (!layersExpected.has_value()) {
		handle_error("Could not fetch available vk layers", layersExpected.result);
	}
	auto layerProperties = *layersExpected;
	vcontext.layerProperties = layerProperties;
	if (vcontext.fetch_infos) {
		vcontext.gather_layers_info();
	}
	// layers check
	bool layersSupported = std::ranges::all_of(requiredLayers, [&layerProperties](auto const& requiredLayer) {
								return std::ranges::any_of(layerProperties, [&requiredLayer](auto const& layerProperty) {
									return std::strcmp(layerProperty.layerName, requiredLayer) == 0;
								});
							});
	if (!layersSupported) {
		throw std::runtime_error("One or more required layer is not supported!");
	} else {
		std::cout << "All required layers supported!\n";
	}

	return requiredLayers;
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
	auto msgerExpected = instance.createDebugUtilsMessengerEXT(messengerInfo);
	if (!msgerExpected.has_value()) {
		handle_error("Failed to create DebugMessenger", msgerExpected.result);
	}
	debugMessenger = std::move(*msgerExpected);
	std::cout << "Successfully created DebugMessenger\n";
}

void Velo::init_vma() {
	VmaVulkanFunctions vkFns = {};
	vkFns.vkGetInstanceProcAddr = instance.getDispatcher()->vkGetInstanceProcAddr;
	vkFns.vkGetDeviceProcAddr = instance.getDispatcher()->vkGetDeviceProcAddr;

	VmaAllocatorCreateInfo allocatorInfo = {};
	allocatorInfo.flags = VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT;
	allocatorInfo.physicalDevice = *physicalDevice;
	allocatorInfo.device = *device;
	allocatorInfo.pVulkanFunctions = &vkFns;
	allocatorInfo.instance = *instance;
	allocatorInfo.vulkanApiVersion = vk::ApiVersion14;

	vmaCreateAllocator(&allocatorInfo, &allocator);
}
