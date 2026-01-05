module;
#include <GLFW/glfw3.h>
#include <vk_mem_alloc.h>

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <chrono>
//
#include <string.h>
#include <iostream>
#include <vector>
#include <stdexcept>
#include <utility>


module vorn;
// import std;
import vulkan_hpp;

Vorn::Vorn() {
	std::cout << "Constructing Vorn\n";
}

Vorn::~Vorn() {
	std::cout << "Destructing Vorn\n";

}

void Vorn::run() {
	init_window();
	init_vulkan();
	main_loop();
	cleanup();
}

void Vorn::init_vulkan() {
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
	create_vertex_buffer();
	create_index_buffer();
	create_uniform_buffers();
	create_command_buffer();
	create_sync_objects();
}

void Vorn::main_loop() {
	while (!glfwWindowShouldClose(window)) {
		glfwPollEvents();
		draw_frame();
	}
	device.waitIdle();
}

void Vorn::cleanup() {
	cleanup_swapchain();

	// VMA allocator being destroyed before vertexBuff
	// explicitly call destructor
	// TODO: find something cleaner
	uniformBuffs.clear();
	indexBuff = VmaBuffer{};
	vertexBuff = VmaBuffer{};
	vmaDestroyAllocator(allocator);
	/*
		we delete window manually here before raii destructors run
		surface won't have valid wayland surface -> segfault
	*/
	surface.clear();

	glfwDestroyWindow(window);
	glfwTerminate();
}

void Vorn::record_command_buffer(uint32_t imgIdx) {
	auto& cmdBuffer = cmdBuffers[frameIdx];
	cmdBuffer.begin({});
	transition_image_layout(
		imgIdx,
		vk::ImageLayout::eUndefined,
		vk::ImageLayout::eColorAttachmentOptimal,
		{},
		vk::AccessFlagBits2::eColorAttachmentWrite,
		vk::PipelineStageFlagBits2::eColorAttachmentOutput,
		vk::PipelineStageFlagBits2::eColorAttachmentOutput
	);
	vk::ClearValue clearColor = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f);
	vk::RenderingAttachmentInfo attachmentInfo = {
		.imageView = *swapchainImgViews[imgIdx],
		.imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
		.loadOp = vk::AttachmentLoadOp::eClear,
		.storeOp = vk::AttachmentStoreOp::eStore,
		.clearValue = clearColor
	};
	vk::RenderingInfo renderingInfo = {
		.renderArea = {.offset = {0, 0}, .extent = swapchainExtent},
		.layerCount = 1,
		.colorAttachmentCount = 1,
		.pColorAttachments = &attachmentInfo
	};

	cmdBuffer.beginRendering(renderingInfo);
	cmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *graphicsPipeline);
	cmdBuffer.bindVertexBuffers(0, vertexBuff.buffer(), {0});
	cmdBuffer.bindIndexBuffer(indexBuff.buffer(), 0, vk::IndexType::eUint16);
	cmdBuffer.setViewport(0, vk::Viewport(0.0f, 0.0f, static_cast<float>(swapchainExtent.width), static_cast<float>(swapchainExtent.height), 0.0f, 1.0f));
	cmdBuffer.setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), swapchainExtent));
	cmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, *descriptorSets, nullptr);
	PushConstants pc {.objIdx = frameIdx, .textureidx = 0};
	cmdBuffer.pushConstants<PushConstants>(pipelineLayout, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, pc);
	cmdBuffer.drawIndexed(indices.size(), 1, 0, 0, 0);
	cmdBuffer.endRendering();

	transition_image_layout(
		imgIdx,
		vk::ImageLayout::eColorAttachmentOptimal,
		vk::ImageLayout::ePresentSrcKHR,
		vk::AccessFlagBits2::eColorAttachmentWrite,
		{},
		vk::PipelineStageFlagBits2::eColorAttachmentOutput,
		vk::PipelineStageFlagBits2::eBottomOfPipe
	);
	cmdBuffer.end();
}

void Vorn::transition_image_layout(
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

void Vorn::draw_frame() {
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

void Vorn::copy_buffer(VmaBuffer& srcBuff, VmaBuffer& dstBuff, vk::DeviceSize size) {
	vk::CommandBufferAllocateInfo allocInfo {
		.commandPool = cmdPool,
		.level = vk::CommandBufferLevel::ePrimary,
		.commandBufferCount = 1
	};
	vk::raii::CommandBuffer cmdCopyBuff = std::move(device.allocateCommandBuffers(allocInfo).value.front());
	// auto buffExpected = dev
	cmdCopyBuff.begin(vk::CommandBufferBeginInfo {.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
	cmdCopyBuff.copyBuffer(srcBuff.buffer(), dstBuff.buffer(), vk::BufferCopy(0, 0, size));
	cmdCopyBuff.end();
	graphicsQueue.submit(vk::SubmitInfo {.commandBufferCount = 1, .pCommandBuffers = &*cmdCopyBuff,}, nullptr);
	graphicsQueue.waitIdle();
}

void Vorn::create_vertex_buffer() {
	vk::DeviceSize buffSize = sizeof(vertices[0]) * vertices.size();
	VmaBuffer stagingBuff = VmaBuffer(allocator, buffSize, vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_AUTO, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);

	void *dataStaging;
	vmaMapMemory(allocator, stagingBuff.allocation(), &dataStaging);
	memcpy(dataStaging, vertices.data(), buffSize);
	vmaUnmapMemory(allocator, stagingBuff.allocation());

	vertexBuff = VmaBuffer(allocator, buffSize, vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst, VMA_MEMORY_USAGE_AUTO, VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT);
	copy_buffer(stagingBuff, vertexBuff, buffSize);
}

uint32_t Vorn::find_memory_type(uint32_t typeFilter, vk::MemoryPropertyFlags properties) {
	vk::PhysicalDeviceMemoryProperties memProperties = physicalDevice.getMemoryProperties();
	for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
		if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
			return i;
		}
	}
	throw std::runtime_error("Failed to find a suitable memory type");
	std::unreachable();
}

void Vorn::create_index_buffer() {
	vk::DeviceSize buffSize = sizeof(indices[0]) * indices.size();
	VmaBuffer stagingBuff = VmaBuffer(allocator, buffSize, vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_AUTO, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);

	void *dataStaging;
	vmaMapMemory(allocator, stagingBuff.allocation(), &dataStaging);
	memcpy(dataStaging, indices.data(), buffSize);
	vmaUnmapMemory(allocator, stagingBuff.allocation());

	indexBuff = VmaBuffer(allocator, buffSize, vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst, VMA_MEMORY_USAGE_AUTO, VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT);
	copy_buffer(stagingBuff, indexBuff, buffSize);
}

void Vorn::create_descriptor_set_layout() {
	vk::DescriptorSetLayoutBinding uniformBinding {
		.binding = 0,
		.descriptorType = vk::DescriptorType::eUniformBuffer,
		.descriptorCount = MAX_OBJECTS,
		.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment
	};
	vk::DescriptorSetLayoutBinding textureBinding {
		.binding = 1,
		.descriptorType = vk::DescriptorType::eCombinedImageSampler,
		.descriptorCount = MAX_OBJECTS,
		.stageFlags = vk::ShaderStageFlagBits::eFragment
	};
	std::array<vk::DescriptorSetLayoutBinding, 2> bindings = {uniformBinding, textureBinding};
	std::array<vk::DescriptorBindingFlags, 2> bindingsFlags
	{
		vk::DescriptorBindingFlagBits::ePartiallyBound | vk::DescriptorBindingFlagBits::eUpdateAfterBind,
		vk::DescriptorBindingFlagBits::ePartiallyBound | vk::DescriptorBindingFlagBits::eUpdateAfterBind
	};
	vk::DescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo {
		.bindingCount = static_cast<uint32_t>(bindingsFlags.size()),
		.pBindingFlags = bindingsFlags.data()
	};
	vk::DescriptorSetLayoutCreateInfo layoutInfo {
		.pNext = bindingFlagsInfo,
		.flags = vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool,
		.bindingCount = static_cast<uint32_t>(bindings.size()),
		.pBindings = bindings.data()
	};

	auto layoutExpected = device.createDescriptorSetLayout(layoutInfo);
	if (!layoutExpected.has_value()) {
		handle_error("Failed to create descriptor set layout", layoutExpected.result);
	}
	descriptorSetLayout = std::move(*layoutExpected);
}

void Vorn::create_uniform_buffers() {

	uniformBuffs.clear();
	uniformBuffsMapped.clear();

	vk::DeviceSize buffSize = sizeof(UniformBufferObject);
	for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
		VmaBuffer buff = VmaBuffer(allocator, buffSize, vk::BufferUsageFlagBits::eUniformBuffer, VMA_MEMORY_USAGE_AUTO, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);
		auto vkBuff = buff.buffer();
		void* mapped = buff.mapped_data();

		uniformBuffsMapped.push_back(mapped);
		uniformBuffs.push_back(std::move(buff));

		vk::DescriptorBufferInfo buffInfo {
			.buffer = vkBuff,
			.offset = 0,
			.range = sizeof(UniformBufferObject)
		};
		vk::WriteDescriptorSet writeSet {
			.dstSet = *descriptorSets,
			.dstBinding = 0,
			.dstArrayElement = static_cast<uint32_t>(i),
			.descriptorCount = 1,
			.descriptorType = vk::DescriptorType::eUniformBuffer,
			.pBufferInfo = &buffInfo
		};
		device.updateDescriptorSets(writeSet, nullptr);
	}
}

void Vorn::update_uniform_buffers(uint32_t currImg) {
	static auto startTime = std::chrono::high_resolution_clock().now();
	auto currentTime = std::chrono::high_resolution_clock().now();
	float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();

	UniformBufferObject ubo{};
	ubo.model = rotate(glm::mat4(1.0f), time * glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
	ubo.view = lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
	ubo.proj = glm::perspective(glm::radians(45.0f), static_cast<float>(swapchainExtent.width) / static_cast<float>(swapchainExtent.height), 0.1f, 10.0f);
	ubo.proj[1][1] *= -1;

	memcpy(uniformBuffsMapped[currImg], &ubo, sizeof(ubo));
}

void Vorn::create_descriptor_pools() {
	std::array<vk::DescriptorPoolSize, 2> poolSizes = {{
		{vk::DescriptorType::eUniformBuffer, MAX_OBJECTS},
		{vk::DescriptorType::eCombinedImageSampler, MAX_TEXTURES}
	}};
	vk::DescriptorPoolCreateInfo poolInfo {
		.flags = vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind | vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
		.maxSets = 1,
		.poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
		.pPoolSizes = poolSizes.data()
	};
	auto poolExpected = device.createDescriptorPool(poolInfo);
	if (!poolExpected.has_value()) {
		handle_error("Failed to create descriptor pool", poolExpected.result);
	}
	descriptorPool = std::move(*poolExpected);
}

void Vorn::create_descriptor_sets() {
	descriptorSets.clear();
	vk::DescriptorSetAllocateInfo allocInfo {
		.descriptorPool = descriptorPool,
		.descriptorSetCount = 1,
		.pSetLayouts = &*descriptorSetLayout
	};
	auto setExpected = device.allocateDescriptorSets(allocInfo);
	if (!setExpected.has_value()) {
		handle_error("Failed to allocate descriptor set", setExpected.result);
	}
	descriptorSets = std::move(setExpected->front());
}

void handle_error(const char* msg, vk::Result err) {
	throw std::runtime_error(std::format("{}: {}", msg, to_string(err)));
}
