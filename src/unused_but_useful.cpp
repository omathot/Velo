module velo;
import std;

std::uint32_t Velo::find_memory_type(std::uint32_t typeFilter, vk::MemoryPropertyFlags properties) const {
	vk::PhysicalDeviceMemoryProperties memProperties = gpu.physicalDevice.getMemoryProperties();
	for (std::uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
		if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
			return i;
		}
	}
	throw std::runtime_error("Failed to find a suitable memory type");
	std::unreachable();
}
