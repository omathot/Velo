module;
#include <GLFW/glfw3.h>
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_DEFAULT_ALIGNED_GENTYPES
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/hash.hpp>
#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>
#include <vk_mem_alloc.h>

export module velo;
import std;
import vulkan_hpp;

#define VULKAN_HPP_DISABLE_IMPLICIT_RESULT_VALUE_CAST

const std::vector<char const*> validationLayers = {
	"VK_LAYER_KHRONOS_validation"
};
#ifdef NDEBUG
	constexpr bool enableValidationLayers = false;
#else
	constexpr bool enableValidationLayers = true;
#endif

const std::vector<const char*> requiredDeviceExtensions = {
    vk::KHRSwapchainExtensionName,
    vk::KHRSpirv14ExtensionName,
    vk::KHRSynchronization2ExtensionName,
    vk::KHRCreateRenderpass2ExtensionName
};

constexpr int MAX_FRAMES_IN_FLIGHT = 2;
// for bindless UBOs
constexpr int MAX_OBJECTS = 100;
constexpr int MAX_TEXTURES = 100;
constexpr int MAX_MATERIALS = 4;

constexpr std::uint32_t WIDTH = 800;
constexpr std::uint32_t HEIGHT = 600;
#if defined(CODAM)
	const std::string MODEL_PATH = "/home/omathot/dev/cpp/velo/models/teapot2.obj";
	const std::string TEXTURE_PATH = "/home/omathot/dev/cpp/velo/textures/teapot2.mtl";
#else
	const std::string MODEL_PATH = "/home/omathot/dev/cpp/velo/models/viking_room.obj";
	const std::string TEXTURE_PATH = "/home/omathot/dev/cpp/velo/textures/viking_room.png";
#endif
const std::string SHADER_PATH = "/home/omathot/dev/cpp/velo/shaders/shader.spv";

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
	std::uint32_t glfwCount{};

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
		std::uint32_t width,
		std::uint32_t height,
		std::uint32_t mipLvls,
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

struct Mesh {
	VmaBuffer vertexBuff;
	VmaBuffer indexBuff;
	std::uint32_t indexCount{};
};

struct Material {
	VmaImage image;
	vk::raii::ImageView view{nullptr};
};

struct PushConstants {
	std::uint32_t objIdx{};
	std::uint32_t textureidx{};
};

struct GpuContext {
	vk::raii::Instance instance{nullptr};
	vk::raii::SurfaceKHR surface{nullptr};
	vk::raii::PhysicalDevice physicalDevice{nullptr};
	vk::raii::Device device{nullptr};
	vk::raii::Queue graphicsQueue{nullptr};
	vk::raii::Queue presentQueue{nullptr};
	std::uint32_t graphicsIdx{};
	std::uint32_t presentIdx{};
	VmaAllocator allocator{};
	vk::raii::CommandPool cmdPool{nullptr};

	// vk::raii::CommandBuffer begin_immediate();
	// void end_immediate(vk::raii::CommandBuffer& cmd);
	vk::raii::CommandBuffer begin_single_time_commands();
	void end_single_time_commands(vk::raii::CommandBuffer& cmdBuff) const;

	void create_instance(vk::raii::Context& context, VeloContext& vcontext);
	void create_surface(GLFWwindow* window);
	void pick_physical_device(VeloContext& vcontext);
	std::tuple<std::uint32_t, std::uint32_t> find_queue_families(const std::vector<vk::QueueFamilyProperties>& qfps, vk::raii::SurfaceKHR& surface) const;
	void create_logical_device(vk::raii::SurfaceKHR& surface);
	void init_vma();
	void create_command_pool();
};

struct SwapchainContext {
	vk::raii::SwapchainKHR swapchain{nullptr};
	std::vector<vk::Image> images;
	std::vector<vk::raii::ImageView> imageViews;
	vk::Format format = vk::Format::eUndefined;
	vk::Extent2D extent{};
	VmaImage depthImage;
	vk::raii::ImageView depthView{nullptr};
	void create(GLFWwindow* window, GpuContext& gpu);
	void recreate(GLFWwindow* window, GpuContext& gpu);
	void cleanup();

	void create_image_views(vk::raii::Device& device);
	void create_depth_resources(GpuContext& gpu);
	static vk::Format find_depth_format(vk::raii::PhysicalDevice& physicalDevice);
};

export class Velo {
public:
	Velo();
	void run();

private:
	VeloContext vcontext;
	GLFWwindow* window{};
	vk::raii::Context context;
	GpuContext gpu;
	SwapchainContext swapchain;
	vk::raii::DebugUtilsMessengerEXT debugMessenger{nullptr};

	std::vector<vk::raii::CommandBuffer> cmdBuffers;

	vk::raii::DescriptorSetLayout descriptorSetLayout{nullptr};
	vk::raii::DescriptorPool descriptorPool{nullptr};
	vk::raii::DescriptorSet descriptorSets{nullptr};
	vk::raii::PipelineLayout pipelineLayout{nullptr};
	vk::raii::Pipeline graphicsPipeline{nullptr};

	vk::raii::Semaphore timelineSem{nullptr};
	std::vector<vk::raii::Semaphore> presentCompleteSems;
	std::vector<vk::raii::Semaphore> renderDoneSems;

	std::vector<Vertex> vertices;
	std::vector<std::uint32_t> indices;
	VmaBuffer vertexBuff;
	VmaBuffer indexBuff;
	VmaBuffer materialIdxBuff;

	std::vector<VmaBuffer> uniformBuffs;
	std::vector<void*> uniformBuffsMapped;
	VmaImage textureImage;
	std::uint32_t mipLvls{};
	vk::raii::ImageView textureImageView{nullptr};
	vk::raii::Sampler textureSampler{nullptr};

	std::vector<VmaImage> materialImages;
	std::vector<vk::raii::ImageView> materialImageViews;
	std::vector<std::uint32_t> materialIndices;

	float totalTime{};
	float dt{};
	/// Total frame count for app lifespan
	std::uint32_t frameCount{};
	/// Frame Index for VK operations ( % MAX_FRAMES_IN_FLIGHT )
	std::uint32_t frameIdx{};
	/// window resized bool
	bool frameBuffResized{};

	glm::vec3 position{{}};
	float speed = 2.0f;
	int rotation = 1;
	float rotationSpeed = 60.0f;
	float currAngle{};

	void init_window();
	void init_vulkan();
	void main_loop();
	void cleanup();

	void init_env();
	void init_swapchain();
	void init_commands();
	void init_descriptors();
	void init_default_data();

	void setup_debug_messenger();
	void create_graphics_pipeline();
	[[nodiscard]] vk::raii::ShaderModule create_shader_module(const std::vector<char>& code) const;
	void create_command_buffer();
	void record_command_buffer(std::uint32_t imgIdx);
	// img transitions
	void transition_image_layout(
		vk::Image img,
		vk::ImageLayout oldLayout,
		vk::ImageLayout newLayout,
		vk::AccessFlags2 srcAccessMask,
		vk::AccessFlags2 dstAccessMask,
		vk::PipelineStageFlags2 srcStageMask,
		vk::PipelineStageFlags2 dstStageMask,
		vk::ImageAspectFlags aspectFlags
	);
	void transition_image_texture_layout(VmaImage& img, vk::ImageLayout oldLayout, vk::ImageLayout newLayout, std::uint32_t mips);
	void create_sync_objects();
	void create_buffer(vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties, VmaBuffer& buff, vk::raii::DeviceMemory& buffMem);
	void create_vertex_buffer();
	void create_index_buffer();
	[[nodiscard]] std::uint32_t find_memory_type(std::uint32_t typeFilter, vk::MemoryPropertyFlags properties) const;
	void copy_buffer(VmaBuffer& srcBuff, VmaBuffer& dstBuff, vk::DeviceSize size);
	void create_descriptor_set_layout();
	void create_uniform_buffers();
	void update_uniform_buffers(std::uint32_t currImg);
	void create_descriptor_pools();
	void create_descriptor_sets();
	void create_texture_image();
	void copy_buffer_to_image(const VmaBuffer& buff, VmaImage& img, std::uint32_t width, std::uint32_t height);
	void create_texture_image_view();
	void create_texture_sampler();
	void load_model();
	void load_material_textures(const std::vector<tinyobj::material_t>& materials);
	void create_material_images();
	void create_texture_material_views();
	void load_model_per_face_material();
	void create_material_index_buffer();
	void create_dummy_material_index_buffer();

	void process_input();
	void draw_frame();


	// debug callback
	static VKAPI_ATTR vk::Bool32 VKAPI_CALL debug_callback(
		vk::DebugUtilsMessageSeverityFlagBitsEXT severity,
		vk::DebugUtilsMessageTypeFlagsEXT type,
		const vk::DebugUtilsMessengerCallbackDataEXT* pCallbackData,
		void* /*data*/
	) {
		if (severity >= vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo) {
			std::cerr << "validation layer: type " << to_string(type) << " msg: " << pCallbackData->pMessage << '\n';
		}
		return vk::False;
	}

	static void frameBufferResizeCb(GLFWwindow* window, int /*width*/, int /*height*/) {
		// yes we can do that, yes it works
		// just looks criminal
		auto* app = reinterpret_cast<Velo*>(glfwGetWindowUserPointer(window));
		app->frameBuffResized = true;
	}
};


// temp utils
std::vector<char> read_file(const std::string& filename) {
	std::ifstream file(filename, std::ios::ate | std::ios::binary);

	if (!file.is_open()) {
		throw std::runtime_error("failed to open file");
	}

	std::vector<char> buffer(static_cast<ulong>(file.tellg()));
	file.seekg(0, std::ios::beg);
	file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
	file.close();
	return buffer;
}
// bool has_stencil_component(vk::Format fmt);
void handle_error(const char* msg, vk::Result error);
/// vector must be ordered from most desirable to least desirable
vk::Format find_supported_format(vk::raii::PhysicalDevice& physicalDevice, const std::vector<vk::Format>& candidates, vk::ImageTiling tiling, vk::FormatFeatureFlags features);
vk::raii::ImageView create_image_view(vk::raii::Device& device, const vk::Image& img, vk::Format fmt, vk::ImageAspectFlags aspectFlags, std::uint32_t mips);
