module;
#include <GLFW/glfw3.h>
#include <glm/fwd.hpp>
#include <vk_mem_alloc.h>
#include <tiny_obj_loader.h>
//
#include <print>
#include <cstring>
#include <unordered_map>
#include <iostream>
#include <cstdint>
#include <vector>
#include <stdexcept>
#include <utility>
#include <print>


module velo;
// import std;
import vulkan_hpp;

Velo::Velo() {
	std::println("Constructing Velo");
	#if defined(CODAM)
		std::println("\tEnabled codam mode");
		vcontext.enable_codam();
	#endif
	#if defined(X11)
		std::println("\tEnabled X11 mode");
		vcontext.enable_x11();
	#endif
	#if defined(INFOS)
		std::println("\tEnabled Info Fetching");
		vcontext.is_info_gathered();
	#endif
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
	init_env();
	init_swapchain();
	init_commands();
	create_sync_objects();
	init_descriptors();
	create_graphics_pipeline();
	init_default_data();
}

void Velo::main_loop() {
	while (!glfwWindowShouldClose(window) && !vcontext.should_quit) {
		glfwPollEvents();
		process_input();
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
	materialIdxBuff = VmaBuffer{};
	textureImage = VmaImage{};
	depthImage = VmaImage{};
	materialImages.clear();
	vmaDestroyAllocator(allocator);
	/*
		we delete window manually here before raii destructors run
		surface won't have valid wayland surface -> segfault
		this might be wayland only?
	*/
	surface.clear();

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
	cmdBuffers[frameIdx].pipelineBarrier2(depInfo);
}

void Velo::draw_frame() {
	uint64_t timelineValue = ++frameCount;
	frameIdx = (timelineValue - 1) % MAX_FRAMES_IN_FLIGHT;
	uint64_t waitValue = 0;
	if (timelineValue > MAX_FRAMES_IN_FLIGHT) {
		waitValue = timelineValue - MAX_FRAMES_IN_FLIGHT;
	}
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
	bool recreate = nextImgExpected.result == vk::Result::eSuboptimalKHR;
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
	if (!nextImgExpected.has_value() && !recreate) {
		handle_error("Failed to acquire next swapchain image", nextImgExpected.result);
	}
	auto imgIdx = nextImgExpected.value;
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
	if (presentExpected == vk::Result::eErrorOutOfDateKHR || presentExpected == vk::Result::eSuboptimalKHR || recreate) {
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
	vk::CommandBufferBeginInfo beginInfo {
		.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit
	};
	cmdBuff.begin(beginInfo);

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

void Velo::create_texture_sampler() {
	vk::PhysicalDeviceProperties properties = physicalDevice.getProperties();
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
	auto samplerExpected = device.createSampler(samplerInfo);
	if (!samplerExpected.has_value()) {
		handle_error("Failed to create texture sampler", samplerExpected.result);
	}
	textureSampler = std::move(*samplerExpected);
}

void Velo::create_depth_resources() {
	vk::Format depthFmt = find_depth_format();
	depthImage = VmaImage(allocator, swapchainExtent.width, swapchainExtent.height, 1, vk::ImageUsageFlagBits::eDepthStencilAttachment, depthFmt,  VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT, VMA_MEMORY_USAGE_AUTO);
	depthImageView = create_image_view(depthImage.image(), depthFmt, vk::ImageAspectFlagBits::eDepth, 1);
}

vk::Format Velo::find_supported_format(const std::vector<vk::Format>& candidates, vk::ImageTiling tiling, vk::FormatFeatureFlags features) {
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


vk::Format Velo::find_depth_format() {
	return find_supported_format(
		{vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint, vk::Format::eD24UnormS8Uint},
		vk::ImageTiling::eOptimal,
		vk::FormatFeatureFlagBits::eDepthStencilAttachment
	);
}
bool Velo::has_stencil_component(vk::Format fmt) {
	return fmt == vk::Format::eD32SfloatS8Uint || fmt == vk::Format::eD24UnormS8Uint;
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

	std::unordered_map<Vertex, uint32_t> uniqueVertices;
	for (const auto& shape: shapes) {
		for (const auto& idx: shape.mesh.indices) {
			Vertex vertex{};
			vertex.pos = {
				attrib.vertices[3 * idx.vertex_index + 0],
				attrib.vertices[3 * idx.vertex_index + 1],
				attrib.vertices[3 * idx.vertex_index + 2]
			};
			if (idx.texcoord_index >= 0) {
				vertex.texCoord = {
					attrib.texcoords[2 * idx.texcoord_index + 0],
					1.0f - attrib.texcoords[2 * idx.texcoord_index + 1]
				};
			} else {
				vertex.texCoord = {0.0f, 0.0f};
			}
			vertex.color = {1.0f, 1.0f, 1.0f};

			if (uniqueVertices.count(vertex) == 0) {
				uniqueVertices[vertex] = static_cast<uint32_t>(vertices.size());
				vertices.push_back(vertex);
			}
			// if already seen index to it with vertex
			indices.push_back(uniqueVertices[vertex]);
		}
	}
	std::cout << "Successfully loaded model, uniquevertices = " << vertices.size() << std::endl;;
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
		vcontext.should_quit = true;

	double mouseX, mouseY;
	glfwGetCursorPos(window, &mouseX, &mouseY);
}

void Velo::init_env() {
	create_instance();
	setup_debug_messenger();
	create_surface();
	pick_physical_device();
	create_logical_device();
	init_vma();
}

void Velo::init_swapchain() {
	create_swapchain();
	create_image_views();
}
void Velo::init_commands() {
	create_command_pool();
	create_command_buffer();
}
void Velo::init_descriptors() {
	create_descriptor_set_layout();
	create_descriptor_pools();
	create_descriptor_sets();
}

void Velo::init_default_data() {
	create_texture_sampler();
	if (vcontext.enabled_codam) {
		create_material_images();
		create_texture_material_views();
		load_model_per_face_material();
	} else {
		create_texture_image();
		create_texture_image_view();
		load_model();
	}
	create_depth_resources();
	create_vertex_buffer();
	create_index_buffer();
	create_uniform_buffers();
	if (vcontext.enabled_codam) {
		create_material_index_buffer();
	}
}

void Velo::create_material_images() {
	// tinyobj::attrib_t attrib;
	// std::map<std::string, int> material_map;
	// std::string baseDir = "textures/";
	// std::vector<tinyobj::material_t> materials;
	// std::string warning, err;
	// std::ifstream mtlstream(TEXTURE_PATH.c_str());

	std::vector<glm::vec3> colors = {
		{1.0f, 1.0f, 1.0},
		{0.8f, 0.8f, 0.8f},
		{0.6f, 0.6f, 0.6f},
		{0.4f, 0.4f, 0.4f},
	};
	for (uint32_t i = 0; i < colors.size(); i++) {
		uint8_t pixels[4] = {
			static_cast<uint8_t>(colors[i].r * 255.0f),
			static_cast<uint8_t>(colors[i].g * 255.0f),
			static_cast<uint8_t>(colors[i].b * 255.0f),
			255
		};
		int texW = 1;
		int texH = 1;
		mipLvls = 1;
		vk::DeviceSize imgSize = 4;
		VmaBuffer stagingBuff = VmaBuffer(allocator, imgSize, vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_AUTO, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
		void* data;
		vmaMapMemory(allocator, stagingBuff.allocation(), &data);
		memcpy(data, pixels, imgSize);
		vmaUnmapMemory(allocator, stagingBuff.allocation());

		materialImages.push_back(VmaImage(allocator, texW, texH, 1, vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eSampled, vk::Format::eR8G8B8A8Srgb, VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT));
		std::println("Successfully created CODAM image");

		transition_image_texture_layout(materialImages[i], vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal, 1);
		copy_buffer_to_image(stagingBuff, materialImages[i], static_cast<uint32_t>(texW), static_cast<uint32_t>(texH));
		transition_image_texture_layout(materialImages[i], vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, 1);
	}

	// if (!mtlstream) {
	// 	throw std::runtime_error("failed to open mtl file");
	// }
	// tinyobj::LoadMtl(&material_map, &materials, &mtlstream, &warning, &err);
	// if (!warning.empty()) {
	// 	std::cout << "mtl warning: " << warning << std::endl;
	// }
	// if (!err.empty()) {
	// 	throw std::runtime_error("mtl error: " + err);
	// }
	// if (materials.empty()) {
	// 	throw std::runtime_error("No materials found in MTL file");
	// }

}

void Velo::create_texture_material_views() {
	std::vector<vk::raii::ImageView> matviews;
	std::vector<vk::DescriptorImageInfo> imageInfos;
	for (uint32_t i = 0; i < materialImages.size(); i++) {
		auto matView = create_image_view(materialImages[i].image(), vk::Format::eR8G8B8A8Srgb, vk::ImageAspectFlagBits::eColor, 1);
		materialImageViews.push_back(std::move(matView));
		imageInfos.push_back({
			.sampler = textureSampler,
			.imageView = materialImageViews[i],
			.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
		});
	}

	std::vector<vk::WriteDescriptorSet> writes;
	for (uint32_t i = 0; i < imageInfos.size(); i++) {
		writes.push_back({
			.dstSet = *descriptorSets,
			.dstBinding = 1,
			.dstArrayElement = i,
			.descriptorCount = 1,
			.descriptorType = vk::DescriptorType::eCombinedImageSampler,
			.pImageInfo = &imageInfos[i]
		});
	}
	device.updateDescriptorSets(writes, nullptr);
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

	std::unordered_map<Vertex, uint32_t> uniqueVertices;
	uint32_t globalFaceIdx = 0;
	materialIndices.clear();

	for (const auto& shape: shapes) {
		uint32_t idxOffset = 0;
		for (uint32_t faceIdx = 0; faceIdx < shape.mesh.num_face_vertices.size(); faceIdx++) {
			int matId = globalFaceIdx % 4;
			if (matId < 0)
				matId = 0;

			materialIndices.push_back(static_cast<uint32_t>(matId));
			size_t numVerts = shape.mesh.num_face_vertices[faceIdx];
			for (size_t i = 0; i < numVerts; i++) {
				const auto& idx = shape.mesh.indices[idxOffset + i];
				Vertex vertex{};
				vertex.pos = {
					attrib.vertices[3 * idx.vertex_index + 0],
					attrib.vertices[3 * idx.vertex_index + 1],
					attrib.vertices[3 * idx.vertex_index + 2]
				};
				if (idx.texcoord_index >= 0) {
					vertex.texCoord = {
						attrib.texcoords[2 * idx.texcoord_index + 0],
						1.0f - attrib.texcoords[2 * idx.texcoord_index + 1]
					};
				} else {
					vertex.texCoord = {0.0f, 0.0f};
				}
				vertex.color = {1.0f, 1.0f, 1.0f};

				if (uniqueVertices.count(vertex) == 0) {
					uniqueVertices[vertex] = static_cast<uint32_t>(vertices.size());
					vertices.push_back(vertex);
				}
				// if already seen index to it with vertex
				indices.push_back(uniqueVertices[vertex]);
			}
			idxOffset += numVerts;
			globalFaceIdx++;
		}
	}
	std::cout << "Successfully loaded model, uniquevertices = " << vertices.size() << std::endl;;
}
