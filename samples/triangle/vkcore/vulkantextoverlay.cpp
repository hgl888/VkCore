#include "vulkantextoverlay.hpp"
#include "array"

VulkanTextOverlay::VulkanTextOverlay(
	VkCoreDevice *vulkanDevice,
	VkQueue queue,
	std::vector<VkFramebuffer> &framebuffers,
	VkFormat colorformat,
	VkFormat depthformat,
	uint32_t *framebufferwidth,
	uint32_t *framebufferheight,
	std::vector<VkPipelineShaderStageCreateInfo> shaderstages)
{
	this->mVulkanDevice = vulkanDevice;
	this->mQueue = queue;
	this->mColorFormat = colorformat;
	this->mDepthFormat = depthformat;

	this->mFrameBuffers.resize(framebuffers.size());
	for (uint32_t i = 0; i < framebuffers.size(); i++)
	{
		this->mFrameBuffers[i] = &framebuffers[i];
	}

	this->mShaderStages = shaderstages;

	this->mFrameBufferWidth = framebufferwidth;
	this->mFrameBufferHeight = framebufferheight;

	mCmdBuffers.resize(framebuffers.size());
	prepareResources();
	prepareRenderPass();
	preparePipeline();
}


VulkanTextOverlay::~VulkanTextOverlay()
{
	// Free up all Vulkan resources requested by the text overlay
	mVertexBuffer.destroy();
	vkDestroySampler(mVulkanDevice->mLogicalDevice, mSampler, nullptr);
	vkDestroyImage(mVulkanDevice->mLogicalDevice, mImage, nullptr);
	vkDestroyImageView(mVulkanDevice->mLogicalDevice, mImageView, nullptr);
	vkFreeMemory(mVulkanDevice->mLogicalDevice, mImageMemory, nullptr);
	vkDestroyDescriptorSetLayout(mVulkanDevice->mLogicalDevice, mDescriptorSetLayout, nullptr);
	vkDestroyDescriptorPool(mVulkanDevice->mLogicalDevice, mDescriptorPool, nullptr);
	vkDestroyPipelineLayout(mVulkanDevice->mLogicalDevice, mPipelineLayout, nullptr);
	vkDestroyPipelineCache(mVulkanDevice->mLogicalDevice, mPipelineCache, nullptr);
	vkDestroyPipeline(mVulkanDevice->mLogicalDevice, mPipeline, nullptr);
	vkDestroyRenderPass(mVulkanDevice->mLogicalDevice, mRenderPass, nullptr);
	vkFreeCommandBuffers(mVulkanDevice->mLogicalDevice, mCommandPool, static_cast<uint32_t>(mCmdBuffers.size()), mCmdBuffers.data());
	vkDestroyCommandPool(mVulkanDevice->mLogicalDevice, mCommandPool, nullptr);
	vkDestroyFence(mVulkanDevice->mLogicalDevice, mFence, nullptr);
}

void VulkanTextOverlay::prepareResources()
{
	static unsigned char font24pixels[STB_FONT_HEIGHT][STB_FONT_WIDTH];
	STB_FONT_NAME(mStbFontData, font24pixels, STB_FONT_HEIGHT);

	// Command buffer

	// Pool
	VkCommandPoolCreateInfo cmdPoolInfo = {};
	cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	cmdPoolInfo.queueFamilyIndex = mVulkanDevice->queueFamilyIndices.graphics;
	cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	VK_CHECK_RESULT(vkCreateCommandPool(mVulkanDevice->mLogicalDevice, &cmdPoolInfo, nullptr, &mCommandPool));

	VkCommandBufferAllocateInfo cmdBufAllocateInfo =
		vkTools::initializers::commandBufferAllocateInfo(
			mCommandPool,
			VK_COMMAND_BUFFER_LEVEL_PRIMARY,
			(uint32_t)mCmdBuffers.size());

	VK_CHECK_RESULT(vkAllocateCommandBuffers(mVulkanDevice->mLogicalDevice, &cmdBufAllocateInfo, mCmdBuffers.data()));

	// Vertex buffer
	VK_CHECK_RESULT(mVulkanDevice->createBuffer(
		VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		&mVertexBuffer,
		MAX_CHAR_COUNT * sizeof(glm::vec4)));

	// Map persistent
	mVertexBuffer.map();

	// Font texture
	VkImageCreateInfo imageInfo = vkTools::initializers::imageCreateInfo();
	imageInfo.imageType = VK_IMAGE_TYPE_2D;
	imageInfo.format = VK_FORMAT_R8_UNORM;
	imageInfo.extent.width = STB_FONT_WIDTH;
	imageInfo.extent.height = STB_FONT_HEIGHT;
	imageInfo.extent.depth = 1;
	imageInfo.mipLevels = 1;
	imageInfo.arrayLayers = 1;
	imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;
	VK_CHECK_RESULT(vkCreateImage(mVulkanDevice->mLogicalDevice, &imageInfo, nullptr, &mImage));

	VkMemoryRequirements memReqs;
	VkMemoryAllocateInfo allocInfo = vkTools::initializers::memoryAllocateInfo();
	vkGetImageMemoryRequirements(mVulkanDevice->mLogicalDevice, mImage, &memReqs);
	allocInfo.allocationSize = memReqs.size;
	allocInfo.memoryTypeIndex = mVulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	VK_CHECK_RESULT(vkAllocateMemory(mVulkanDevice->mLogicalDevice, &allocInfo, nullptr, &mImageMemory));
	VK_CHECK_RESULT(vkBindImageMemory(mVulkanDevice->mLogicalDevice, mImage, mImageMemory, 0));

	// Staging
	vk::Buffer stagingBuffer;

	VK_CHECK_RESULT(mVulkanDevice->createBuffer(
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		&stagingBuffer,
		allocInfo.allocationSize));

	stagingBuffer.map();
	memcpy(stagingBuffer.mapped, &font24pixels[0][0], STB_FONT_WIDTH * STB_FONT_HEIGHT);	// Only one channel, so data size = W * H (*R8)
	stagingBuffer.unmap();

	// Copy to image
	VkCommandBuffer copyCmd;
	cmdBufAllocateInfo.commandBufferCount = 1;
	VK_CHECK_RESULT(vkAllocateCommandBuffers(mVulkanDevice->mLogicalDevice, &cmdBufAllocateInfo, &copyCmd));

	VkCommandBufferBeginInfo cmdBufInfo = vkTools::initializers::commandBufferBeginInfo();
	VK_CHECK_RESULT(vkBeginCommandBuffer(copyCmd, &cmdBufInfo));

	// Prepare for transfer
	vkTools::setImageLayout(
		copyCmd,
		mImage,
		VK_IMAGE_ASPECT_COLOR_BIT,
		VK_IMAGE_LAYOUT_PREINITIALIZED,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

	VkBufferImageCopy bufferCopyRegion = {};
	bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	bufferCopyRegion.imageSubresource.mipLevel = 0;
	bufferCopyRegion.imageSubresource.layerCount = 1;
	bufferCopyRegion.imageExtent.width = STB_FONT_WIDTH;
	bufferCopyRegion.imageExtent.height = STB_FONT_HEIGHT;
	bufferCopyRegion.imageExtent.depth = 1;

	vkCmdCopyBufferToImage(
		copyCmd,
		stagingBuffer.buffer,
		mImage,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		1,
		&bufferCopyRegion
	);

	// Prepare for shader read
	vkTools::setImageLayout(
		copyCmd,
		mImage,
		VK_IMAGE_ASPECT_COLOR_BIT,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	VK_CHECK_RESULT(vkEndCommandBuffer(copyCmd));

	VkSubmitInfo submitInfo = vkTools::initializers::submitInfo();
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &copyCmd;

	VK_CHECK_RESULT(vkQueueSubmit(mQueue, 1, &submitInfo, VK_NULL_HANDLE));
	VK_CHECK_RESULT(vkQueueWaitIdle(mQueue));

	stagingBuffer.destroy();

	vkFreeCommandBuffers(mVulkanDevice->mLogicalDevice, mCommandPool, 1, &copyCmd);

	VkImageViewCreateInfo imageViewInfo = vkTools::initializers::imageViewCreateInfo();
	imageViewInfo.image = mImage;
	imageViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	imageViewInfo.format = imageInfo.format;
	imageViewInfo.components = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B,	VK_COMPONENT_SWIZZLE_A };
	imageViewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
	VK_CHECK_RESULT(vkCreateImageView(mVulkanDevice->mLogicalDevice, &imageViewInfo, nullptr, &mImageView));

	// Sampler
	VkSamplerCreateInfo samplerInfo = vkTools::initializers::samplerCreateInfo();
	samplerInfo.magFilter = VK_FILTER_LINEAR;
	samplerInfo.minFilter = VK_FILTER_LINEAR;
	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.mipLodBias = 0.0f;
	samplerInfo.compareOp = VK_COMPARE_OP_NEVER;
	samplerInfo.minLod = 0.0f;
	samplerInfo.maxLod = 1.0f;
	samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
	VK_CHECK_RESULT(vkCreateSampler(mVulkanDevice->mLogicalDevice, &samplerInfo, nullptr, &mSampler));

	// Descriptor
	// Font uses a separate descriptor pool
	std::array<VkDescriptorPoolSize, 1> poolSizes;
	poolSizes[0] = vkTools::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1);

	VkDescriptorPoolCreateInfo descriptorPoolInfo =
		vkTools::initializers::descriptorPoolCreateInfo(
			static_cast<uint32_t>(poolSizes.size()),
			poolSizes.data(),
			1);

	VK_CHECK_RESULT(vkCreateDescriptorPool(mVulkanDevice->mLogicalDevice, &descriptorPoolInfo, nullptr, &mDescriptorPool));

	// Descriptor set layout
	std::array<VkDescriptorSetLayoutBinding, 1> setLayoutBindings;
	setLayoutBindings[0] = vkTools::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 0);

	VkDescriptorSetLayoutCreateInfo descriptorSetLayoutInfo =
		vkTools::initializers::descriptorSetLayoutCreateInfo(
			setLayoutBindings.data(),
			static_cast<uint32_t>(setLayoutBindings.size()));

	VK_CHECK_RESULT(vkCreateDescriptorSetLayout(mVulkanDevice->mLogicalDevice, &descriptorSetLayoutInfo, nullptr, &mDescriptorSetLayout));

	// Pipeline layout
	VkPipelineLayoutCreateInfo pipelineLayoutInfo =
		vkTools::initializers::pipelineLayoutCreateInfo(
			&mDescriptorSetLayout,
			1);

	VK_CHECK_RESULT(vkCreatePipelineLayout(mVulkanDevice->mLogicalDevice, &pipelineLayoutInfo, nullptr, &mPipelineLayout));

	// Descriptor set
	VkDescriptorSetAllocateInfo descriptorSetAllocInfo =
		vkTools::initializers::descriptorSetAllocateInfo(
			mDescriptorPool,
			&mDescriptorSetLayout,
			1);

	VK_CHECK_RESULT(vkAllocateDescriptorSets(mVulkanDevice->mLogicalDevice, &descriptorSetAllocInfo, &mDescriptorSet));

	VkDescriptorImageInfo texDescriptor =
		vkTools::initializers::descriptorImageInfo(
			mSampler,
			mImageView,
			VK_IMAGE_LAYOUT_GENERAL);

	std::array<VkWriteDescriptorSet, 1> writeDescriptorSets;
	writeDescriptorSets[0] = vkTools::initializers::writeDescriptorSet(mDescriptorSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0, &texDescriptor);
	vkUpdateDescriptorSets(mVulkanDevice->mLogicalDevice, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);

	// Pipeline cache
	VkPipelineCacheCreateInfo pipelineCacheCreateInfo = {};
	pipelineCacheCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
	VK_CHECK_RESULT(vkCreatePipelineCache(mVulkanDevice->mLogicalDevice, &pipelineCacheCreateInfo, nullptr, &mPipelineCache));

	// Command buffer execution fence
	VkFenceCreateInfo fenceCreateInfo = vkTools::initializers::fenceCreateInfo();
	VK_CHECK_RESULT(vkCreateFence(mVulkanDevice->mLogicalDevice, &fenceCreateInfo, nullptr, &mFence));
}

void VulkanTextOverlay::preparePipeline()
{
	VkPipelineInputAssemblyStateCreateInfo inputAssemblyState =
		vkTools::initializers::pipelineInputAssemblyStateCreateInfo(
			VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
			0,
			VK_FALSE);

	VkPipelineRasterizationStateCreateInfo rasterizationState =
		vkTools::initializers::pipelineRasterizationStateCreateInfo(
			VK_POLYGON_MODE_FILL,
			VK_CULL_MODE_BACK_BIT,
			VK_FRONT_FACE_CLOCKWISE,
			0);

	// Enable blending
	VkPipelineColorBlendAttachmentState blendAttachmentState =
		vkTools::initializers::pipelineColorBlendAttachmentState(0xf, VK_TRUE);

	blendAttachmentState.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
	blendAttachmentState.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
	blendAttachmentState.colorBlendOp = VK_BLEND_OP_ADD;
	blendAttachmentState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	blendAttachmentState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	blendAttachmentState.alphaBlendOp = VK_BLEND_OP_ADD;
	blendAttachmentState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

	VkPipelineColorBlendStateCreateInfo colorBlendState =
		vkTools::initializers::pipelineColorBlendStateCreateInfo(
			1,
			&blendAttachmentState);

	VkPipelineDepthStencilStateCreateInfo depthStencilState =
		vkTools::initializers::pipelineDepthStencilStateCreateInfo(
			VK_FALSE,
			VK_FALSE,
			VK_COMPARE_OP_LESS_OR_EQUAL);

	VkPipelineViewportStateCreateInfo viewportState =
		vkTools::initializers::pipelineViewportStateCreateInfo(1, 1, 0);

	VkPipelineMultisampleStateCreateInfo multisampleState =
		vkTools::initializers::pipelineMultisampleStateCreateInfo(
			VK_SAMPLE_COUNT_1_BIT,
			0);

	std::vector<VkDynamicState> dynamicStateEnables = {
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR
	};

	VkPipelineDynamicStateCreateInfo dynamicState =
		vkTools::initializers::pipelineDynamicStateCreateInfo(
			dynamicStateEnables.data(),
			static_cast<uint32_t>(dynamicStateEnables.size()),
			0);

	std::array<VkVertexInputBindingDescription, 2> vertexBindings = {};
	vertexBindings[0] = vkTools::initializers::vertexInputBindingDescription(0, sizeof(glm::vec4), VK_VERTEX_INPUT_RATE_VERTEX);
	vertexBindings[1] = vkTools::initializers::vertexInputBindingDescription(1, sizeof(glm::vec4), VK_VERTEX_INPUT_RATE_VERTEX);

	std::array<VkVertexInputAttributeDescription, 2> vertexAttribs = {};
	// Position
	vertexAttribs[0] = vkTools::initializers::vertexInputAttributeDescription(0, 0, VK_FORMAT_R32G32_SFLOAT, 0);
	// UV
	vertexAttribs[1] = vkTools::initializers::vertexInputAttributeDescription(1, 1, VK_FORMAT_R32G32_SFLOAT, sizeof(glm::vec2));

	VkPipelineVertexInputStateCreateInfo inputState = vkTools::initializers::pipelineVertexInputStateCreateInfo();
	inputState.vertexBindingDescriptionCount = static_cast<uint32_t>(vertexBindings.size());
	inputState.pVertexBindingDescriptions = vertexBindings.data();
	inputState.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexAttribs.size());
	inputState.pVertexAttributeDescriptions = vertexAttribs.data();

	VkGraphicsPipelineCreateInfo pipelineCreateInfo =
		vkTools::initializers::pipelineCreateInfo(
			mPipelineLayout,
			mRenderPass,
			0);

	pipelineCreateInfo.pVertexInputState = &inputState;
	pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
	pipelineCreateInfo.pRasterizationState = &rasterizationState;
	pipelineCreateInfo.pColorBlendState = &colorBlendState;
	pipelineCreateInfo.pMultisampleState = &multisampleState;
	pipelineCreateInfo.pViewportState = &viewportState;
	pipelineCreateInfo.pDepthStencilState = &depthStencilState;
	pipelineCreateInfo.pDynamicState = &dynamicState;
	pipelineCreateInfo.stageCount = static_cast<uint32_t>(mShaderStages.size());
	pipelineCreateInfo.pStages = mShaderStages.data();

	VK_CHECK_RESULT(vkCreateGraphicsPipelines(mVulkanDevice->mLogicalDevice, mPipelineCache, 1, &pipelineCreateInfo, nullptr, &mPipeline));
}


void VulkanTextOverlay::prepareRenderPass()
{
	VkAttachmentDescription attachments[2] = {};

	// Color attachment
	attachments[0].format = mColorFormat;
	attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
	// Don't clear the framebuffer (like the renderpass from the example does)
	attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

	// Depth attachment
	attachments[1].format = mDepthFormat;
	attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
	attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkAttachmentReference colorReference = {};
	colorReference.attachment = 0;
	colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkAttachmentReference depthReference = {};
	depthReference.attachment = 1;
	depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkSubpassDependency subpassDependencies[2] = {};

	// Transition from final to initial (VK_SUBPASS_EXTERNAL refers to all commmands executed outside of the actual renderpass)
	subpassDependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
	subpassDependencies[0].dstSubpass = 0;
	subpassDependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
	subpassDependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	subpassDependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
	subpassDependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	subpassDependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

	// Transition from initial to final
	subpassDependencies[1].srcSubpass = 0;
	subpassDependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
	subpassDependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	subpassDependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
	subpassDependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	subpassDependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
	subpassDependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

	VkSubpassDescription subpassDescription = {};
	subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpassDescription.flags = 0;
	subpassDescription.inputAttachmentCount = 0;
	subpassDescription.pInputAttachments = NULL;
	subpassDescription.colorAttachmentCount = 1;
	subpassDescription.pColorAttachments = &colorReference;
	subpassDescription.pResolveAttachments = NULL;
	subpassDescription.pDepthStencilAttachment = &depthReference;
	subpassDescription.preserveAttachmentCount = 0;
	subpassDescription.pPreserveAttachments = NULL;

	VkRenderPassCreateInfo renderPassInfo = {};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassInfo.pNext = NULL;
	renderPassInfo.attachmentCount = 2;
	renderPassInfo.pAttachments = attachments;
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpassDescription;
	renderPassInfo.dependencyCount = 2;
	renderPassInfo.pDependencies = subpassDependencies;

	VK_CHECK_RESULT(vkCreateRenderPass(mVulkanDevice->mLogicalDevice, &renderPassInfo, nullptr, &mRenderPass));
}

void VulkanTextOverlay::beginTextUpdate()
{
	mMappedLocal = (glm::vec4*)mVertexBuffer.mapped;
	mNumLetters = 0;
}

void VulkanTextOverlay::addText(std::string text, float x, float y, TextAlign align)
{
	assert(mVertexBuffer.mapped != nullptr);

	const float charW = 1.5f / *mFrameBufferWidth;
	const float charH = 1.5f / *mFrameBufferHeight;

	float fbW = (float)*mFrameBufferWidth;
	float fbH = (float)*mFrameBufferHeight;
	x = (x / fbW * 2.0f) - 1.0f;
	y = (y / fbH * 2.0f) - 1.0f;

	// Calculate text width
	float textWidth = 0;
	for (auto letter : text)
	{
		stb_fontchar *charData = &mStbFontData[(uint32_t)letter - STB_FIRST_CHAR];
		textWidth += charData->advance * charW;
	}

	switch (align)
	{
	case alignRight:
		x -= textWidth;
		break;
	case alignCenter:
		x -= textWidth / 2.0f;
		break;
	case alignLeft:
		break;
	}

	// Generate a uv mapped quad per char in the new text
	for (auto letter : text)
	{
		stb_fontchar *charData = &mStbFontData[(uint32_t)letter - STB_FIRST_CHAR];

		mMappedLocal->x = (x + (float)charData->x0 * charW);
		mMappedLocal->y = (y + (float)charData->y0 * charH);
		mMappedLocal->z = charData->s0;
		mMappedLocal->w = charData->t0;
		mMappedLocal++;

		mMappedLocal->x = (x + (float)charData->x1 * charW);
		mMappedLocal->y = (y + (float)charData->y0 * charH);
		mMappedLocal->z = charData->s1;
		mMappedLocal->w = charData->t0;
		mMappedLocal++;

		mMappedLocal->x = (x + (float)charData->x0 * charW);
		mMappedLocal->y = (y + (float)charData->y1 * charH);
		mMappedLocal->z = charData->s0;
		mMappedLocal->w = charData->t1;
		mMappedLocal++;

		mMappedLocal->x = (x + (float)charData->x1 * charW);
		mMappedLocal->y = (y + (float)charData->y1 * charH);
		mMappedLocal->z = charData->s1;
		mMappedLocal->w = charData->t1;
		mMappedLocal++;

		x += charData->advance * charW;

		mNumLetters++;
	}
}

void VulkanTextOverlay::endTextUpdate()
{
	updateCommandBuffers();
}

void VulkanTextOverlay::updateCommandBuffers()
{
	VkCommandBufferBeginInfo cmdBufInfo = vkTools::initializers::commandBufferBeginInfo();

	VkClearValue clearValues[1];
	clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };

	VkRenderPassBeginInfo renderPassBeginInfo = vkTools::initializers::renderPassBeginInfo();
	renderPassBeginInfo.renderPass = mRenderPass;
	renderPassBeginInfo.renderArea.extent.width = *mFrameBufferWidth;
	renderPassBeginInfo.renderArea.extent.height = *mFrameBufferHeight;
	renderPassBeginInfo.clearValueCount = 1;
	renderPassBeginInfo.pClearValues = clearValues;

	for (int32_t i = 0; i < mCmdBuffers.size(); ++i)
	{
		renderPassBeginInfo.framebuffer = *mFrameBuffers[i];

		VK_CHECK_RESULT(vkBeginCommandBuffer(mCmdBuffers[i], &cmdBufInfo));

		if (vkDebug::DebugMarker::active)
		{
			vkDebug::DebugMarker::beginRegion(mCmdBuffers[i], "Text overlay", glm::vec4(1.0f, 0.94f, 0.3f, 1.0f));
		}

		vkCmdBeginRenderPass(mCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

		VkViewport viewport = vkTools::initializers::viewport((float)*mFrameBufferWidth, (float)*mFrameBufferHeight, 0.0f, 1.0f);
		vkCmdSetViewport(mCmdBuffers[i], 0, 1, &viewport);

		VkRect2D scissor = vkTools::initializers::rect2D(*mFrameBufferWidth, *mFrameBufferHeight, 0, 0);
		vkCmdSetScissor(mCmdBuffers[i], 0, 1, &scissor);

		vkCmdBindPipeline(mCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, mPipeline);
		vkCmdBindDescriptorSets(mCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, mPipelineLayout, 0, 1, &mDescriptorSet, 0, NULL);

		VkDeviceSize offsets = 0;
		vkCmdBindVertexBuffers(mCmdBuffers[i], 0, 1, &mVertexBuffer.buffer, &offsets);
		vkCmdBindVertexBuffers(mCmdBuffers[i], 1, 1, &mVertexBuffer.buffer, &offsets);
		for (uint32_t j = 0; j < mNumLetters; j++)
		{
			vkCmdDraw(mCmdBuffers[i], 4, 1, j * 4, 0);
		}

		vkCmdEndRenderPass(mCmdBuffers[i]);

		if (vkDebug::DebugMarker::active)
		{
			vkDebug::DebugMarker::endRegion(mCmdBuffers[i]);
		}

		VK_CHECK_RESULT(vkEndCommandBuffer(mCmdBuffers[i]));
	}
}

void VulkanTextOverlay::submit(VkQueue queue, uint32_t bufferindex)
{
	if (!mVisible)
	{
		return;
	}
	VkSubmitInfo submitInfo = {};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.pCommandBuffers = &mCmdBuffers[bufferindex];
	submitInfo.commandBufferCount = 1;

	VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, mFence));

	VK_CHECK_RESULT(vkWaitForFences(mVulkanDevice->mLogicalDevice, 1, &mFence, VK_TRUE, UINT64_MAX));
	VK_CHECK_RESULT(vkResetFences(mVulkanDevice->mLogicalDevice, 1, &mFence));
}

void VulkanTextOverlay::reallocateCommandBuffers()
{
	vkFreeCommandBuffers(mVulkanDevice->mLogicalDevice, mCommandPool, static_cast<uint32_t>(mCmdBuffers.size()), mCmdBuffers.data());

	VkCommandBufferAllocateInfo cmdBufAllocateInfo =
		vkTools::initializers::commandBufferAllocateInfo(
			mCommandPool,
			VK_COMMAND_BUFFER_LEVEL_PRIMARY,
			static_cast<uint32_t>(mCmdBuffers.size()));

	VK_CHECK_RESULT(vkAllocateCommandBuffers(mVulkanDevice->mLogicalDevice, &cmdBufAllocateInfo, mCmdBuffers.data()));
}

