module;
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <utility>
#include <vector>
#include <iostream>
#include <algorithm>
#include <cstring>
#include <vulkan/vulkan_core.h>
#include <map>
#include <vk_mem_alloc.h>

module velo;
static std::vector<char const*> get_required_extensions(vk::raii::Context& context, VeloContext& vcontext);
static std::vector<char const*> get_required_layers(vk::raii::Context& context, VeloContext& vcontext);
/*
	"All the helper functions that submit commands so far have been set up to execute synchronously by
	waiting for the queue to become idle. For practical applications it is recommended to combine these operations
	in a single command buffer and execute them asynchronously for higher throughput, especially the transitions
	and copy in the createTextureImage function. Try to experiment with this by creating a setupCommandBuffer that the
	helper functions record commands into, and add a flushSetupCommands to execute the commands that have been recorded so far.
	It’s best to do this after the texture mapping works to check if the texture resources are still set up correctly."
		 - tutorial (https://docs.vulkan.org/tutorial/latest/06_Texture_mapping/00_Images.html)
	TODO: Make command submit asynchronous
*/
vk::raii::CommandBuffer GpuContext::begin_single_time_commands() {
	vk::CommandBufferAllocateInfo allocInfo {
		.commandPool = cmdPool,
		.level = vk::CommandBufferLevel::ePrimary,
		.commandBufferCount = 1
	};
	auto cmdBuffExpected = device.allocateCommandBuffers(allocInfo);
	if (!cmdBuffExpected.has_value()) {
		handle_error("Failed to allocate command buffer", cmdBuffExpected.result);
	}
	auto cmdBuff = std::move(cmdBuffExpected->front());
	vk::CommandBufferBeginInfo beginInfo {
		.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit
	};
	cmdBuff.begin(beginInfo);

	return cmdBuff;
}

void GpuContext::end_single_time_commands(vk::raii::CommandBuffer& cmdBuff)  const {
	cmdBuff.end();

	vk::SubmitInfo submitInfo {
		.commandBufferCount = 1,
		.pCommandBuffers = &*cmdBuff
	};
	graphicsQueue.submit(submitInfo);
	graphicsQueue.waitIdle();
}

void GpuContext::create_logical_device(vk::raii::SurfaceKHR& _surface) {
	auto qfps = physicalDevice.getQueueFamilyProperties();
	auto [graphicsIndex, presentIndex] = find_queue_families(qfps, _surface);
	graphicsIdx = graphicsIndex;
	presentIdx = presentIndex;
	float queuePrio = 1.0f;

	vk::StructureChain<
		vk::PhysicalDeviceFeatures2,
		vk::PhysicalDeviceVulkan11Features,
		vk::PhysicalDeviceVulkan12Features,
		vk::PhysicalDeviceVulkan13Features,
		vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT
	> featureChain = {
		{.features = { // 1.0
			.geometryShader = true,
			.samplerAnisotropy = true
		}},
		{ // 1.1
			.shaderDrawParameters = true,
		},
		{ // 1.2
			.descriptorIndexing = true,
			.shaderUniformBufferArrayNonUniformIndexing = true,
			.shaderSampledImageArrayNonUniformIndexing = true,
			.descriptorBindingUniformBufferUpdateAfterBind = true,
			.descriptorBindingSampledImageUpdateAfterBind = true,
			.descriptorBindingStorageBufferUpdateAfterBind = true,
			.descriptorBindingUpdateUnusedWhilePending = true,
			.descriptorBindingPartiallyBound = true,
			.descriptorBindingVariableDescriptorCount = true,
			.runtimeDescriptorArray = true,
			.timelineSemaphore = true
		},
		{ // 1.3
			.synchronization2 = true,
			.dynamicRendering = true
		},
		// extensions
		{.extendedDynamicState = true},
	};

	std::vector<vk::DeviceQueueCreateInfo> queueInfos{};
	vk::DeviceQueueCreateInfo graphicsQueueInfo{
		.queueFamilyIndex = graphicsIdx,
		.queueCount = 1,
		.pQueuePriorities = &queuePrio,
	};
	queueInfos.push_back(graphicsQueueInfo);
	if (graphicsIdx != presentIdx) {
		vk::DeviceQueueCreateInfo presentQueueInfo {
			.queueFamilyIndex = presentIdx,
			.queueCount = 1,
			.pQueuePriorities = &queuePrio,
		};
		queueInfos.push_back(presentQueueInfo);
	}

	// not using {} constructor because it expects deprecated layerCount/Names before extensions.
	// TODO: update this when fixed on VK side
	vk::DeviceCreateInfo deviceInfo{};
	deviceInfo.pNext = &featureChain.get<vk::PhysicalDeviceFeatures2>();
	deviceInfo.queueCreateInfoCount = static_cast<uint32_t>(queueInfos.size());
	deviceInfo.pQueueCreateInfos = queueInfos.data();
	deviceInfo.enabledExtensionCount = static_cast<uint32_t>(requiredDeviceExtensions.size());
	deviceInfo.ppEnabledExtensionNames = requiredDeviceExtensions.data();

	auto deviceExpected = physicalDevice.createDevice(deviceInfo);
	if (!deviceExpected.has_value()) {
		handle_error("Failed to create logical device", deviceExpected.result);
	}
	device = std::move(*deviceExpected);
	std::cout << "Successfully created logical device\n";
	graphicsQueue = device.getQueue(graphicsIdx, 0);
	presentQueue = device.getQueue(presentIdx, 0);
}

void GpuContext::create_instance(vk::raii::Context& context, VeloContext& vcontext) {
	constexpr vk::ApplicationInfo appInfo {
		.pApplicationName = "LVK",
		.applicationVersion = VK_MAKE_API_VERSION(0, 1, 0, 0),
		.pEngineName = "No Engine",
		.engineVersion = VK_MAKE_API_VERSION(0, 1, 0, 0),
		.apiVersion = vk::ApiVersion14
	};

	auto requiredLayers = get_required_layers(context, vcontext);
	auto requiredExtensions = get_required_extensions(context, vcontext);

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

void GpuContext::pick_physical_device(VeloContext& vcontext) {
	auto devicesExpected = instance.enumeratePhysicalDevices();
	if (!devicesExpected.has_value()) {
		handle_error("Failed to request available physical devices", devicesExpected.result);
	}
	auto devices = *devicesExpected;

	if (devices.empty()) {
		throw std::runtime_error("Failed to find GPU with Vulkan Support");
	}

	std::multimap<int, vk::raii::PhysicalDevice> candidates{};
	for (const auto& dev : devices) {
		auto deviceProperties = dev.getProperties();
		auto deviceFeatures = dev.getFeatures();
		auto queueFamilies = dev.getQueueFamilyProperties();
		auto extsExpected = dev.enumerateDeviceExtensionProperties();
		if (!extsExpected.has_value()) {
			handle_error("Failed to query device for extensions", extsExpected.result);
		}
		const auto& deviceExts = *extsExpected;

		// check suitability
		bool recentEnough = deviceProperties.apiVersion >= vk::ApiVersion13;
		const auto qfpIter = std::ranges::find_if(queueFamilies, [](vk::QueueFamilyProperties const& qfp) {
									return (qfp.queueFlags & vk::QueueFlagBits::eGraphics) != static_cast<vk::QueueFlags>(0);
								});
		bool haveExtensions = std::ranges::all_of(requiredDeviceExtensions, [deviceExts](auto const& deviceExtension) {
								return std::ranges::any_of(deviceExts, [deviceExtension](vk::ExtensionProperties const& extProperty) {
										return std::strcmp(extProperty.extensionName, deviceExtension) == 0;
									});
								});
		if (!deviceFeatures.geometryShader || !deviceFeatures.samplerAnisotropy) {
			continue;
		}
		if (!recentEnough || !haveExtensions || qfpIter == queueFamilies.end()) {
			continue;
		}

		// begin scoring
		uint32_t score = 0;
		if (deviceProperties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu) {
			score += 1000;
		}
		score += deviceProperties.limits.maxImageDimension2D;
		candidates.insert(std::make_pair(score, dev));
	}

	if (candidates.rbegin()->first > 0) {
		physicalDevice = std::move(candidates.rbegin()->second);
		vcontext.deviceProperties = physicalDevice.getProperties();
		vcontext.deviceFeatures = physicalDevice.getFeatures();
		if (vcontext.fetch_infos) {
			vcontext.gather_features_info();
		}
		std::cout << "Picked physical device " << vcontext.deviceProperties.deviceName << '\n';
	} else {
		throw std::runtime_error("Failed to find suitable GPU");
	}
}

static std::vector<char const*> get_required_extensions(vk::raii::Context& context, VeloContext& vcontext) {
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

static std::vector<char const*> get_required_layers(vk::raii::Context& context, VeloContext& vcontext) {
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

std::tuple<uint32_t, uint32_t> GpuContext::find_queue_families(const std::vector<vk::QueueFamilyProperties>& qfps, vk::raii::SurfaceKHR& _surface) const {
	// graphics queue
	auto graphicsQueueFamily = std::ranges::find_if(qfps, [](vk::QueueFamilyProperties const& qfp) {
									return static_cast<bool>(qfp.queueFlags & vk::QueueFlagBits::eGraphics);
								});
	if (graphicsQueueFamily == qfps.end()) {
		throw std::runtime_error("No graphics queue family found");
	}
	uint32_t graphicsIndex = static_cast<uint32_t>(std::distance(qfps.begin(), graphicsQueueFamily));

	// present queue
	auto graphicsSupportPresExpected = physicalDevice.getSurfaceSupportKHR(graphicsIndex, _surface);
	if (!graphicsSupportPresExpected.has_value()) {
		handle_error("Failed to querty for surface support", graphicsSupportPresExpected.result);
	}
	vk::Bool32 supportsPresent = *graphicsSupportPresExpected;
	uint32_t presentIndex = UINT32_MAX;
	if (supportsPresent) {
		// graphics queue family also supports present
		presentIndex = graphicsIndex;
	} else {
		for (uint32_t i = 0; i < qfps.size(); i++) {
			if (i == graphicsIndex) continue;
			auto presSupportExpected = physicalDevice.getSurfaceSupportKHR(i, _surface);
			if (!presSupportExpected.has_value()) {
				handle_error("Failed to query for present support", presSupportExpected.result);
			}
			vk::Bool32 presentSupport = *presSupportExpected;
			if (presentSupport) {
				presentIndex = i;
				break;
			}
		}
	}
	if (presentIndex == UINT32_MAX) {
		throw std::runtime_error("Failed to find present queue family");
	}

	std::cout << "Found Graphics and Present queue families, respectively: [" << graphicsIndex << "], [" << presentIndex << "]\n";

	return {graphicsIndex, presentIndex};
}

void GpuContext::create_command_pool() {
	vk::CommandPoolCreateInfo poolInfo {
		.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer, .queueFamilyIndex = graphicsIdx
	};
	auto poolExpected = device.createCommandPool(poolInfo);
	if (!poolExpected.has_value()) {
		handle_error("Failed to create command pool", poolExpected.result);
	}
	cmdPool = std::move(*poolExpected);
}

void GpuContext::create_surface(GLFWwindow* window) {
	VkSurfaceKHR _surface = nullptr;
	if (glfwCreateWindowSurface(*instance, &*window, nullptr, &_surface) != 0) {
		const char* msg = nullptr;
		glfwGetError(&msg);
		throw std::runtime_error(std::format("Failed to create window surface: {}", msg));
	}
	surface = vk::raii::SurfaceKHR(instance, _surface);
}

void GpuContext::init_vma() {
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
