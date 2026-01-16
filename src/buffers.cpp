module;
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

module velo;
import std;
import vulkan_hpp;

void Velo::create_index_buffer() {
	vk::DeviceSize buffSize = sizeof(indices[0]) * indices.size();
	VmaBuffer stagingBuff = VmaBuffer(gpu.allocator, buffSize, vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_AUTO, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);

	void *dataStaging = nullptr;
	vmaMapMemory(gpu.allocator, stagingBuff.allocation(), &dataStaging);
	std::memcpy(dataStaging, indices.data(), buffSize);
	vmaUnmapMemory(gpu.allocator, stagingBuff.allocation());

	indexBuff = VmaBuffer(gpu.allocator, buffSize, vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst, VMA_MEMORY_USAGE_AUTO, VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT);
	copy_buffer(stagingBuff, indexBuff, buffSize);
}

void Velo::create_vertex_buffer() {
	vk::DeviceSize buffSize = sizeof(vertices[0]) * vertices.size();
	VmaBuffer stagingBuff = VmaBuffer(gpu.allocator, buffSize, vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_AUTO, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);

	void *dataStaging = nullptr;;
	vmaMapMemory(gpu.allocator, stagingBuff.allocation(), &dataStaging);
	std::memcpy(dataStaging, vertices.data(), buffSize);
	vmaUnmapMemory(gpu.allocator, stagingBuff.allocation());

	vertexBuff = VmaBuffer(gpu.allocator, buffSize, vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst, VMA_MEMORY_USAGE_AUTO, VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT);
	copy_buffer(stagingBuff, vertexBuff, buffSize);
}

void Velo::create_uniform_buffers() {
	uniformBuffs.clear();
	uniformBuffsMapped.clear();

	vk::DeviceSize buffSize = sizeof(UniformBufferObject);
	for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
		VmaBuffer buff = VmaBuffer(gpu.allocator, buffSize, vk::BufferUsageFlagBits::eUniformBuffer, VMA_MEMORY_USAGE_AUTO, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);
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
			.dstArrayElement = static_cast<std::uint32_t>(i),
			.descriptorCount = 1,
			.descriptorType = vk::DescriptorType::eUniformBuffer,
			.pBufferInfo = &buffInfo
		};
		gpu.device.updateDescriptorSets(writeSet, nullptr);
	}
}

void Velo::create_material_index_buffer() {
	vk::DeviceSize buffSize = sizeof(std::uint32_t) * materialIndices.size();
	VmaBuffer stagingBuff = VmaBuffer(gpu.allocator, buffSize, vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_AUTO, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);

	void *dataStaging = nullptr;
	vmaMapMemory(gpu.allocator, stagingBuff.allocation(), &dataStaging);
	std::memcpy(dataStaging, materialIndices.data(), buffSize);
	vmaUnmapMemory(gpu.allocator, stagingBuff.allocation());

	materialIdxBuff = VmaBuffer(gpu.allocator, buffSize, vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst, VMA_MEMORY_USAGE_AUTO, VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT);
	copy_buffer(stagingBuff, materialIdxBuff, buffSize);

	vk::DescriptorBufferInfo matBuffInfo {
		.buffer = materialIdxBuff.buffer(),
		.offset = 0,
		.range = buffSize
	};
	vk::WriteDescriptorSet writeSet {
		.dstSet = *descriptorSets,
		.dstBinding = 2,
		.dstArrayElement = 0,
		.descriptorCount = 1,
		.descriptorType = vk::DescriptorType::eStorageBuffer,
		.pBufferInfo = &matBuffInfo
	};
	gpu.device.updateDescriptorSets(writeSet, nullptr);
}

void Velo::update_uniform_buffers(std::uint32_t currImg) {
	static auto startTime = std::chrono::high_resolution_clock::now();
	static auto lastTime = startTime;
	auto currentTime = std::chrono::high_resolution_clock::now();
	totalTime = std::chrono::duration<float>(currentTime - startTime).count();
	dt = std::chrono::duration<float>(currentTime - lastTime).count();
	lastTime = currentTime;

	UniformBufferObject ubo{};
	ubo.model = glm::translate(glm::mat4(1.0f), position);
	currAngle += dt * glm::radians(rotationSpeed) * static_cast<float>(rotation);
	ubo.model = glm::rotate(
		ubo.model, // input matrix
		currAngle,
		glm::vec3(0.0f, 1.0f, 0.0f) // axis to rotate around
	);
	ubo.view = lookAt(
		glm::vec3(0.0f, 3.0f, 7.0f), // camera pos
		glm::vec3(0.0f, 1.0f, 0.0f), // target
		glm::vec3(0.0f, 1.0f, 0.0f)  // X/Y/Z is up
	);
	// TODO: figure this one out
	ubo.proj = glm::perspective(
		glm::radians(45.0f),
		static_cast<float>(swapchain.extent.width) / static_cast<float>(swapchain.extent.height),
		0.1f, 10.0f
	);
	ubo.proj[1][1] *= -1;

	std::memcpy(uniformBuffsMapped[currImg], &ubo, sizeof(ubo));
}
void Velo::copy_buffer(VmaBuffer& srcBuff, VmaBuffer& dstBuff, vk::DeviceSize size) {
	auto cmdBuff = gpu.begin_single_time_commands();
	cmdBuff.copyBuffer(srcBuff.buffer(), dstBuff.buffer(), vk::BufferCopy(0, 0, size));
	gpu.end_single_time_commands(cmdBuff);
}

void Velo::record_command_buffer(std::uint32_t imgIdx) {
	auto& cmdBuffer = cmdBuffers[frameIdx];
	cmdBuffer.begin({});
	transition_image_layout(
		swapchain.images[imgIdx],
		vk::ImageLayout::eUndefined,
		vk::ImageLayout::eColorAttachmentOptimal,
		{},
		vk::AccessFlagBits2::eColorAttachmentWrite,
		vk::PipelineStageFlagBits2::eColorAttachmentOutput,
		vk::PipelineStageFlagBits2::eColorAttachmentOutput,
		vk::ImageAspectFlagBits::eColor
	);
	transition_image_layout(
		swapchain.depthImage.image(),
		vk::ImageLayout::eUndefined,
		vk::ImageLayout::eDepthAttachmentOptimal,
		vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
		vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
		vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
		vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
		vk::ImageAspectFlagBits::eDepth
	);
	vk::ClearValue clearColor = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f);
	vk::RenderingAttachmentInfo attachmentInfo = {
		.imageView = *swapchain.imageViews[imgIdx],
		.imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
		.loadOp = vk::AttachmentLoadOp::eClear,
		.storeOp = vk::AttachmentStoreOp::eStore,
		.clearValue = clearColor
	};
	vk::ClearValue clearDepth = vk::ClearDepthStencilValue(1.0f, 0);
	vk::RenderingAttachmentInfo depthAttachmentInfo {
		.imageView = swapchain.depthView,
		.imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
		.loadOp = vk::AttachmentLoadOp::eClear,
		.storeOp = vk::AttachmentStoreOp::eDontCare,
		.clearValue = clearDepth
	};
	vk::RenderingInfo renderingInfo = {
		.renderArea = {.offset = {0, 0}, .extent = swapchain.extent}, // NOLINT
		.layerCount = 1,
		.colorAttachmentCount = 1,
		.pColorAttachments = &attachmentInfo,
		.pDepthAttachment = &depthAttachmentInfo
	};

	cmdBuffer.beginRendering(renderingInfo);
	cmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *graphicsPipeline);
	cmdBuffer.bindVertexBuffers(0, vertexBuff.buffer(), {0});
	cmdBuffer.bindIndexBuffer(indexBuff.buffer(), 0, vk::IndexType::eUint32);
	cmdBuffer.setViewport(0, vk::Viewport(0.0f, 0.0f, static_cast<float>(swapchain.extent.width), static_cast<float>(swapchain.extent.height), 0.0f, 1.0f));
	cmdBuffer.setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), swapchain.extent));
	cmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, *descriptorSets, nullptr);
	PushConstants pc {.objIdx = frameIdx, .textureidx = 0};
	cmdBuffer.pushConstants<PushConstants>(pipelineLayout, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, pc);
	cmdBuffer.drawIndexed(static_cast<std::uint32_t>(indices.size()), 1, 0, 0, 0);
	cmdBuffer.endRendering();

	transition_image_layout(
		swapchain.images[imgIdx],
		vk::ImageLayout::eColorAttachmentOptimal,
		vk::ImageLayout::ePresentSrcKHR,
		vk::AccessFlagBits2::eColorAttachmentWrite,
		{},
		vk::PipelineStageFlagBits2::eColorAttachmentOutput,
		vk::PipelineStageFlagBits2::eBottomOfPipe,
		vk::ImageAspectFlagBits::eColor
	);
	cmdBuffer.end();
}
