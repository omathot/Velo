module;
#include <vk_mem_alloc.h>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

module velo;
import std;
import vulkan_hpp;

vk::raii::ImageView create_image_view(vk::raii::Device& device, const vk::Image& img, vk::Format fmt, vk::ImageAspectFlags aspectFlags, std::uint32_t mips) {
	vk::ImageViewCreateInfo viewInfo {
		.image = img,
		.viewType = vk::ImageViewType::e2D,
		.format = fmt,
		.subresourceRange = {
			.aspectMask = aspectFlags,
			.baseMipLevel = 0,
			.levelCount = mips,
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

void Velo::create_texture_image() {
	int texWidth = 0, texHeight = 0, texChannels = 0;
	stbi_uc* pixels{};
	pixels = stbi_load(TEXTURE_PATH.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
	if (!pixels) {
		throw std::runtime_error("Failed to load pixels from texture");
	}
	vk::DeviceSize imgSize = static_cast<vk::DeviceSize>(texWidth * texHeight) * 4; // 4 bytes per pixel
	// mipLvls = static_cast<std::uint32_t>(std::floor(std::log2(std::max(texWidth, texHeight)))) + 1;
	mipLvls = 1;

	VmaBuffer stagingBuffer = VmaBuffer(gpu.allocator, imgSize, vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_AUTO, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
	void* data = nullptr;
	vmaMapMemory(gpu.allocator, stagingBuffer.allocation(), &data);
	std::memcpy(data, pixels, imgSize);
	vmaUnmapMemory(gpu.allocator, stagingBuffer.allocation());

	stbi_image_free(pixels);
	textureImage = VmaImage(gpu.allocator, static_cast<std::uint32_t>(texWidth), static_cast<std::uint32_t>(texHeight), mipLvls, vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eSampled, vk::Format::eR8G8B8A8Srgb,  VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT, VMA_MEMORY_USAGE_AUTO);
	std::cout << "Successfully created image\n";

	transition_image_texture_layout(textureImage, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal, mipLvls);
	copy_buffer_to_image(stagingBuffer, textureImage, static_cast<std::uint32_t>(texWidth), static_cast<std::uint32_t>(texHeight));
	transition_image_texture_layout(textureImage, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, mipLvls);
}

void Velo::transition_image_texture_layout(VmaImage& img, vk::ImageLayout oldLayout, vk::ImageLayout newLayout, std::uint32_t mips) {
	auto cmdBuff = gpu.begin_single_time_commands();

	vk::ImageMemoryBarrier2 barrier = {
		.oldLayout = oldLayout,
		.newLayout = newLayout,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = img.image(),
		.subresourceRange = {
			.aspectMask = vk::ImageAspectFlagBits::eColor,
			.baseMipLevel = 0,
			.levelCount = mips,
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

	gpu.end_single_time_commands(cmdBuff);
}

void Velo::create_texture_image_view() {
	textureImageView = create_image_view(gpu.device, textureImage.image(), vk::Format::eR8G8B8A8Srgb, vk::ImageAspectFlagBits::eColor, mipLvls);
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
	gpu.device.updateDescriptorSets(writes, nullptr);
}

void Velo::copy_buffer_to_image(const VmaBuffer& buff, VmaImage& img, std::uint32_t width, std::uint32_t height) {
	auto cmdBuff = gpu.begin_single_time_commands();
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
		.imageOffset = {0, 0, 0}, // NOLINT
		.imageExtent = {width, height, 1} // NOLINT
	};
	vk::CopyBufferToImageInfo2 imgInfo {
		.srcBuffer = buff.buffer(),
		.dstImage = img.image(),
		.dstImageLayout = vk::ImageLayout::eTransferDstOptimal,
		.regionCount = 1,
		.pRegions = &region
	};
	cmdBuff.copyBufferToImage2(imgInfo);

	gpu.end_single_time_commands(cmdBuff);
}
