module;
#include <GLFW/glfw3.h>
#include <vk_mem_alloc.h>
#include <tiny_obj_loader.h>
//
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
		enable_codam();
	#endif
	#if defined(X11)
		std::println("\tEnabled X11 mode");
		enable_x11();
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
	create_depth_resources();
	if (vcontext.enabled_codam) {
		create_texture_from_mtl();
	} else {
		create_texture_image();
	}
	create_texture_sampler();
	create_texture_image_view();
	load_model();
	create_vertex_buffer();
	create_index_buffer();
	create_uniform_buffers();
	create_command_buffer();
	create_sync_objects();
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
	textureImage = VmaImage{};
	depthImage = VmaImage{};
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
			indices.push_back(uniqueVertices[vertex]);
		}
	}
	std::cout << "Successfully loaded model, total vertices = " << vertices.size() << std::endl;;
}

void Velo::create_texture_from_mtl() {
	tinyobj::attrib_t attrib;
	std::map<std::string, int> material_map;
	std::string baseDir = "textures/";
	std::vector<tinyobj::material_t> materials;
	std::string warning, err;
	std::ifstream mtlstream(TEXTURE_PATH.c_str());
	if (!mtlstream) {
		throw std::runtime_error("failed to open mtl file");
	}
	tinyobj::LoadMtl(&material_map, &materials, &mtlstream, &warning, &err);
	if (!warning.empty()) {
		std::cout << "mtl warning: " << warning << std::endl;
	}
	if (!err.empty()) {
		throw std::runtime_error("mtl error: " + err);
	}
	if (materials.empty()) {
		throw std::runtime_error("No materials found in MTL file");
	}

	tinyobj::material_t& mat = materials[0];
	uint8_t pixels[4] = {
		static_cast<uint8_t>(mat.diffuse[0] * 255),
		static_cast<uint8_t>(mat.diffuse[1] * 255),
		static_cast<uint8_t>(mat.diffuse[2] * 255),
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

	textureImage = VmaImage(allocator, texW, texH, mipLvls, vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eSampled, vk::Format::eR8G8B8A8Srgb, VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT);
	std::cout << "Successfully created CODAM image\n";

	transition_image_texture_layout(textureImage, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal, mipLvls);
	copy_buffer_to_image(stagingBuff, textureImage, static_cast<uint32_t>(texW), static_cast<uint32_t>(texH));
	transition_image_texture_layout(textureImage, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, mipLvls);
}

void Velo::process_input() {
	if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
		position.x -= speed * dt;
	if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
		position.x += speed * dt;
	if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
		position.y += speed * dt;
	if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
		position.y -= speed * dt;
	if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
		vcontext.should_quit = true;
	if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS)
		position.z -= speed * dt;
	if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS)
		position.z += speed * dt;

	double mouseX, mouseY;
	glfwGetCursorPos(window, &mouseX, &mouseY);
	std::println("Mouse: {},{}", mouseX, mouseY);
	std::cout << "Mouse: " << mouseX << ", " << mouseY << "\n";
}
