module;
#include <glm/glm.hpp>
#include <vector>
#include <vk_mem_alloc.h>
#include <cstdint>

export module vorn:types;
// import std;
import vulkan_hpp;

struct Vertex {
	glm::vec2 pos;
	glm::vec3 color;

	static vk::VertexInputBindingDescription get_bindings_description() {
		return {0, sizeof(Vertex), vk::VertexInputRate::eVertex};
	}
	static std::array<vk::VertexInputAttributeDescription, 2> get_attribute_description() {
		return {
			vk::VertexInputAttributeDescription(0, 0, vk::Format::eR32G32Sfloat, offsetof(Vertex, pos)),
			vk::VertexInputAttributeDescription(1, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, pos))
		};
	}
};

const std::vector<Vertex> vertices = {
    {{-0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}},
    {{0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}},
    {{0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}},
    {{-0.5f, 0.5f}, {1.0f, 1.0f, 1.0f}}
};

const std::vector<uint16_t> indices = {
	0, 1, 2, 2, 3, 0
};

// replaces vk::raii::Buffer
class VmaBuffer {
public:
	VmaBuffer() = default;
	VmaBuffer(VmaAllocator allocator, vk::DeviceSize size, vk::BufferUsageFlags usage, VmaMemoryUsage vmaMemUsage = VMA_MEMORY_USAGE_AUTO, VmaAllocationCreateFlags flags = 0);
	~VmaBuffer();
	VmaBuffer(VmaBuffer&& other) noexcept;
	VmaBuffer& operator=(VmaBuffer&& other) noexcept;
	VmaBuffer(const VmaBuffer&) = delete;
	VmaBuffer& operator=(const VmaBuffer&) = delete;
	explicit operator bool() const { return _buffer != VK_NULL_HANDLE; }

	VkBuffer get() const;
	vk::Buffer buffer() const;
	operator VkBuffer() const;
	VmaAllocation allocation() const;
	void* mapped_data() const;

private:
	VmaAllocator vmaAllocator = VK_NULL_HANDLE;
	VkBuffer _buffer = VK_NULL_HANDLE;
	VmaAllocation vmaAllocation = VK_NULL_HANDLE;
	void* mapped = nullptr;
};

struct UniformBufferObject {
	glm::mat4 model;
	glm::mat4 view;
	glm::mat4 proj;
};

struct PushConstants {
	uint32_t objIdx;
	uint32_t textureidx;
};
