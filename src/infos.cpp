module;
#include <print>
#include <fstream>
#include <filesystem>

module velo;
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
void VeloContext::gather_info() {
	std::println("gathering info");
	const char* allNames[] = {
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
		vk::Bool32* pFeatures = reinterpret_cast<vk::Bool32*>(&deviceFeatures);
		for (int i = 0; i < 55; i++) {
		    featureFile << allNames[i] << ": " << (pFeatures[i] ? "YES" : "NO") << "\n";
		}
		featureFile.close();
	}
}
