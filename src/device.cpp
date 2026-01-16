module;
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
//
#include <vector>

module velo;
// import std;
import vulkan_hpp;

void Velo::create_command_buffer() {
	cmdBuffers.clear();
	vk::CommandBufferAllocateInfo allocInfo {
		.commandPool = *gpu.cmdPool,
		.level = vk::CommandBufferLevel::ePrimary,
		.commandBufferCount = MAX_FRAMES_IN_FLIGHT
	};

	auto cmdBuffExpected = gpu.device.allocateCommandBuffers(allocInfo);
	if (!cmdBuffExpected.has_value()) {
		handle_error("Failed to allocate cmd buffer", cmdBuffExpected.result);
	}
	cmdBuffers = std::move(*cmdBuffExpected);
}

void Velo::create_sync_objects() {
	assert(presentCompleteSems.empty() && renderDoneSems.empty());

	for (size_t i = 0; i < swapchain.images.size(); i++) {
		auto renderSemExpected = gpu.device.createSemaphore(vk::SemaphoreCreateInfo{});
		if (!renderSemExpected.has_value()) {
			handle_error("Failed to create render semaphore", renderSemExpected.result);
		}
		renderDoneSems.emplace_back(std::move(*renderSemExpected));
	}
	for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
		// present complete binary sem
		auto presentSemExpected = gpu.device.createSemaphore(vk::SemaphoreCreateInfo{});
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
	auto timeSemExpected = gpu.device.createSemaphore(timeSemInfo);
	if (!timeSemExpected.has_value()) {
		handle_error("Failed to create timeline semaphore", timeSemExpected.result);
	}
	timelineSem = std::move(*timeSemExpected);
}
