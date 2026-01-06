module;
#include <GLFW/glfw3.h>
#include <vk_mem_alloc.h>
//
#include <iostream>
#include <vector>
#include <stdexcept>
#include <utility>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>


module velo;
// import std;
import vulkan_hpp;

Velo::Velo() {
	std::cout << "Constructing Velo\n";
}

Velo::~Velo() {
	std::cout << "Destructing Velo\n";

}

void Velo::run() {
	init_window();
	init_vulkan();
	main_loop();
	cleanup();
}

void Velo::init_vulkan() {
	create_instance();
	setup_debug_messenger();
	create_surface();
	pick_physical_device();
	create_logical_device();
	init_vma();
	create_swapchain();
	create_image_views();
	create_descriptor_set_layout();
	create_descriptor_pools();
	create_descriptor_sets();
	create_graphics_pipeline();
	create_command_pool();
	create_texture_image();
	create_vertex_buffer();
	create_index_buffer();
	create_uniform_buffers();
	create_command_buffer();
	create_sync_objects();
}

void Velo::main_loop() {
	while (!glfwWindowShouldClose(window)) {
		glfwPollEvents();
		draw_frame();
	}
	device.waitIdle();
}

void Velo::cleanup() {
	cleanup_swapchain();

	// VMA allocator being destroyed before vertexBuff
	// explicitly call destructor
	// TODO: find something cleaner
	uniformBuffs.clear();
	indexBuff = VmaBuffer{};
	vertexBuff = VmaBuffer{};
	image = VmaImage{};
	vmaDestroyAllocator(allocator);
	/*
		we delete window manually here before raii destructors run
		surface won't have valid wayland surface -> segfault
	*/
	surface.clear();

	glfwDestroyWindow(window);
	glfwTerminate();
}

void Velo::transition_image_layout(
		uint32_t imgIdx,
		vk::ImageLayout oldLayout,
		vk::ImageLayout newLayout,
		vk::AccessFlags2 srcAccessMask,
		vk::AccessFlags2 dstAccessMask,
		vk::PipelineStageFlags2 srcStageMask,
		vk::PipelineStageFlags2 dstStageMask
) {
	vk::ImageMemoryBarrier2 barrier = {
		.srcStageMask = srcStageMask,
		.srcAccessMask = srcAccessMask,
		.dstStageMask = dstStageMask,
		.dstAccessMask = dstAccessMask,
		.oldLayout = oldLayout,
		.newLayout = newLayout,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = swapchainImgs[imgIdx],
		.subresourceRange = {
			.aspectMask = vk::ImageAspectFlagBits::eColor,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 1
		}
	};
	vk::DependencyInfo depInfo = {
		.dependencyFlags = {},
		.imageMemoryBarrierCount = 1,
		.pImageMemoryBarriers = &barrier
	};
	cmdBuffers[frameIdx].pipelineBarrier2(depInfo);
}

void Velo::draw_frame() {
	uint64_t timelineValue = ++frameCount;
	frameIdx = (timelineValue - 1) % MAX_FRAMES_IN_FLIGHT;
	uint64_t waitValue = 0;
	if (timelineValue > MAX_FRAMES_IN_FLIGHT)
		waitValue = timelineValue - MAX_FRAMES_IN_FLIGHT;
	vk::SemaphoreWaitInfo waitInfo = {
		.semaphoreCount = 1,
		.pSemaphores = &*timelineSem,
		.pValues = &waitValue
	};
	auto waitExpected = device.waitSemaphores(waitInfo, UINT64_MAX);
	if (waitExpected != vk::Result::eSuccess) {
		handle_error("Failed to wait on timeline semaphore", waitExpected);
	}

	// check resize before acquiring (diff from tutorial because timeline sem instead of fences they can just reset)
	if (frameBuffResized) {
		frameBuffResized = false;
		recreate_swapchain();
		// dummy signal (yay documentation)
		vk::SemaphoreSignalInfo signalInfo {
			.semaphore = *timelineSem,
			.value = timelineValue
		};
		device.signalSemaphore(signalInfo);
		return;
	}

	update_uniform_buffers(frameIdx);
	auto nextImgExpected = swapchain.acquireNextImage(UINT64_MAX, *presentCompleteSems[frameIdx], nullptr);
	if (!nextImgExpected.has_value()) {
		handle_error("Failed to acquire next swapchain image", nextImgExpected.result);
	}
	// TODO: Mark suboptimalKHR for recreation after rather than recreate now, it's soft
	if (nextImgExpected.result == vk::Result::eErrorOutOfDateKHR) {
		frameBuffResized = false;
		recreate_swapchain();
		vk::SemaphoreSignalInfo signalInfo {
			.semaphore = *timelineSem,
			.value = timelineValue
		};
		device.signalSemaphore(signalInfo);
		return;
	}
	auto imgIdx = *nextImgExpected;
	record_command_buffer(imgIdx);

	// using sync 2 feature
	vk::SemaphoreSubmitInfo waitSemsInfo[] = {
		{
			.semaphore = *presentCompleteSems[frameIdx],
			.value = 0,
			.stageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput // when to signal the Sem
		}
	};
	vk::SemaphoreSubmitInfo signalSemsInfo[] = {
		{
			.semaphore = *renderDoneSems[imgIdx],
			.value = 0,
			.stageMask = vk::PipelineStageFlagBits2::eAllGraphics
		},
		{
			.semaphore = *timelineSem,
			.value = timelineValue,
			.stageMask = vk::PipelineStageFlagBits2::eAllGraphics
		}
	};
	vk::CommandBufferSubmitInfo cmdInfo {
		.commandBuffer = *cmdBuffers[frameIdx]
	};
	vk::SubmitInfo2 submitInfo = {
		.waitSemaphoreInfoCount = 1,
		.pWaitSemaphoreInfos = waitSemsInfo,
		.commandBufferInfoCount = 1,
		.pCommandBufferInfos = &cmdInfo,
		.signalSemaphoreInfoCount = 2,
		.pSignalSemaphoreInfos = signalSemsInfo
	};
	graphicsQueue.submit2(submitInfo);

	const vk::PresentInfoKHR presentInfo = {
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = &*renderDoneSems[imgIdx],
		.swapchainCount = 1,
		.pSwapchains = &*swapchain,
		.pImageIndices = &imgIdx
	};
	auto presentExpected = presentQueue.presentKHR(presentInfo);
	if (presentExpected == vk::Result::eErrorOutOfDateKHR || presentExpected == vk::Result::eSuboptimalKHR) {
		recreate_swapchain();
	} else if (presentExpected != vk::Result::eSuccess) {
		handle_error("Failed to present frame", presentExpected);
	}
}

uint32_t Velo::find_memory_type(uint32_t typeFilter, vk::MemoryPropertyFlags properties) {
	vk::PhysicalDeviceMemoryProperties memProperties = physicalDevice.getMemoryProperties();
	for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
		if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
			return i;
		}
	}
	throw std::runtime_error("Failed to find a suitable memory type");
	std::unreachable();
}

void Velo::create_texture_image() {
	int texWidth, texHeight, texChannels;
	stbi_uc* pixels = stbi_load("textures/khronos_sample.jpg", &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
	vk::DeviceSize imgSize = texWidth * texHeight * 4; // 4 bytes per pixel
	if (!pixels) {
		throw std::runtime_error("Failed to load pixels from texture");
	}

	VmaBuffer stagingBuffer = VmaBuffer(allocator, imgSize, vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_AUTO, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
	void* data;
	vmaMapMemory(allocator, stagingBuffer.allocation(), &data);
	memcpy(data, pixels, imgSize);
	vmaUnmapMemory(allocator, stagingBuffer.allocation());

	stbi_image_free(pixels);
	image = VmaImage(allocator, vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled, vk::Format::eR8G8B8A8Srgb, VMA_MEMORY_USAGE_AUTO, VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT);
	std::cout << "Successfully created image\n";
}

vk::raii::CommandBuffer Velo::begin_single_time_commands() {
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
	return cmdBuff;
}

void Velo::end_single_time_commands(vk::raii::CommandBuffer& cmdBuff)  {
	cmdBuff.end();

	vk::SubmitInfo submitInfo {
		.commandBufferCount = 1,
		.pCommandBuffers = &*cmdBuff
	};
	graphicsQueue.submit(submitInfo);
	graphicsQueue.waitIdle();
}
