module;
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
//
#include <vector>
#include <stdexcept>
#include <iostream>


module velo;
// import std;
import vulkan_hpp;

void Velo::create_logical_device() {
	auto qfps = physicalDevice.getQueueFamilyProperties();
	auto [graphicsIndex, presentIndex] = find_queue_families(qfps);
	graphicsIdx = graphicsIndex;
	presentIdx = presentIndex;
	float queuePrio = 1.0f;

	vk::StructureChain<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan13Features, vk::PhysicalDeviceVulkan12Features, vk::PhysicalDeviceVulkan11Features, vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT> featureChain = {
		{},
		{ // 1.3
			.synchronization2 = true,
			.dynamicRendering = true
		},
		{ // 1.2
			.descriptorIndexing = true,
			.shaderUniformBufferArrayNonUniformIndexing = true,
			.shaderSampledImageArrayNonUniformIndexing = true,
			.descriptorBindingUniformBufferUpdateAfterBind = true,
			.descriptorBindingSampledImageUpdateAfterBind = true,
			.descriptorBindingUpdateUnusedWhilePending = true,
			.descriptorBindingPartiallyBound = true,
			.descriptorBindingVariableDescriptorCount = true,
			.runtimeDescriptorArray = true,
			.timelineSemaphore = true
		},
		{.shaderDrawParameters = true}, // 1.1
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

void Velo::create_surface() {
	VkSurfaceKHR _surface;
	if (glfwCreateWindowSurface(*instance, &*window, nullptr, &_surface) != 0) {
		const char* msg;
		glfwGetError(&msg);
		throw std::runtime_error(std::format("Failed to create window surface: {}", msg));
	}
	surface = vk::raii::SurfaceKHR(instance, _surface);
}

void Velo::create_command_pool() {
	vk::CommandPoolCreateInfo poolInfo {
		.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer, .queueFamilyIndex = graphicsIdx
	};
	auto poolExpected = device.createCommandPool(poolInfo);
	if (!poolExpected.has_value()) {
		handle_error("Failed to create command pool", poolExpected.result);
	}
	cmdPool = std::move(*poolExpected);
}

void Velo::create_command_buffer() {
	cmdBuffers.clear();
	vk::CommandBufferAllocateInfo allocInfo {
		.commandPool = *cmdPool,
		.level = vk::CommandBufferLevel::ePrimary,
		.commandBufferCount = MAX_FRAMES_IN_FLIGHT
	};

	auto cmdBuffExpected = device.allocateCommandBuffers(allocInfo);
	if (!cmdBuffExpected.has_value()) {
		handle_error("Failed to allocate cmd buffer", cmdBuffExpected.result);
	}
	cmdBuffers = std::move(*cmdBuffExpected);
}

void Velo::create_sync_objects() {
	assert(presentCompleteSems.empty() && renderDoneSems.empty());

	for (size_t i = 0; i < swapchainImgs.size(); i++) {
		auto renderSemExpected = device.createSemaphore(vk::SemaphoreCreateInfo{});
		if (!renderSemExpected.has_value()) {
			handle_error("Failed to create render semaphore", renderSemExpected.result);
		}
		renderDoneSems.emplace_back(std::move(*renderSemExpected));
	}
	for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
		// present complete binary sem
		auto presentSemExpected = device.createSemaphore(vk::SemaphoreCreateInfo{});
		if (!presentSemExpected.has_value()) {
			handle_error("Failed to create present semaphore", presentSemExpected.result);
		}
		presentCompleteSems.emplace_back(std::move(*presentSemExpected));
	}

	// timeline sem (just one)
	vk::SemaphoreCreateInfo timeSemInfo = {};
	vk::SemaphoreTypeCreateInfoKHR typepInfo = {
		.semaphoreType = vk::SemaphoreTypeKHR::eTimeline,
		.initialValue = 0
	};
	timeSemInfo.pNext = &typepInfo;
	auto timeSemExpected = device.createSemaphore(timeSemInfo);
	if (!timeSemExpected.has_value()) {
		handle_error("Failed to create timeline semaphore", timeSemExpected.result);
	}
	timelineSem = std::move(*timeSemExpected);
}
void Velo::recreate_swapchain() {
	int width = 0, height = 0;
	glfwGetFramebufferSize(window, &width, &height);
	while (width == 0 || height == 0) {
		glfwGetFramebufferSize(window, &width, &height);
		glfwWaitEvents();
	}

	device.waitIdle();
	cleanup_swapchain();
	create_swapchain();
	create_image_views();
}

void Velo::cleanup_swapchain() {
	swapchainImgViews.clear();
	swapchain = nullptr;
}
