module;
#include <cstdint>
//
#include <vector>
#include <stdexcept>
#include <iostream>

module velo;
// import std;
import vulkan_hpp;

void Velo::create_graphics_pipeline() {
	auto shaderCode = read_file("shaders/shader.spv");
	vk::raii::ShaderModule shaderModule = create_shader_module(shaderCode);

	vk::PipelineShaderStageCreateInfo vertShaderInfo{
		.stage = vk::ShaderStageFlagBits::eVertex,
		.module = *shaderModule,
		.pName = "vertMain"
	};
	vk::PipelineShaderStageCreateInfo fragShaderInfo {
		.stage = vk::ShaderStageFlagBits::eFragment,
		.module = *shaderModule,
		.pName = "fragMain"
	};
	vk::PipelineShaderStageCreateInfo shaderStages[] = {vertShaderInfo, fragShaderInfo};
	auto bindingDescription = Vertex::get_bindings_description();
	auto attributeDescription = Vertex::get_attribute_description();
	vk::PipelineVertexInputStateCreateInfo vertexInputInfo = {
		.vertexBindingDescriptionCount = 1,
		.pVertexBindingDescriptions = &bindingDescription,
		.vertexAttributeDescriptionCount = attributeDescription.size(),
		.pVertexAttributeDescriptions = attributeDescription.data()
	};

	std::vector<vk::DynamicState> dynStates = {
		vk::DynamicState::eViewport,
		vk::DynamicState::eScissor
	};
	vk::PipelineDynamicStateCreateInfo dynStateInfo {
		.dynamicStateCount = static_cast<uint32_t>(dynStates.size()),
		.pDynamicStates = dynStates.data(),
	};
	vk::PipelineViewportStateCreateInfo viewportState {
		.viewportCount = 1,
		.scissorCount = 1
	};
	vk::PipelineInputAssemblyStateCreateInfo inputAsm {
		.topology = vk::PrimitiveTopology::eTriangleList,
	};

	vk::PipelineRasterizationStateCreateInfo rasterizer {
		.depthClampEnable = vk::False,
		.rasterizerDiscardEnable = vk::False,
		.polygonMode = vk::PolygonMode::eFill,
		.cullMode = vk::CullModeFlagBits::eBack,
		.frontFace = vk::FrontFace::eCounterClockwise,
		.depthBiasEnable = vk::False,
		.depthBiasSlopeFactor = 1.0f,
		.lineWidth = 1.0f
	};

	vk::PipelineMultisampleStateCreateInfo multisampling {
		.rasterizationSamples = vk::SampleCountFlagBits::e1,
		.sampleShadingEnable = vk::False
	};

	vk::PipelineColorBlendAttachmentState colorBlendAttachment {
		.blendEnable = vk::False,
		.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA
	};
	vk::PipelineColorBlendStateCreateInfo colorBlending {
		.logicOpEnable = vk::False,
		.logicOp = vk::LogicOp::eCopy,
		.attachmentCount = 1,
		.pAttachments = &colorBlendAttachment
	};

	vk::PushConstantRange pcRange {
		.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
		.offset = 0,
		.size = sizeof(PushConstants)
	};
	vk::PipelineLayoutCreateInfo layoutInfo{
		.setLayoutCount = 1,
		.pSetLayouts = &*descriptorSetLayout,
		.pushConstantRangeCount = 1,
		.pPushConstantRanges = &pcRange
	};
	auto layoutExpected = device.createPipelineLayout(layoutInfo);
	if (!layoutExpected.has_value()) {
		handle_error("Failed to create pipeline layout", layoutExpected.result);
	}
	pipelineLayout = std::move(*layoutExpected);

	vk::PipelineRenderingCreateInfo renderingInfo {
		.colorAttachmentCount = 1,
		.pColorAttachmentFormats = &swapchainImgFmt
	};

	vk::GraphicsPipelineCreateInfo pipelineInfo {
		.pNext = &renderingInfo,
		.stageCount = 2,
		.pStages = shaderStages,
		.pVertexInputState = &vertexInputInfo,
		.pInputAssemblyState = &inputAsm,
		.pViewportState = &viewportState,
		.pRasterizationState = &rasterizer,
		.pMultisampleState = &multisampling,
		.pColorBlendState = &colorBlending,
		.pDynamicState = &dynStateInfo,
		.layout = *pipelineLayout,
		.renderPass = nullptr,
	};

	auto pipelineExpected = device.createGraphicsPipeline(nullptr, pipelineInfo);
	if (!pipelineExpected.has_value()) {
		handle_error("Failed to create graphics pipeline", pipelineExpected.result);
	}
	graphicsPipeline = std::move(*pipelineExpected);
	std::cout << "Successfully created graphics pipeline\n";
}

vk::raii::ShaderModule Velo::create_shader_module(const std::vector<char>& code) const {
	vk::ShaderModuleCreateInfo shaderInfo {
		.codeSize = code.size() * sizeof(char),
		.pCode = reinterpret_cast<const uint32_t*>(code.data()),
	};
	auto moduleExpected = device.createShaderModule(shaderInfo);
	if (!moduleExpected.has_value()) {
		handle_error("Failed to create shader module", moduleExpected.result);
	}
	return std::move(*moduleExpected);
}
