module;
#include <cstdint>
//
#include <stdexcept>
#include <iostream>
#include <algorithm>
#include <cstring>
#include <map>

module velo;
// import std;
import vulkan_hpp;

void Velo::pick_physical_device() {
	auto devicesExpected = instance.enumeratePhysicalDevices();
	if (!devicesExpected.has_value()) {
		handle_error("Failed to request available physical devices", devicesExpected.result);
	}
	auto devices = *devicesExpected;

	if (devices.empty()) {
		throw std::runtime_error("Failed to find GPU with Vulkan Support");
	}

	std::multimap<int, vk::raii::PhysicalDevice> candidates{};
	for (const auto& device : devices) {
		auto deviceProperties = device.getProperties();
		auto deviceFeatures = device.getFeatures();
		auto queueFamilies = device.getQueueFamilyProperties();
		auto extsExpected = device.enumerateDeviceExtensionProperties();
		if (!extsExpected.has_value()) {
			handle_error("Failed to query device for extensions", extsExpected.result);
		}
		auto deviceExts = *extsExpected;

		// check suitability
		bool recentEnough = deviceProperties.apiVersion >= vk::ApiVersion13;
		const auto qfpIter = std::ranges::find_if(queueFamilies, [](vk::QueueFamilyProperties const& qfp) {
									return (qfp.queueFlags & vk::QueueFlagBits::eGraphics) != static_cast<vk::QueueFlags>(0);
								});
		bool haveExtensions = std::ranges::all_of(requiredDeviceExtensions, [deviceExts](auto const& deviceExtension) {
								return std::ranges::any_of(deviceExts, [deviceExtension](vk::ExtensionProperties const& extProperty) {
										return std::strcmp(extProperty.extensionName, deviceExtension) == 0;
									});
								});
		if (!deviceFeatures.geometryShader || !deviceFeatures.samplerAnisotropy) {
			continue;
		}
		if (!recentEnough || !haveExtensions || qfpIter == queueFamilies.end()) {
			continue;
		}

		// begin scoring
		uint32_t score = 0;
		if (deviceProperties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu) {
			score += 1000;
		}
		score += deviceProperties.limits.maxImageDimension2D;
		candidates.insert(std::make_pair(score, std::move(const_cast<vk::raii::PhysicalDevice&>(device))));
	}

	if (candidates.rbegin()->first > 0) {
		physicalDevice = std::move(candidates.rbegin()->second);
		vcontext.deviceProperties = physicalDevice.getProperties();
		vcontext.deviceFeatures = physicalDevice.getFeatures();
		// vcontext.is_info_gathered();
		if (vcontext.fetch_infos) {
			vcontext.gather_features_info();
		}
		std::cout << "Picked physical device " << vcontext.deviceProperties.deviceName << std::endl;
	} else {
		throw std::runtime_error("Failed to find suitable GPU");
	}
}

std::tuple<uint32_t, uint32_t> Velo::find_queue_families(const std::vector<vk::QueueFamilyProperties>& qfps) {
	// graphics queue
	auto graphicsQueueFamily = std::ranges::find_if(qfps, [](vk::QueueFamilyProperties const& qfp) {
									return static_cast<bool>(qfp.queueFlags & vk::QueueFlagBits::eGraphics);
								});
	if (graphicsQueueFamily == qfps.end()) {
		throw std::runtime_error("No graphics queue family found");
	}
	uint32_t graphicsIdx = static_cast<uint32_t>(std::distance(qfps.begin(), graphicsQueueFamily));

	// present queue
	auto graphicsSupportPresExpected = physicalDevice.getSurfaceSupportKHR(graphicsIdx, surface);
	if (!graphicsSupportPresExpected.has_value()) {
		handle_error("Failed to querty for surface support", graphicsSupportPresExpected.result);
	}
	vk::Bool32 supportsPresent = *graphicsSupportPresExpected;
	uint32_t presentIdx = UINT32_MAX;
	if (supportsPresent) {
		// graphics queue family also supports present
		presentIdx = graphicsIdx;
	} else {
		for (uint32_t i = 0; i < qfps.size(); i++) {
			if (i == graphicsIdx) continue;
			auto presSupportExpected = physicalDevice.getSurfaceSupportKHR(i, surface);
			if (!presSupportExpected.has_value()) {
				handle_error("Failed to query for present support", presSupportExpected.result);
			}
			vk::Bool32 presentSupport = *presSupportExpected;
			if (presentSupport) {
				presentIdx = i;
				break;
			}
		}
	}
	if (presentIdx == UINT32_MAX) {
		throw std::runtime_error("Failed to find present queue family");
	}

	std::cout << "Found Graphics and Present queue families, respectively: [" << graphicsIdx << "], [" << presentIdx << "]\n";

	return {graphicsIdx, presentIdx};
}
