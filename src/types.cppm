module;
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_DEFAULT_ALIGNED_GENTYPES
#define GLM_ENABLE_EXPERIMENTAL
#include <functional>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/hash.hpp>
#include <vk_mem_alloc.h>
#include <cstdint>

export module velo:types;
// import std;
import vulkan_hpp;

struct VeloContext {
	bool should_quit{};
	bool enabled_codam{};
	bool enabled_x11{};
	bool fetch_infos{};

	vk::PhysicalDeviceFeatures deviceFeatures{};
	vk::PhysicalDeviceProperties deviceProperties{};
	std::vector<vk::LayerProperties> layerProperties;
	std::vector<vk::ExtensionProperties> extensionProperties;
	const char** requiredGlfwExtensions{};
	uint32_t glfwCount{};

	void enable_codam();
	void enable_x11();
	bool is_info_gathered();
	void gather_features_info();
	void gather_extensions_info();
	void gather_layers_info();
};

struct Vertex {
	glm::vec3 pos{};
	glm::vec3 color{};
	glm::vec2 texCoord{};

	static vk::VertexInputBindingDescription get_bindings_description() {
		return {
			.binding = 0,
			.stride = sizeof(Vertex),
			.inputRate = vk::VertexInputRate::eVertex};
		};
	static std::array<vk::VertexInputAttributeDescription, 3> get_attribute_description() {
		return {
			vk::VertexInputAttributeDescription(0, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, pos)),
			vk::VertexInputAttributeDescription(1, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, color)),
	        vk::VertexInputAttributeDescription(2, 0, vk::Format::eR32G32Sfloat, offsetof(Vertex, texCoord)),
		};
	}

	bool operator==(const Vertex &other) const {
		return pos == other.pos &&
				color == other.color &&
				texCoord == other.texCoord;
	}
};
template <>
struct std::hash<Vertex> {
	size_t operator()(Vertex const &vertex) const noexcept {
		return ((hash<glm::vec3>()(vertex.pos) ^
				(hash<glm::vec3>()(vertex.color) << 1)) >> 1) ^
				(hash<glm::vec2>()(vertex.texCoord) << 1);
	}
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

	operator VkBuffer() const;
	[[nodiscard]] VkBuffer get() const;
	[[nodiscard]] vk::Buffer buffer() const;
	[[nodiscard]] VmaAllocation allocation() const;
	[[nodiscard]] void* mapped_data() const;

private:
	VmaAllocator vmaAllocator = VK_NULL_HANDLE;
	VkBuffer _buffer = VK_NULL_HANDLE;
	VmaAllocation vmaAllocation = VK_NULL_HANDLE;
	void* mapped{};
};

struct UniformBufferObject {
	alignas(16) glm::mat4 model;
	alignas(16) glm::mat4 view;
	alignas(16) glm::mat4 proj;
};

class VmaImage {
public:
	VmaImage() = default;
	VmaImage(
		VmaAllocator allocator,
		uint32_t width,
		uint32_t height,
		uint32_t mipLvls,
		vk::ImageUsageFlags usage,
		vk::Format fmt,
		VmaAllocationCreateFlags flags = 0,
		VmaMemoryUsage vmaMemUsage = VMA_MEMORY_USAGE_AUTO
	);
	~VmaImage();
	VmaImage(VmaImage&& other) noexcept;
	VmaImage& operator=(VmaImage&& other) noexcept;
	VmaImage(const VmaImage&) = delete;
	VmaImage& operator=(const VmaImage&) = delete;
	explicit operator bool() const { return _image != VK_NULL_HANDLE; }

	operator VkImage() const;
	[[nodiscard]] VkImage get() const;
	[[nodiscard]] vk::Image image() const;
	[[nodiscard]] VmaAllocation allocation() const;
	[[nodiscard]] void* mapped_data() const;

private:
	VmaAllocator vmaAllocator = VK_NULL_HANDLE;
	VkImage _image = VK_NULL_HANDLE;
	VmaAllocation vmaAllocation = VK_NULL_HANDLE;
	void* mapped{};
};

struct PushConstants {
	uint32_t objIdx{};
	uint32_t textureidx{};
};

