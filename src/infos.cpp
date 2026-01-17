module velo;
import std;
bool VeloContext::is_info_gathered() {
	const std::filesystem::path infoDir = "infos/";
	if (std::filesystem::exists(infoDir)) {
		std::println("Found info directory, delete it to gather infos again");
		fetch_infos = false;
		return true;
	}
	std::filesystem::create_directory("infos");
	fetch_infos = true;
	return false;
}

// one day we'll have reflection
void VeloContext::gather_features_info() {
	std::println("gathering info");
	std::array<const char*, 55> allNames = {
		"robustBufferAccess", "fullDrawIndexUint32", "imageCubeArray",
		"independentBlend", "geometryShader", "tessellationShader",
		"sampleRateShading", "dualSrcBlend", "logicOp",
		"multiDrawIndirect", "drawIndirectFirstInstance", "depthClamp",
		"depthBiasClamp", "fillModeNonSolid", "depthBounds",
		"wideLines", "largePoints", "alphaToOne",
		"multiViewport", "samplerAnisotropy", "textureCompressionETC2",
		"textureCompressionASTC_LDR", "textureCompressionBC", "occlusionQueryPrecise",
		"pipelineStatisticsQuery", "vertexPipelineStoresAndAtomics", "fragmentStoresAndAtomics",
		"shaderTessellationAndGeometryPointSize", "shaderImageGatherExtended", "shaderStorageImageExtendedFormats",
		"shaderStorageImageMultisample", "shaderStorageImageReadWithoutFormat", "shaderStorageImageWriteWithoutFormat",
		"shaderUniformBufferArrayDynamicIndexing", "shaderSampledImageArrayDynamicIndexing", "shaderStorageBufferArrayDynamicIndexing",
		"shaderStorageImageArrayDynamicIndexing", "shaderClipDistance", "shaderCullDistance",
		"shaderFloat64", "shaderInt64", "shaderInt16",
		"shaderResourceResidency", "shaderResourceMinLod", "sparseBinding",
		"sparseResidencyBuffer", "sparseResidencyImage2D", "sparseResidencyImage3D",
		"sparseResidency2Samples", "sparseResidency4Samples", "sparseResidency8Samples",
		"sparseResidency16Samples", "sparseResidencyAliased", "variableMultisampleRate",
		"inheritedQueries"
	};

	std::ofstream featureFile("infos/available_device_features.txt");
	if (featureFile.is_open()) {
		// we are treating vk::PhysicalDeviceFatures as an array of vk::Bool32
		// the struct is guaranteed to be a contiguous sequence of Bool32s with no padding
		vk::Bool32* pFeatures = reinterpret_cast<vk::Bool32*>(&deviceFeatures);
		for (std::size_t i = 0; i < 55; i++) {
		    featureFile << allNames.at(i) << ": " << (pFeatures[i] ? "YES" : "NO") << "\n";
		}
		featureFile.close();
	}
}

void VeloContext::gather_extensions_info() {
	std::ofstream vkExtensionFile("infos/available_vk_extensions.txt");
	if (vkExtensionFile.is_open()) {
		vkExtensionFile << "Available VK Exts:\n";
		for (const auto& property: extensionProperties) {
			vkExtensionFile << property.extensionName << "\n";
		}
		vkExtensionFile.close();
	}

	std::ofstream glfwExtsfile("infos/required_glfw_extensions.txt");
	if (glfwExtsfile.is_open()) {
		std::span glfwSpan(requiredGlfwExtensions, glfwCount);
		glfwExtsfile << "Required GLFW extensions:\n";
		for (const auto& line: glfwSpan) {
			glfwExtsfile << line << "\n";
		}
		glfwExtsfile.close();
	}
}

void VeloContext::gather_layers_info() {
	std::ofstream layersFile("infos/available_layers.txt");
	if (layersFile.is_open()) {
		layersFile << "Available layers:\n";
		for (const auto& property: layerProperties) {
			layersFile << property.layerName << ":\n\t" << property.description << ". impl ver=" << property.implementationVersion << '\n';
		}
		layersFile.close();
	}
}

void VeloContext::enable_x11() {
	enabled_x11 = true;
}
void VeloContext::enable_codam() {
	enabled_codam = true;
}
