module;
#include <GLFW/glfw3.h>
#include <vk_mem_alloc.h>
#include <tiny_obj_loader.h>
//
#include <algorithm>
#include <cmath>
#include <iostream>
#include <cstdint>
#include <unordered_map>
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

void Velo::set_codam_mode() {
	codam_mode = true;
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
	create_image_views(); // this
	create_descriptor_set_layout();
	create_descriptor_pools();
	create_descriptor_sets();
	create_graphics_pipeline();
	create_command_pool();
	create_depth_resources();
	create_texture_image();
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
	textureImage = VmaImage{};
	depthImage = VmaImage{};
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

void Velo::create_texture_image() {
	int texWidth = 0, texHeight = 0, texChannels = 0;
	stbi_uc* pixels{};
	// if (codam_mode) {
		// tinyobj::LoadMtl(std::map<std::string, int> *material_map, std::vector<material_t> *materials, std::istream *inStream, std::string *warning, std::string *err)
	// } else {
		pixels = stbi_load(TEXTURE_PATH.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
		if (!pixels) {
			throw std::runtime_error("Failed to load pixels from texture");
		}
	// }
	vk::DeviceSize imgSize = texWidth * texHeight * 4; // 4 bytes per pixel
	mipLvls = static_cast<uint32_t>(std::floor(std::log2(std::max(texWidth, texHeight)))) + 1;

	VmaBuffer stagingBuffer = VmaBuffer(allocator, imgSize, vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_AUTO, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
	void* data;
	vmaMapMemory(allocator, stagingBuffer.allocation(), &data);
	memcpy(data, pixels, imgSize);
	vmaUnmapMemory(allocator, stagingBuffer.allocation());

	stbi_image_free(pixels);
	textureImage = VmaImage(allocator, texWidth, texHeight, mipLvls, vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eSampled, vk::Format::eR8G8B8A8Srgb,  VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT, VMA_MEMORY_USAGE_AUTO);
	std::cout << "Successfully created image\n";

	transition_image_texture_layout(textureImage, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal, mipLvls);
	copy_buffer_to_image(stagingBuffer, textureImage, static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight));
	transition_image_texture_layout(textureImage, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, mipLvls);
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

void Velo::transition_image_texture_layout(VmaImage& img, vk::ImageLayout oldLayout, vk::ImageLayout newLayout, uint32_t mipLvls) {
	auto cmdBuff = begin_single_time_commands();

	vk::ImageMemoryBarrier2 barrier = {
		.oldLayout = oldLayout,
		.newLayout = newLayout,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = img.image(),
		.subresourceRange = {
			.aspectMask = vk::ImageAspectFlagBits::eColor,
			.baseMipLevel = 0,
			.levelCount = mipLvls,
			.baseArrayLayer = 0,
			.layerCount = 1
		}
	};
	if (oldLayout == vk::ImageLayout::eUndefined && newLayout == vk::ImageLayout::eTransferDstOptimal) {
		barrier.srcAccessMask = {};
		barrier.dstAccessMask = vk::AccessFlagBits2::eTransferWrite;
		barrier.srcStageMask= vk::PipelineStageFlagBits2::eTopOfPipe;
		barrier.dstStageMask = vk::PipelineStageFlagBits2::eTransfer;
	} else if(oldLayout == vk::ImageLayout::eTransferDstOptimal && newLayout == vk::ImageLayout::eShaderReadOnlyOptimal) {
		barrier.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
		barrier.dstAccessMask = vk::AccessFlagBits2::eShaderRead;
		barrier.srcStageMask = vk::PipelineStageFlagBits2::eTransfer;
		barrier.dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader;
	} else {
		throw std::invalid_argument("Unsupposed layout transition");
	}

	vk::DependencyInfo depInfo = {
		.dependencyFlags = {},
		.imageMemoryBarrierCount = 1,
		.pImageMemoryBarriers = &barrier,
	};
	cmdBuff.pipelineBarrier2(depInfo);

	end_single_time_commands(cmdBuff);

}

void Velo::copy_buffer_to_image(const VmaBuffer& buff, VmaImage& img, uint32_t width, uint32_t height) {
	auto cmdBuff = begin_single_time_commands();
	vk::BufferImageCopy2 region {
		.bufferOffset = 0,
		.bufferRowLength = 0,
		.bufferImageHeight = 0,
		.imageSubresource = {
			.aspectMask = vk::ImageAspectFlagBits::eColor,
			.mipLevel = 0,
			.baseArrayLayer = 0,
			.layerCount = 1
		},
		.imageOffset = {0, 0, 0},
		.imageExtent = {width, height, 1}
	};
	vk::CopyBufferToImageInfo2 imgInfo {
		.srcBuffer = buff.buffer(),
		.dstImage = img.image(),
		.dstImageLayout = vk::ImageLayout::eTransferDstOptimal,
		.regionCount = 1,
		.pRegions = &region
	};
	cmdBuff.copyBufferToImage2(imgInfo);

	end_single_time_commands(cmdBuff);
}

void Velo::create_texture_image_view() {
	textureImageView = create_image_view(textureImage.image(), vk::Format::eR8G8B8A8Srgb, vk::ImageAspectFlagBits::eColor, mipLvls);
	vk::DescriptorImageInfo imageInfo {
		.sampler = *textureSampler,
		.imageView = textureImageView,
		.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
	};
	// could just do like for ubos
	// make views and arrayElem = i
	vk::WriteDescriptorSet writes {
		.dstSet = *descriptorSets,
		.dstBinding = 1,
		.dstArrayElement = 0,
		.descriptorCount = 1,
		.descriptorType = vk::DescriptorType::eCombinedImageSampler,
		.pImageInfo = &imageInfo
	};
	device.updateDescriptorSets(writes, nullptr);
}

vk::raii::ImageView Velo::create_image_view(const vk::Image& img, vk::Format fmt, vk::ImageAspectFlags aspectFlags, uint32_t mipLvls) {
	vk::ImageViewCreateInfo viewInfo {
		.image = img,
		.viewType = vk::ImageViewType::e2D,
		.format = fmt,
		.subresourceRange = {
			.aspectMask = aspectFlags,
			.baseMipLevel = 0,
			.levelCount = mipLvls,
			.baseArrayLayer = 0,
			.layerCount = 1
		}
	};
	auto imgViewExpected = device.createImageView(viewInfo);
	if (!imgViewExpected.has_value()) {
		handle_error("Failed to create image view", imgViewExpected.result);
	}
	return (std::move(*imgViewExpected));

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
			vertex.texCoord = {
				attrib.texcoords[2 * idx.texcoord_index + 0],
				1.0f - attrib.texcoords[2 * idx.texcoord_index + 1]
			};
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

// void Velo::load_material_textures(const std::vector<tinyobj::material_t>& materials) {
// 	std::vector<vk::DescriptorImageInfo> imageInfos;

// 	// for (const auto& material: materials) {
// 	// 	VmaImage texture;
// 	// 	vk::raii::ImageView view = nullptr;

// 	// 	if (!material.diffuse_texname.empty()) {
// 	// 		texture =
// 	// 	}
// 	// }
// }

// void Velo::load_material() {
// 	tinyobj::ObjReader reader;
// 	tinyobj::ObjReaderConfig config;
// 	config.mtl_search_path = "textures/";

// 	if (!reader.ParseFromFile(TEXTURE_PATH.c_str(), config)) {
// 		throw std::runtime_error(reader.Warning() + reader.Error());
// 	}

// 	const auto& attrib = reader.GetAttrib();
// 	const auto& shaoes = reader.GetShapes();
// 	const auto& materials = reader.GetMaterials();

// 	load_material_textures(materials);
// }
