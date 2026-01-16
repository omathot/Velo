module;
#include <GLFW/glfw3.h>
#include <glm/fwd.hpp>
#include <vk_mem_alloc.h>
#include <tiny_obj_loader.h>
#include <unordered_map>

module velo;
import std;
import vulkan_hpp;

Velo::Velo() {
	std::println("Constructing Velo");
	#if defined(CODAM)
		config.enable_codam();
		std::println("\tEnabled codam mode");
	#endif
	#if defined(X11)
		config.enable_x11();
		std::println("\tEnabled X11 mode");
	#endif
	#if defined(INFOS)
		config.is_info_gathered();
		std::println("\tEnabled Info Fetching");
	#endif
}

void Velo::run() {
	init_window();
	init_vulkan();
	main_loop();
	cleanup();
}

void Velo::init_vulkan() {
	gpu.create_instance(context, config);
	setup_debug_messenger();
	gpu.create_surface(window);
	gpu.pick_physical_device(config);
	gpu.create_logical_device(gpu.surface);
	gpu.init_vma();
	gpu.create_command_pool();

	swapchain.create(window, gpu);
	swapchain.create_image_views(gpu.device);
	swapchain.create_depth_resources(gpu);

	sync.create(gpu.device, static_cast<uint32_t>(swapchain.images.size()));

	descriptors.create_layout(gpu.device);
	descriptors.create_pool(gpu.device);
	descriptors.create_set(gpu.device);

	for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
		frames[i].create(gpu, *descriptors.set, i);
	}

	create_graphics_pipeline();
	init_default_data();
}

void Velo::main_loop() {
	while (!glfwWindowShouldClose(window) && !config.should_quit) {
		glfwPollEvents();
		process_input();
		draw_frame();
	}
	gpu.device.waitIdle();
}

void Velo::cleanup() {
	swapchain.cleanup();

	// VMA allocator being destroyed before vertexBuff
	// explicitly call destructor
	// TODO: find something cleaner
	indexBuff = VmaBuffer{};
	vertexBuff = VmaBuffer{};
	materialIdxBuff = VmaBuffer{};
	textureImage = VmaImage{};
	swapchain.depthImage = VmaImage{};
	materialImages.clear();
	for (auto& frame: frames) {
		frame.uniformBuffer = VmaBuffer{};
	}
	vmaDestroyAllocator(gpu.allocator);
	/*
		we delete window manually here before raii destructors run
		surface won't have valid wayland surface -> segfault
		this might be wayland only?
	*/
	gpu.surface.clear();

	glfwDestroyWindow(window);
	glfwTerminate();
}

void Velo::transition_image_layout(
		vk::Image img,
		vk::ImageLayout oldLayout,
		vk::ImageLayout newLayout,
		vk::AccessFlags2 srcAccessMask,
		vk::AccessFlags2 dstAccessMask,
		vk::PipelineStageFlags2 srcStageMask,
		vk::PipelineStageFlags2 dstStageMask,
		vk::ImageAspectFlags aspectFlags
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
		.image = img,
		.subresourceRange = {
			.aspectMask = aspectFlags,
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
	frames[frameIdx].cmdBuffer.pipelineBarrier2(depInfo);
}

void Velo::draw_frame() {
	uint64_t timelineValue = ++frameCount;
	frameIdx = (timelineValue - 1) % MAX_FRAMES_IN_FLIGHT;
	FrameContext& frame = frames[frameIdx];
	sync.wait_for_frame(gpu.device, timelineValue);

	// check resize before acquiring (diff from tutorial because timeline sem instead of fences they can just reset)
	if (frameBuffResized) {
		frameBuffResized = false;
		swapchain.recreate(window, gpu);
		sync.signal_timeline(gpu.device, timelineValue);
		return;
	}

	update_uniform_buffers();
	auto nextImgExpected = swapchain.swapchain.acquireNextImage(UINT64_MAX, *frame.acquireSem, nullptr);
	bool recreate = nextImgExpected.result == vk::Result::eSuboptimalKHR;
	if (nextImgExpected.result == vk::Result::eErrorOutOfDateKHR) {
		frameBuffResized = false;
		swapchain.recreate(window, gpu);
		vk::SemaphoreSignalInfo signalInfo {
			.semaphore = *sync.timelineSem,
			.value = timelineValue
		};
		gpu.device.signalSemaphore(signalInfo);
		return;
	}
	if (!nextImgExpected.has_value() && !recreate) {
		handle_error("Failed to acquire next swapchain image", nextImgExpected.result);
	}
	auto imgIdx = nextImgExpected.value;
	record_command_buffer(imgIdx);

	// using sync 2 feature
	std::array<vk::SemaphoreSubmitInfo, 1> waitSemsInfo = {{
		{
			.semaphore = *frame.acquireSem,
			.value = 0,
			.stageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput // when to signal the Sem
		}
	}};
	std::array<vk::SemaphoreSubmitInfo, 2> signalSemsInfo = {{
		{
			.semaphore = *sync.presentSems[imgIdx],
			.value = 0,
			.stageMask = vk::PipelineStageFlagBits2::eAllGraphics
		},
		{
			.semaphore = *sync.timelineSem,
			.value = timelineValue,
			.stageMask = vk::PipelineStageFlagBits2::eAllGraphics
		}
	}};
	vk::CommandBufferSubmitInfo cmdInfo {
		.commandBuffer = *frames[frameIdx].cmdBuffer
	};
	vk::SubmitInfo2 submitInfo = {
		.waitSemaphoreInfoCount = 1,
		.pWaitSemaphoreInfos = waitSemsInfo.data(),
		.commandBufferInfoCount = 1,
		.pCommandBufferInfos = &cmdInfo,
		.signalSemaphoreInfoCount = 2,
		.pSignalSemaphoreInfos = signalSemsInfo.data()
	};
	gpu.graphicsQueue.submit2(submitInfo);

	const vk::PresentInfoKHR presentInfo = {
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = &*sync.presentSems[imgIdx],
		.swapchainCount = 1,
		.pSwapchains = &*swapchain.swapchain,
		.pImageIndices = &imgIdx
	};
	auto presentExpected = gpu.presentQueue.presentKHR(presentInfo);
	if (presentExpected == vk::Result::eErrorOutOfDateKHR || presentExpected == vk::Result::eSuboptimalKHR || recreate) {
		swapchain.recreate(window, gpu);
	} else if (presentExpected != vk::Result::eSuccess) {
		handle_error("Failed to present frame", presentExpected);
	}
}

void Velo::create_texture_sampler() {
	vk::PhysicalDeviceProperties properties = gpu.physicalDevice.getProperties();
	vk::SamplerCreateInfo samplerInfo {
		.magFilter = vk::Filter::eLinear,
		.minFilter = vk::Filter::eLinear,
		.mipmapMode = vk::SamplerMipmapMode::eLinear,
		.addressModeU = vk::SamplerAddressMode::eRepeat,
		.addressModeV = vk::SamplerAddressMode::eRepeat,
		.mipLodBias = 0.0f,
		.anisotropyEnable = vk::True,
		.maxAnisotropy = properties.limits.maxSamplerAnisotropy,
		.compareEnable = vk::False,
		.compareOp = vk::CompareOp::eAlways,
		.minLod = 0.0f,
		.maxLod = 0.0f,
		.borderColor = vk::BorderColor::eIntOpaqueBlack,
		.unnormalizedCoordinates = vk::False
	};
	auto samplerExpected = gpu.device.createSampler(samplerInfo);
	if (!samplerExpected.has_value()) {
		handle_error("Failed to create texture sampler", samplerExpected.result);
	}
	textureSampler = std::move(*samplerExpected);
}

vk::Format find_supported_format(vk::raii::PhysicalDevice& physicalDevice, const std::vector<vk::Format>& candidates, vk::ImageTiling tiling, vk::FormatFeatureFlags features) {
	for (const auto format : candidates) {
		vk::FormatProperties props = physicalDevice.getFormatProperties(format);
		if (tiling == vk::ImageTiling::eLinear && (props.linearTilingFeatures & features) == features) {
			return format;
		}
		if (tiling == vk::ImageTiling::eOptimal && (props.optimalTilingFeatures & features) == features) {
			return format;
		}
	}
	throw std::runtime_error("Failed to find supported format");
}

void Velo::load_model() {
	tinyobj::attrib_t attrib;
	std::vector<tinyobj::shape_t> shapes;
	std::vector<tinyobj::material_t> materials;
	std::string warn, err;
	std::string basedir = "/home/omathot/dev/cpp/velo/models/";

	if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, MODEL_PATH.c_str(), basedir.c_str())) {
		throw std::runtime_error(warn + err);
	}

	std::unordered_map<Vertex, std::uint32_t> uniqueVertices;
	for (const auto& shape: shapes) {
		for (const auto& idx: shape.mesh.indices) {
			Vertex vertex{};
			vertex.pos = {
				attrib.vertices[3 * static_cast<ulong>(idx.vertex_index) + 0],
				attrib.vertices[3 * static_cast<ulong>(idx.vertex_index) + 1],
				attrib.vertices[3 * static_cast<ulong>(idx.vertex_index) + 2]
			};
			if (idx.texcoord_index >= 0) {
				vertex.texCoord = {
					attrib.texcoords[2 * static_cast<ulong>(idx.texcoord_index) + 0],
					1.0f - attrib.texcoords[2 * static_cast<ulong>(idx.texcoord_index) + 1]
				};
			} else {
				vertex.texCoord = {0.0f, 0.0f};
			}
			vertex.color = {1.0f, 1.0f, 1.0f};

			if (!uniqueVertices.contains(vertex)) {
				uniqueVertices[vertex] = static_cast<std::uint32_t>(vertices.size());
				vertices.push_back(vertex);
			}
			// if already seen index to it with vertex
			indices.push_back(uniqueVertices[vertex]);
		}
	}
	std::cout << "Successfully loaded model, uniquevertices = " << vertices.size() << '\n';
}

void Velo::process_input() {
	if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
		position.x -= speed * dt;
	if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
		position.x += speed * dt;
	if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
		position.z -= speed * dt;
	if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
		position.z += speed * dt;
	if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS)
		position.y += speed * dt;
	if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS)
		position.y -= speed * dt;

	static bool spacePressed = false;
	bool spacePress = glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS;
	if (spacePress && !spacePressed) {
		if (rotation == 0) {
			rotation = -1;
		}
		rotation = -rotation;
	}
	spacePressed = spacePress;

	static bool cPressed = false;
	bool cPress = glfwGetKey(window, GLFW_KEY_C) == GLFW_PRESS;
	if (cPress && !cPressed) {
		if (rotation == 0)
			rotation = 1;
		else
			rotation = 0;
	}
	cPressed = cPress;

	static bool minusPressed = false;
	bool minusPress = glfwGetKey(window, GLFW_KEY_MINUS) == GLFW_PRESS;
	if (minusPress && !minusPressed) {
		if (rotationSpeed >= 10)
			rotationSpeed -= 10;
	}
	minusPressed = minusPress;

	static bool plusPressed = false;
	bool plusPress = glfwGetKey(window, GLFW_KEY_EQUAL) == GLFW_PRESS;
	if (plusPress && !plusPressed) {
		if (rotationSpeed <= 140)
			rotationSpeed += 10;
	}
	plusPressed = plusPress;

	if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
		config.should_quit = true;

	double mouseX = 0;
	double mouseY = 0;
	glfwGetCursorPos(window, &mouseX, &mouseY);
}

void Velo::init_default_data() {
	create_texture_sampler();
	if (config.enabled_codam) {
		create_material_images();
		create_texture_material_views();
		load_model_per_face_material();
	} else {
		create_texture_image();
		create_texture_image_view();
		load_model();
	}
	create_vertex_buffer();
	create_index_buffer();
	if (config.enabled_codam) {
		create_material_index_buffer();
	} else {
		// yes this is ugly. just temporary for codam
		create_dummy_material_index_buffer();
	}
}

void Velo::create_material_images() {
	std::vector<glm::vec3> colors = {
		{1.0f, 1.0f, 1.0},
		{0.8f, 0.8f, 0.8f},
		{0.6f, 0.6f, 0.6f},
		{0.4f, 0.4f, 0.4f},
	};
	for (std::uint32_t i = 0; i < colors.size(); i++) {
		std::array<uint8_t, 4> pixels = {
			static_cast<uint8_t>(colors[i].r * 255.0f),
			static_cast<uint8_t>(colors[i].g * 255.0f),
			static_cast<uint8_t>(colors[i].b * 255.0f),
			255
		};
		int texW = 1;
		int texH = 1;
		mipLvls = 1;
		vk::DeviceSize imgSize = 4;
		VmaBuffer stagingBuff = VmaBuffer(gpu.allocator, imgSize, vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_AUTO, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
		void* data = nullptr;
		vmaMapMemory(gpu.allocator, stagingBuff.allocation(), &data);
		std::memcpy(data, pixels.data(), imgSize);
		vmaUnmapMemory(gpu.allocator, stagingBuff.allocation());

		materialImages.emplace_back(gpu.allocator, static_cast<std::uint32_t>(texW), static_cast<std::uint32_t>(texH), 1, vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eSampled, vk::Format::eR8G8B8A8Srgb, VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT);
		// materialImages.push_back(VmaImage(allocator, static_cast<std::uint32_t>(texW), static_cast<std::uint32_t>(texH), 1, vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eSampled, vk::Format::eR8G8B8A8Srgb, VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT));
		std::println("Successfully created CODAM image");

		transition_image_texture_layout(materialImages[i], vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal, 1);
		copy_buffer_to_image(stagingBuff, materialImages[i], static_cast<std::uint32_t>(texW), static_cast<std::uint32_t>(texH));
		transition_image_texture_layout(materialImages[i], vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, 1);
	}
}

void Velo::create_texture_material_views() {
	std::vector<vk::raii::ImageView> matviews;
	std::vector<vk::DescriptorImageInfo> imageInfos;
	for (std::uint32_t i = 0; i < materialImages.size(); i++) {
		auto matView = create_image_view(gpu.device, materialImages[i].image(), vk::Format::eR8G8B8A8Srgb, vk::ImageAspectFlagBits::eColor, 1);
		materialImageViews.push_back(std::move(matView));
		imageInfos.push_back({
			.sampler = textureSampler,
			.imageView = materialImageViews[i],
			.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
		});
	}

	std::vector<vk::WriteDescriptorSet> writes;
	writes.reserve(imageInfos.size());
	for (std::uint32_t i = 0; i < imageInfos.size(); i++) {
		writes.push_back({
			.dstSet = *descriptors.set,
			.dstBinding = 1,
			.dstArrayElement = i,
			.descriptorCount = 1,
			.descriptorType = vk::DescriptorType::eCombinedImageSampler,
			.pImageInfo = &imageInfos[i]
		});
	}
	gpu.device.updateDescriptorSets(writes, nullptr);
}

void Velo::load_model_per_face_material() {
	tinyobj::attrib_t attrib;
	std::vector<tinyobj::shape_t> shapes;
	std::vector<tinyobj::material_t> materials;
	std::string warn, err;
	std::string basedir = "/home/omathot/dev/cpp/velo/models/";

	if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, MODEL_PATH.c_str(), basedir.c_str())) {
		throw std::runtime_error(warn + err);
	}

	std::unordered_map<Vertex, std::uint32_t> uniqueVertices;
	std::uint32_t globalFaceIdx = 0;
	materialIndices.clear();

	for (const auto& shape: shapes) {
		std::uint32_t idxOffset = 0;
		for (std::uint32_t faceIdx = 0; faceIdx < shape.mesh.num_face_vertices.size(); faceIdx++) {
			int matId = static_cast<int>(globalFaceIdx) % 4;
			if (std::max(matId, 0) == 0)
				matId = 0;

			materialIndices.push_back(static_cast<std::uint32_t>(matId));
			size_t numVerts = shape.mesh.num_face_vertices[faceIdx];
			for (size_t i = 0; i < numVerts; i++) {
				const auto& idx = shape.mesh.indices[idxOffset + i];
				Vertex vertex{};
				vertex.pos = {
					attrib.vertices[3 * static_cast<ulong>(idx.vertex_index) + 0],
					attrib.vertices[3 * static_cast<ulong>(idx.vertex_index) + 1],
					attrib.vertices[3 * static_cast<ulong>(idx.vertex_index) + 2]
				};
				if (idx.texcoord_index >= 0) {
					vertex.texCoord = {
						attrib.texcoords[2 * static_cast<ulong>(idx.texcoord_index) + 0],
						1.0f - attrib.texcoords[2 * static_cast<ulong>(idx.texcoord_index) + 1]
					};
				} else {
					vertex.texCoord = {0.0f, 0.0f};
				}
				vertex.color = {1.0f, 1.0f, 1.0f};

				if (!uniqueVertices.contains(vertex)) {
					uniqueVertices[vertex] = static_cast<std::uint32_t>(vertices.size());
					vertices.push_back(vertex);
				}
				// if already seen index to it with vertex
				indices.push_back(uniqueVertices[vertex]);
			}
			idxOffset += numVerts;
			globalFaceIdx++;
		}
	}
	std::cout << "Successfully loaded model, uniquevertices = " << vertices.size() << '\n';
}

void Velo::create_dummy_material_index_buffer() {
    vk::DeviceSize buffSize = sizeof(std::uint32_t);
    materialIdxBuff = VmaBuffer(gpu.allocator, buffSize,
        vk::BufferUsageFlagBits::eStorageBuffer,
        VMA_MEMORY_USAGE_AUTO,
        VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT);

    vk::DescriptorBufferInfo matBuffInfo {
        .buffer = materialIdxBuff.buffer(),
        .offset = 0,
        .range = buffSize
    };
    vk::WriteDescriptorSet writeSet {
        .dstSet = *descriptors.set,
        .dstBinding = 2,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = vk::DescriptorType::eStorageBuffer,
        .pBufferInfo = &matBuffInfo
    };
    gpu.device.updateDescriptorSets(writeSet, nullptr);
}

void FrameContext::create(GpuContext& gpu, vk::DescriptorSet dstSet, std::uint32_t frameIdx) {
	vk::CommandBufferAllocateInfo allocInfo {
		.commandPool = *gpu.cmdPool,
		.level = vk::CommandBufferLevel::ePrimary,
		.commandBufferCount = 1
	};

	auto cmdBuffExpected = gpu.device.allocateCommandBuffers(allocInfo);
	if (!cmdBuffExpected.has_value()) {
		handle_error("Failed to allocate cmd buffer", cmdBuffExpected.result);
	}
	cmdBuffer = std::move(cmdBuffExpected->front());

	auto acquireSemExpected = gpu.device.createSemaphore(vk::SemaphoreCreateInfo{});
	if (!acquireSemExpected.has_value()) {
		handle_error("Failed to create render semaphore", acquireSemExpected.result);
	}
	acquireSem = std::move(*acquireSemExpected);

	uniformBuffer = VmaBuffer(gpu.allocator, sizeof(UniformBufferObject), vk::BufferUsageFlagBits::eUniformBuffer, VMA_MEMORY_USAGE_AUTO, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);
	uniformBufferMapped = uniformBuffer.mapped_data();

	vk::DescriptorBufferInfo buffInfo {
		.buffer = uniformBuffer.buffer(),
		.offset = 0,
		.range = sizeof(UniformBufferObject)
	};
	vk::WriteDescriptorSet writes {
		.dstSet = dstSet,
		.dstBinding = 0,
		.dstArrayElement = frameIdx,
		.descriptorCount = 1,
		.descriptorType = vk::DescriptorType::eUniformBuffer,
		.pBufferInfo = &buffInfo
	};
	gpu.device.updateDescriptorSets(writes, nullptr);
}

void SyncContext::create(vk::raii::Device& device, std::uint32_t swapchainImgCount) {
	presentSems.clear();
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

	for (uint32_t i = 0; i < swapchainImgCount; i++) {
		presentSems.push_back(device.createSemaphore({}).value);
	}
}

void SyncContext::wait_for_frame(vk::raii::Device& device, std::uint64_t frameCount) const {
	uint64_t waitValue = 0;
	if (frameCount > MAX_FRAMES_IN_FLIGHT) {
		waitValue = frameCount - MAX_FRAMES_IN_FLIGHT;
	}
	vk::SemaphoreWaitInfo waitInfo = {
		.semaphoreCount = 1,
		.pSemaphores = &*timelineSem,
		.pValues = &waitValue
	};
	auto waitExpected = device.waitSemaphores(waitInfo, UINT64_MAX);
	if (waitExpected != vk::Result::eSuccess) {
		handle_error("Failed to wait for semaphore", waitExpected);
	}
}

void SyncContext::signal_timeline(vk::raii::Device& device, std::uint64_t value) const {
	// dummy signal (yay documentation)
	vk::SemaphoreSignalInfo signalInfo {
		.semaphore = *timelineSem,
		.value = value
	};
	device.signalSemaphore(signalInfo);
}

// util
// unused for now
// static bool has_stencil_component(vk::Format fmt) {
// 	return fmt == vk::Format::eD32SfloatS8Uint || fmt == vk::Format::eD24UnormS8Uint;
// }
//
