module;
#include <GLFW/glfw3.h>
//
#include <stdexcept>
#include <iostream>

module velo;
// import std;
import vulkan_hpp;

void Velo::create_swapchain() {
	auto capabilitiesExpected = physicalDevice.getSurfaceCapabilitiesKHR(surface);
	if (!capabilitiesExpected.has_value()) {
		handle_error("Failed to query for surface capabilities", capabilitiesExpected.result);
	}
	auto fmtsExpected = physicalDevice.getSurfaceFormatsKHR(surface);
	if (!fmtsExpected.has_value()) {
		handle_error("Failed to query for available surface formats", fmtsExpected.result);
	}
	auto presentExpected = physicalDevice.getSurfacePresentModesKHR(surface);
	if (!presentExpected.has_value()) {
		handle_error("Failed to query for available surface present modes", presentExpected.result);
	}
	auto surfaceCapabilities = *capabilitiesExpected;

	auto fmt = choose_swap_surface_format(*fmtsExpected);
	auto mode = choose_swap_present_mode(*presentExpected);
	auto extent = choose_swap_extent(surfaceCapabilities);
	// target triple buffering for mailbox mode (instead of minImageCount + 1)
	auto minImgCount = std::max(3u, surfaceCapabilities.minImageCount);
	minImgCount = (surfaceCapabilities.maxImageCount > 0 && minImgCount > surfaceCapabilities.maxImageCount) ? surfaceCapabilities.maxImageCount : minImgCount;

	vk::SwapchainCreateInfoKHR swapInfo {
		.flags = vk::SwapchainCreateFlagsKHR(),
		.surface = surface,
		.minImageCount = minImgCount,
		.imageFormat = fmt.format,
		.imageColorSpace = fmt.colorSpace,
		.imageExtent = extent,
		.imageArrayLayers = 1,
		.imageUsage = vk::ImageUsageFlagBits::eColorAttachment,
		.imageSharingMode = vk::SharingMode::eExclusive,
		.preTransform = surfaceCapabilities.currentTransform,
		.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque,
		.presentMode = mode,
		.clipped = true,
	};
	uint32_t indices[] = {graphicsIdx, presentIdx};
	if (graphicsIdx != presentIdx) {
		swapInfo.imageSharingMode = vk::SharingMode::eConcurrent;
		swapInfo.queueFamilyIndexCount = 2;
		swapInfo.pQueueFamilyIndices = indices;
	} else {
		swapInfo.imageSharingMode = vk::SharingMode::eExclusive;
	}

	auto swapchainExpected = device.createSwapchainKHR(swapInfo);
	if (!swapchainExpected.has_value()) {
		handle_error("Failed to create swapchain", swapchainExpected.result);
	}
	swapchain = std::move(*swapchainExpected);
	auto imgsExpected = swapchain.getImages();
	if (!imgsExpected.has_value()) {
		handle_error("Failed to get swapchain images", imgsExpected.result);
	}
	swapchainImgs = *imgsExpected;
	swapchainExtent = extent;
	swapchainImgFmt = fmt.format;

	std::cout << "Successfully created swapchain and acquired swapchain images\n";
}

vk::SurfaceFormatKHR Velo::choose_swap_surface_format(const std::vector<vk::SurfaceFormatKHR>& availableFormats) {
	// srgb cause most common and best
	for (const auto& format: availableFormats) {
		if (format.format == vk::Format::eB8G8R8A8Srgb && format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
			return format;
		}
	}
	// no further rank on how "good" they are for now, if not ideal return first index
	return availableFormats[0];
}
vk::PresentModeKHR Velo::choose_swap_present_mode(const std::vector<vk::PresentModeKHR> availableModes) {
	// mailbox if available
	for (const auto& mode: availableModes) {
		if (mode == vk::PresentModeKHR::eMailbox) {
			return mode;
		}
	}
	// only one guaranteed to exist, also best for mobile devices where energy usage is relevant
	return vk::PresentModeKHR::eFifo;
}
vk::Extent2D Velo::choose_swap_extent(const vk::SurfaceCapabilitiesKHR& capabilities) {
	// check for magic number
	if (capabilities.currentExtent.width != UINT32_MAX) {
		return capabilities.currentExtent;
	}
	int width, height;
	glfwGetFramebufferSize(window, &width, &height);
	return {
		std::clamp<uint32_t>(width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
		std::clamp<uint32_t>(height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height)
	};
}

void Velo::create_image_views() {
	swapchainImgViews.clear();
	for (uint32_t i = 0; i < swapchainImgs.size(); i++) {
		auto imgView = create_image_view(swapchainImgs[i], swapchainImgFmt, vk::ImageAspectFlagBits::eColor);
		swapchainImgViews.emplace_back(std::move(imgView));
	}
	std::cout << "Successfully created image views, imgViews vec size: " << swapchainImgViews.size() << std::endl;;
}

