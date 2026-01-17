module;
#include <GLFW/glfw3.h>
#include <vk_mem_alloc.h>

module velo;
import std;
import vulkan_hpp;

static vk::SurfaceFormatKHR choose_swap_surface_format(const std::vector<vk::SurfaceFormatKHR>& availableFormats);
static vk::PresentModeKHR choose_swap_present_mode(const std::vector<vk::PresentModeKHR>& availableModes);
static vk::Extent2D choose_swap_extent(GLFWwindow* window, const vk::SurfaceCapabilitiesKHR& capabilities);

void SwapchainContext::create(GLFWwindow* window, GpuContext& gpu) {
	auto capabilitiesExpected = gpu.physicalDevice.getSurfaceCapabilitiesKHR(gpu.surface);
	if (!capabilitiesExpected.has_value()) {
		handle_error("Failed to query for surface capabilities", capabilitiesExpected.result);
	}
	auto fmtsExpected = gpu.physicalDevice.getSurfaceFormatsKHR(gpu.surface);
	if (!fmtsExpected.has_value()) {
		handle_error("Failed to query for available surface formats", fmtsExpected.result);
	}
	auto presentExpected = gpu.physicalDevice.getSurfacePresentModesKHR(gpu.surface);
	if (!presentExpected.has_value()) {
		handle_error("Failed to query for available surface present modes", presentExpected.result);
	}
	auto surfaceCapabilities = *capabilitiesExpected;

	auto fmt = choose_swap_surface_format(*fmtsExpected);
	auto mode = choose_swap_present_mode(*presentExpected);
	auto tmpExtent = choose_swap_extent(window, surfaceCapabilities);
	// target triple buffering for mailbox mode (instead of minImageCount + 1)
	auto minImgCount = std::max(3u, surfaceCapabilities.minImageCount);
	minImgCount = (surfaceCapabilities.maxImageCount > 0 && minImgCount > surfaceCapabilities.maxImageCount) ? surfaceCapabilities.maxImageCount : minImgCount;

	vk::SwapchainCreateInfoKHR swapInfo {
		.flags = vk::SwapchainCreateFlagsKHR(),
		.surface = gpu.surface,
		.minImageCount = minImgCount,
		.imageFormat = fmt.format,
		.imageColorSpace = fmt.colorSpace,
		.imageExtent = tmpExtent,
		.imageArrayLayers = 1,
		.imageUsage = vk::ImageUsageFlagBits::eColorAttachment,
		.imageSharingMode = vk::SharingMode::eExclusive,
		.preTransform = surfaceCapabilities.currentTransform,
		.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque,
		.presentMode = mode,
		.clipped = true,
	};
	std::array<std::uint32_t, 2> familyIndices = {gpu.graphicsIdx, gpu.presentIdx};
	if (gpu.graphicsIdx != gpu.presentIdx) {
		swapInfo.imageSharingMode = vk::SharingMode::eConcurrent;
		swapInfo.queueFamilyIndexCount = 2;
		swapInfo.pQueueFamilyIndices = familyIndices.data();
	} else {
		swapInfo.imageSharingMode = vk::SharingMode::eExclusive;
	}

	auto swapchainExpected = gpu.device.createSwapchainKHR(swapInfo);
	if (!swapchainExpected.has_value()) {
		handle_error("Failed to create swapchain", swapchainExpected.result);
	}
	swapchain = std::move(*swapchainExpected);
	auto imgsExpected = swapchain.getImages();
	if (!imgsExpected.has_value()) {
		handle_error("Failed to get swapchain images", imgsExpected.result);
	}
	images = *imgsExpected;
	extent = tmpExtent;
	format = fmt.format;
}

static vk::Extent2D choose_swap_extent(GLFWwindow* window, const vk::SurfaceCapabilitiesKHR& capabilities) {
	// check for magic number
	if (capabilities.currentExtent.width != UINT32_MAX) {
		return capabilities.currentExtent;
	}
	int width = 0, height = 0;
	glfwGetFramebufferSize(window, &width, &height);
	return {
		.width = std::clamp<std::uint32_t>(static_cast<std::uint32_t>(width), capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
		.height = std::clamp<std::uint32_t>(static_cast<std::uint32_t>(height), capabilities.minImageExtent.height, capabilities.maxImageExtent.height)
	};
}

void SwapchainContext::create_image_views(vk::raii::Device& device) {
	imageViews.clear();
	for (auto swapchainImg : images) {
		auto imgView = create_image_view(device, swapchainImg, format, vk::ImageAspectFlagBits::eColor, 1);
		imageViews.emplace_back(std::move(imgView));
	}
}

static vk::SurfaceFormatKHR choose_swap_surface_format(const std::vector<vk::SurfaceFormatKHR>& availableFormats) {
	// srgb cause most common and best
	for (const auto& format: availableFormats) {
		if (format.format == vk::Format::eB8G8R8A8Srgb && format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
			return format;
		}
	}
	// no further rank on how "good" they are for now, if not ideal return first index
	return availableFormats[0];
}

static vk::PresentModeKHR choose_swap_present_mode(const std::vector<vk::PresentModeKHR>& availableModes) {
	// mailbox if available
	for (const auto& mode: availableModes) {
		if (mode == vk::PresentModeKHR::eMailbox) {
			return mode;
		}
	}
	// only one guaranteed to exist, also best for mobile devices where energy usage is relevant
	return vk::PresentModeKHR::eFifo;
}

void SwapchainContext::create_depth_resources(GpuContext& gpu) {
	vk::Format depthFmt = find_depth_format(gpu.physicalDevice);
	depthImage = VmaImage(gpu.allocator, extent.width, extent.height, 1, vk::ImageUsageFlagBits::eDepthStencilAttachment, depthFmt,  VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT, VMA_MEMORY_USAGE_AUTO);
	depthView = create_image_view(gpu.device, depthImage.image(), depthFmt, vk::ImageAspectFlagBits::eDepth, 1);
}

vk::Format SwapchainContext::find_depth_format(vk::raii::PhysicalDevice& physicalDevice) {
	return find_supported_format(
		physicalDevice,
		{vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint, vk::Format::eD24UnormS8Uint},
		vk::ImageTiling::eOptimal,
		vk::FormatFeatureFlagBits::eDepthStencilAttachment
	);
}

void SwapchainContext::recreate(GLFWwindow* window, GpuContext& gpu) {
	int width = 0, height = 0;
	glfwGetFramebufferSize(window, &width, &height);
	while (width == 0 || height == 0) {
		glfwGetFramebufferSize(window, &width, &height);
		glfwWaitEvents();
	}

	gpu.device.waitIdle();
	cleanup();
	create(window, gpu);
	create_image_views(gpu.device);
	create_depth_resources(gpu);
}

void SwapchainContext::cleanup() {
	imageViews.clear();
	swapchain = nullptr;
}
