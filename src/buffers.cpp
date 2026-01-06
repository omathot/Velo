module;
#include <vk_mem_alloc.h>
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <chrono>
//
#include <cstdint>
#include <string.h>
#include <utility>

module velo;
import vulkan_hpp;

void Velo::create_index_buffer() {
	vk::DeviceSize buffSize = sizeof(indices[0]) * indices.size();
	VmaBuffer stagingBuff = VmaBuffer(allocator, buffSize, vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_AUTO, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);

	void *dataStaging;
	vmaMapMemory(allocator, stagingBuff.allocation(), &dataStaging);
	memcpy(dataStaging, indices.data(), buffSize);
	vmaUnmapMemory(allocator, stagingBuff.allocation());

	indexBuff = VmaBuffer(allocator, buffSize, vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst, VMA_MEMORY_USAGE_AUTO, VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT);
	copy_buffer(stagingBuff, indexBuff, buffSize);
}

void Velo::create_vertex_buffer() {
	vk::DeviceSize buffSize = sizeof(vertices[0]) * vertices.size();
	VmaBuffer stagingBuff = VmaBuffer(allocator, buffSize, vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_AUTO, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);

	void *dataStaging;
	vmaMapMemory(allocator, stagingBuff.allocation(), &dataStaging);
	memcpy(dataStaging, vertices.data(), buffSize);
	vmaUnmapMemory(allocator, stagingBuff.allocation());

	vertexBuff = VmaBuffer(allocator, buffSize, vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst, VMA_MEMORY_USAGE_AUTO, VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT);
	copy_buffer(stagingBuff, vertexBuff, buffSize);
}

void Velo::create_uniform_buffers() {
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

void Velo::update_uniform_buffers(uint32_t currImg) {
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
void Velo::copy_buffer(VmaBuffer& srcBuff, VmaBuffer& dstBuff, vk::DeviceSize size) {
	auto cmdBuff = begin_single_time_commands();
	cmdBuff.copyBuffer(srcBuff.buffer(), dstBuff.buffer(), vk::BufferCopy(0, 0, size));
	end_single_time_commands(cmdBuff);
}

void Velo::record_command_buffer(uint32_t imgIdx) {
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
