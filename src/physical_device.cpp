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
	for (const auto& dev : devices) {
		auto deviceProperties = dev.getProperties();
		auto deviceFeatures = dev.getFeatures();
		auto queueFamilies = dev.getQueueFamilyProperties();
		auto extsExpected = dev.enumerateDeviceExtensionProperties();
		if (!extsExpected.has_value()) {
			handle_error("Failed to query device for extensions", extsExpected.result);
		}
		const auto& deviceExts = *extsExpected;

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
		candidates.insert(std::make_pair(score, dev));
	}

	if (candidates.rbegin()->first > 0) {
		physicalDevice = std::move(candidates.rbegin()->second);
		vcontext.deviceProperties = physicalDevice.getProperties();
		vcontext.deviceFeatures = physicalDevice.getFeatures();
		if (vcontext.fetch_infos) {
			vcontext.gather_features_info();
		}
		std::cout << "Picked physical device " << vcontext.deviceProperties.deviceName << '\n';
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
	uint32_t graphicsIndex = static_cast<uint32_t>(std::distance(qfps.begin(), graphicsQueueFamily));

	// present queue
	auto graphicsSupportPresExpected = physicalDevice.getSurfaceSupportKHR(graphicsIndex, surface);
	if (!graphicsSupportPresExpected.has_value()) {
		handle_error("Failed to querty for surface support", graphicsSupportPresExpected.result);
	}
	vk::Bool32 supportsPresent = *graphicsSupportPresExpected;
	uint32_t presentIndex = UINT32_MAX;
	if (supportsPresent) {
		// graphics queue family also supports present
		presentIndex = graphicsIndex;
	} else {
		for (uint32_t i = 0; i < qfps.size(); i++) {
			if (i == graphicsIndex) continue;
			auto presSupportExpected = physicalDevice.getSurfaceSupportKHR(i, surface);
			if (!presSupportExpected.has_value()) {
				handle_error("Failed to query for present support", presSupportExpected.result);
			}
			vk::Bool32 presentSupport = *presSupportExpected;
			if (presentSupport) {
				presentIndex = i;
				break;
			}
		}
	}
	if (presentIndex == UINT32_MAX) {
		throw std::runtime_error("Failed to find present queue family");
	}

	std::cout << "Found Graphics and Present queue families, respectively: [" << graphicsIndex << "], [" << presentIndex << "]\n";

	return {graphicsIndex, presentIndex};
}
