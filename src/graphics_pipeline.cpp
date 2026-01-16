module velo;
import std;
import vulkan_hpp;

void Velo::create_graphics_pipeline() {
	auto shaderCode = read_file(SHADER_PATH);
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
	std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages = {vertShaderInfo, fragShaderInfo};
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
		.dynamicStateCount = static_cast<std::uint32_t>(dynStates.size()),
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

	vk::PipelineDepthStencilStateCreateInfo depthStencil {
		.depthTestEnable = vk::True,
		.depthWriteEnable = vk::True,
		.depthCompareOp = vk::CompareOp::eLess,
		.depthBoundsTestEnable = vk::False,
		.stencilTestEnable = vk::False
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
		.pSetLayouts = &*descriptors.layout,
		.pushConstantRangeCount = 1,
		.pPushConstantRanges = &pcRange
	};
	auto layoutExpected = gpu.device.createPipelineLayout(layoutInfo);
	if (!layoutExpected.has_value()) {
		handle_error("Failed to create pipeline layout", layoutExpected.result);
	}
	pipelineLayout = std::move(*layoutExpected);

	auto depthFmt = SwapchainContext::find_depth_format(gpu.physicalDevice);
	vk::PipelineRenderingCreateInfo renderingInfo {
		.colorAttachmentCount = 1,
		.pColorAttachmentFormats = &swapchain.format,
		.depthAttachmentFormat = depthFmt
	};

	vk::GraphicsPipelineCreateInfo pipelineInfo {
		.pNext = &renderingInfo,
		.stageCount = 2,
		.pStages = shaderStages.data(),
		.pVertexInputState = &vertexInputInfo,
		.pInputAssemblyState = &inputAsm,
		.pViewportState = &viewportState,
		.pRasterizationState = &rasterizer,
		.pMultisampleState = &multisampling,
		.pDepthStencilState = &depthStencil,
		.pColorBlendState = &colorBlending,
		.pDynamicState = &dynStateInfo,
		.layout = *pipelineLayout,
		.renderPass = nullptr,
	};

	auto pipelineExpected = gpu.device.createGraphicsPipeline(nullptr, pipelineInfo);
	if (!pipelineExpected.has_value()) {
		handle_error("Failed to create graphics pipeline", pipelineExpected.result);
	}
	graphicsPipeline = std::move(*pipelineExpected);
	std::cout << "Successfully created graphics pipeline\n";
}

vk::raii::ShaderModule Velo::create_shader_module(const std::vector<char>& code) const {
	vk::ShaderModuleCreateInfo shaderInfo {
		.codeSize = code.size() * sizeof(char),
		.pCode = reinterpret_cast<const std::uint32_t*>(code.data()),
	};
	auto moduleExpected = gpu.device.createShaderModule(shaderInfo);
	if (!moduleExpected.has_value()) {
		handle_error("Failed to create shader module", moduleExpected.result);
	}
	return std::move(*moduleExpected);
}

void DescriptorContext::create_layout(vk::raii::Device& device) {
	vk::DescriptorSetLayoutBinding uniformBinding {
		.binding = 0,
		.descriptorType = vk::DescriptorType::eUniformBuffer,
		.descriptorCount = MAX_OBJECTS,
		.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment
	};
	vk::DescriptorSetLayoutBinding textureBinding {
		.binding = 1,
		.descriptorType = vk::DescriptorType::eCombinedImageSampler,
		.descriptorCount = MAX_OBJECTS,
		.stageFlags = vk::ShaderStageFlagBits::eFragment
	};
	vk::DescriptorSetLayoutBinding materialIdxBinding {
		.binding = 2,
		.descriptorType = vk::DescriptorType::eStorageBuffer,
		.descriptorCount = 1,
		.stageFlags = vk::ShaderStageFlagBits::eFragment
	};
	std::array<vk::DescriptorSetLayoutBinding, 3> bindings = {uniformBinding, textureBinding, materialIdxBinding};
	std::array<vk::DescriptorBindingFlags, 3> bindingsFlags
	{
		vk::DescriptorBindingFlagBits::ePartiallyBound | vk::DescriptorBindingFlagBits::eUpdateAfterBind,
		vk::DescriptorBindingFlagBits::ePartiallyBound | vk::DescriptorBindingFlagBits::eUpdateAfterBind,
		vk::DescriptorBindingFlagBits::ePartiallyBound | vk::DescriptorBindingFlagBits::eUpdateAfterBind
	};
	vk::DescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo {
		.bindingCount = static_cast<std::uint32_t>(bindingsFlags.size()),
		.pBindingFlags = bindingsFlags.data()
	};
	vk::DescriptorSetLayoutCreateInfo layoutInfo {
		.pNext = bindingFlagsInfo,
		.flags = vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool,
		.bindingCount = static_cast<std::uint32_t>(bindings.size()),
		.pBindings = bindings.data()
	};

	auto layoutExpected = device.createDescriptorSetLayout(layoutInfo);
	if (!layoutExpected.has_value()) {
		handle_error("Failed to create descriptor set layout", layoutExpected.result);
	}
	layout = std::move(*layoutExpected);
}

void DescriptorContext::create_pool(vk::raii::Device& device) {
	std::array<vk::DescriptorPoolSize, 3> poolSizes = {{
		{.type = vk::DescriptorType::eUniformBuffer, .descriptorCount = MAX_OBJECTS},
		{.type = vk::DescriptorType::eCombinedImageSampler, .descriptorCount = MAX_TEXTURES},
		{.type = vk::DescriptorType::eStorageBuffer, .descriptorCount = 1}
	}};
	vk::DescriptorPoolCreateInfo poolInfo {
		.flags = vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind | vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
		.maxSets = 1,
		.poolSizeCount = static_cast<std::uint32_t>(poolSizes.size()),
		.pPoolSizes = poolSizes.data()
	};
	auto poolExpected = device.createDescriptorPool(poolInfo);
	if (!poolExpected.has_value()) {
		handle_error("Failed to create descriptor pool", poolExpected.result);
	}
	pool = std::move(*poolExpected);
}

void DescriptorContext::create_set(vk::raii::Device& device) {
	set.clear();
	vk::DescriptorSetAllocateInfo allocInfo {
		.descriptorPool = pool,
		.descriptorSetCount = 1,
		.pSetLayouts = &*layout
	};
	auto setExpected = device.allocateDescriptorSets(allocInfo);
	if (!setExpected.has_value()) {
		handle_error("Failed to allocate descriptor set", setExpected.result);
	}
	set = std::move(setExpected->front());
}

void handle_error(const char* msg, vk::Result err) {
	throw std::runtime_error(std::format("{}: {}", msg, to_string(err)));
}
