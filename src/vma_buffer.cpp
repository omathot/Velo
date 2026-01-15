module;
#include <vk_mem_alloc.h>

module velo;
import vulkan_hpp;

VmaBuffer::VmaBuffer(VmaAllocator allocator, vk::DeviceSize size, vk::BufferUsageFlags usage, VmaMemoryUsage vmaMemUsage, VmaAllocationCreateFlags flags) : vmaAllocator(allocator) {
	vk::BufferCreateInfo buffInfo {
		.size = size,
		.usage = usage,
		.sharingMode = vk::SharingMode::eExclusive
	};
	VmaAllocationCreateInfo allocCreateInfo = {};
	allocCreateInfo.usage = vmaMemUsage;
	allocCreateInfo.flags = flags;
	VmaAllocationInfo allocInfo;
	VkResult res = vmaCreateBuffer(vmaAllocator, buffInfo, &allocCreateInfo, &_buffer, &vmaAllocation, &allocInfo);
	if (res != VK_SUCCESS) {
		handle_error("Failed to create vma buffer", vk::Result(res));
	}
	mapped = allocInfo.pMappedData;
}
VmaBuffer::~VmaBuffer() {
	if (_buffer) {
		vmaDestroyBuffer(vmaAllocator, _buffer, vmaAllocation);
	}
}
VmaBuffer::VmaBuffer(VmaBuffer&& other) noexcept : vmaAllocator(other.vmaAllocator), _buffer(other._buffer), vmaAllocation(other.vmaAllocation), mapped(other.mapped) {
	other._buffer = VK_NULL_HANDLE;
	other.vmaAllocation = VK_NULL_HANDLE;
	other.mapped = nullptr;
}
VmaBuffer& VmaBuffer::operator=(VmaBuffer&& other) noexcept {
	if (this != &other) {
		if (_buffer && vmaAllocator) {
			vmaDestroyBuffer(vmaAllocator, _buffer, vmaAllocation);
		}
		_buffer = other._buffer;
		mapped = other.mapped;
		vmaAllocator = other.vmaAllocator;
		vmaAllocation = other.vmaAllocation;

		other._buffer = VK_NULL_HANDLE;
		other.vmaAllocation = VK_NULL_HANDLE;
		other.mapped = nullptr;
	}
	return *this;
}

VkBuffer VmaBuffer::get() const {
	return _buffer;
}

vk::Buffer VmaBuffer::buffer() const {
	return {_buffer};
}

VmaBuffer::operator VkBuffer() const {
	return _buffer;
}

VmaAllocation VmaBuffer::allocation() const {
	return vmaAllocation;
}

void* VmaBuffer::mapped_data() const {
	return mapped;
}
