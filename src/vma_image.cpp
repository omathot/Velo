module;
#include <vk_mem_alloc.h>

module velo;
import std;

VmaImage::VmaImage(VmaAllocator allocator, std::uint32_t width, std::uint32_t height, std::uint32_t mipLvls, vk::ImageUsageFlags usage, vk::Format fmt, VmaAllocationCreateFlags flags, VmaMemoryUsage vmaMemUsage) : vmaAllocator(allocator) {
	vk::ImageCreateInfo buffInfo {
		.imageType = vk::ImageType::e2D,
		.format = fmt,
		.extent = {width, height, 1}, // NOLINT
		.mipLevels = mipLvls,
		.arrayLayers = 1,
		.samples =vk::SampleCountFlagBits::e1,
		.tiling = vk::ImageTiling::eOptimal,
		.usage = usage,
		.sharingMode = vk::SharingMode::eExclusive
	};
	VmaAllocationCreateInfo allocCreateInfo = {};
	allocCreateInfo.usage = vmaMemUsage;
	allocCreateInfo.flags = flags;
	VmaAllocationInfo allocInfo;
	VkResult res = vmaCreateImage(vmaAllocator, buffInfo, &allocCreateInfo, &_image, &vmaAllocation, &allocInfo);
	if (res != VK_SUCCESS) {
		handle_error("Failed to create vma buffer", vk::Result(res));
	}
	mapped = allocInfo.pMappedData;
}

VmaImage::~VmaImage() {
	if (_image) {
		vmaDestroyImage(vmaAllocator, _image, vmaAllocation);
	}

}
VmaImage::VmaImage(VmaImage&& other) noexcept : vmaAllocator(other.vmaAllocator), _image(other._image), vmaAllocation(other.vmaAllocation), mapped(other.mapped) {
	other._image = VK_NULL_HANDLE;
	other.vmaAllocation = VK_NULL_HANDLE;
	other.mapped = nullptr;
}

VmaImage& VmaImage::operator=(VmaImage&& other) noexcept {
	if (this != &other) {
		if (_image && vmaAllocator) {
			vmaDestroyImage(vmaAllocator, _image, vmaAllocation);
		}
		_image = other._image;
		mapped = other.mapped;
		vmaAllocator = other.vmaAllocator;
		vmaAllocation = other.vmaAllocation;

		other._image = VK_NULL_HANDLE;
		other.vmaAllocation = VK_NULL_HANDLE;
		other.mapped = nullptr;
	}
	return *this;
}

VkImage VmaImage::get() const {
	return _image;
}
vk::Image VmaImage::image() const {
	return {_image};
}
VmaAllocation VmaImage::allocation() const {
	return vmaAllocation;
}
void* VmaImage::mapped_data() const {
	return mapped;
}


