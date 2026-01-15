module;
#include <cstdint>
#include <GLFW/glfw3.h>
#define TINYOBJLOADER_IMPLEMENTATION
#include <glm/glm.hpp>
#include <tiny_obj_loader.h>
#include <vk_mem_alloc.h>
//
#include <string>
#include <vector>
#include <iostream>
#include <fstream>
#include <stdexcept>

export module velo;
import :types;
// import std;
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

constexpr uint32_t WIDTH = 800;
constexpr uint32_t HEIGHT = 600;
#ifdef CODAM
	const std::string MODEL_PATH = "/home/omathot/dev/cpp/velo/models/teapot2.obj";
	const std::string TEXTURE_PATH = "/home/omathot/dev/cpp/velo/textures/teapot2.mtl";
#else
	const std::string MODEL_PATH = "/home/omathot/dev/cpp/velo/models/viking_room.obj";
	const std::string TEXTURE_PATH = "/home/omathot/dev/cpp/velo/textures/viking_room.png";
#endif
const std::string SHADER_PATH = "/home/omathot/dev/cpp/velo/shaders/shader.spv";

export class Velo {
public:
	Velo();
	void run();

private:
	VeloContext vcontext;
	GLFWwindow* window{};
	vk::raii::Context context;
	vk::raii::Instance instance{nullptr};
	vk::raii::DebugUtilsMessengerEXT debugMessenger{nullptr};
	vk::raii::SurfaceKHR surface{nullptr};

	vk::raii::PhysicalDevice physicalDevice{nullptr};
	vk::raii::Device device{nullptr};
	vk::raii::Queue graphicsQueue{nullptr};
	vk::raii::Queue presentQueue{nullptr};
	uint32_t graphicsIdx{};
	uint32_t presentIdx{};

	VmaAllocator allocator{};

	vk::raii::SwapchainKHR swapchain{nullptr};
	std::vector<vk::Image> swapchainImgs;
	std::vector<vk::raii::ImageView> swapchainImgViews;
	vk::Format swapchainImgFmt = vk::Format::eUndefined;
	vk::Extent2D swapchainExtent{};

	vk::raii::CommandPool cmdPool{nullptr};
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
	std::vector<uint32_t> indices;
	VmaBuffer vertexBuff;
	VmaBuffer indexBuff;
	VmaBuffer materialIdxBuff;

	std::vector<VmaBuffer> uniformBuffs;
	std::vector<void*> uniformBuffsMapped;
	VmaImage textureImage;
	uint32_t mipLvls{};
	vk::raii::ImageView textureImageView{nullptr};
	vk::raii::Sampler textureSampler{nullptr};
	VmaImage depthImage;
	vk::raii::ImageView depthImageView{nullptr};

	std::vector<VmaImage> materialImages;
	std::vector<vk::raii::ImageView> materialImageViews;
	std::vector<uint32_t> materialIndices;


	float totalTime{};
	float dt{};
	/// Total frame count for app lifespan
	uint32_t frameCount{};
	/// Frame Index for VK operations ( % MAX_FRAMES_IN_FLIGHT )
	uint32_t frameIdx{};
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

	void create_instance();
	std::vector<char const*> get_required_extensions();
	std::vector<char const*> get_required_layers();
	void setup_debug_messenger();
	void pick_physical_device();
	std::tuple<uint32_t, uint32_t> find_queue_families(const std::vector<vk::QueueFamilyProperties>& qfps);
	void create_logical_device();
	void create_surface();
	void init_vma();
	void create_swapchain();
	vk::Extent2D choose_swap_extent(const vk::SurfaceCapabilitiesKHR& capabilities);
	void create_image_views();
	void create_graphics_pipeline();
	[[nodiscard]] vk::raii::ShaderModule create_shader_module(const std::vector<char>& code) const;
	void create_command_pool();
	void create_command_buffer();
	void record_command_buffer(uint32_t imgIdx);
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
	void transition_image_texture_layout(VmaImage& img, vk::ImageLayout oldLayout, vk::ImageLayout newLayout, uint32_t mips);
	void create_sync_objects();
	void recreate_swapchain();
	void cleanup_swapchain();
	void create_buffer(vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties, VmaBuffer& buff, vk::raii::DeviceMemory& buffMem);
	void create_vertex_buffer();
	void create_index_buffer();
	uint32_t find_memory_type(uint32_t typeFilter, vk::MemoryPropertyFlags properties);
	void copy_buffer(VmaBuffer& srcBuff, VmaBuffer& dstBuff, vk::DeviceSize size);
	void create_descriptor_set_layout();
	void create_uniform_buffers();
	void update_uniform_buffers(uint32_t currImg);
	void create_descriptor_pools();
	void create_descriptor_sets();
	void create_texture_image();
	vk::raii::CommandBuffer begin_single_time_commands();
	void end_single_time_commands(vk::raii::CommandBuffer& cmdBuff);
	void copy_buffer_to_image(const VmaBuffer& buff, VmaImage& img, uint32_t width, uint32_t height);
	void create_texture_image_view();
	vk::raii::ImageView create_image_view(const vk::Image& img, vk::Format fmt, vk::ImageAspectFlags aspectFlags, uint32_t mips);
	void create_texture_sampler();
	void create_depth_resources();
	/// vector must be ordered from most desirable to least desirable
	vk::Format find_supported_format(const std::vector<vk::Format>& candidates, vk::ImageTiling tiling, vk::FormatFeatureFlags features);
	vk::Format find_depth_format();
	void load_model();
	void load_material_textures(const std::vector<tinyobj::material_t>& materials);
	void process_input();
	void init_env();
	void init_swapchain();
	void init_commands();
	void init_descriptors();
	void init_default_data();
	void create_material_images();
	void create_texture_material_views();
	void load_model_per_face_material();
	void create_material_index_buffer();

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
bool has_stencil_component(vk::Format fmt);
void handle_error(const char* msg, vk::Result error);
vk::SurfaceFormatKHR choose_swap_surface_format(const std::vector<vk::SurfaceFormatKHR>& availableFormats);
vk::PresentModeKHR choose_swap_present_mode(const std::vector<vk::PresentModeKHR>& availableModes);
