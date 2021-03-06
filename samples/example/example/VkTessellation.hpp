#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <vector>
#include "define.h"

#include <vulkan/vulkan.h>
#include "VulkanBase.h"

#define ENABLE_VALIDATION false

class VkTessellation : public VulkanBase
{
	// Vertex layout for this example
	std::vector<vkMeshLoader::VertexLayout> vertexLayout =
	{
		vkMeshLoader::VERTEX_LAYOUT_POSITION,
		vkMeshLoader::VERTEX_LAYOUT_NORMAL,
		vkMeshLoader::VERTEX_LAYOUT_UV
	};
public:
	bool splitScreen = true;

	struct {
		vkTools::VulkanTexture colorMap;
	} textures;

	struct {
		VkPipelineVertexInputStateCreateInfo inputState;
		std::vector<VkVertexInputBindingDescription> bindingDescriptions;
		std::vector<VkVertexInputAttributeDescription> attributeDescriptions;
	} vertices;

	struct {
		vkMeshLoader::MeshBuffer object;
	} meshes;

	vkTools::UniformData uniformDataTC, uniformDataTE;

	struct {
		float tessLevel = 3.0f;
	} uboTC;

	struct {
		Matrix projection;
		Matrix model;
		float tessAlpha = 1.0f;
	} uboTE;

	struct {
		VkPipeline solid;
		VkPipeline wire;
		VkPipeline solidPassThrough;
		VkPipeline wirePassThrough;
	} pipelines;
	VkPipeline *pipelineLeft = &pipelines.wirePassThrough;
	VkPipeline *pipelineRight = &pipelines.wire;

	VkPipelineLayout pipelineLayout;
	VkDescriptorSet descriptorSet;
	VkDescriptorSetLayout descriptorSetLayout;

	VkTessellation() : VulkanBase(ENABLE_VALIDATION)
	{
		mZoom = -6.5f;
		mRotation = Vector3(-350.0f, 60.0f, 0.0f);
		cameraPos = Vector3(-3.0f, 2.3f, 0.0f);
		title = "Tessellation shader (PN Triangles)";
		mEnableTextOverlay = true;
		// Support for tessellation shaders is optional, so check first
		if (!mVulkanDevice->mFeatures.tessellationShader)
		{
			vkTools::exitFatal("Selected GPU does not support tessellation shaders!", "Feature not supported");
		}
	}

	~VkTessellation()
	{
		// Clean up used Vulkan resources 
		// Note : Inherited destructor cleans up resources stored in base class
		vkDestroyPipeline(mVulkanDevice->mLogicalDevice, pipelines.solid, nullptr);
		vkDestroyPipeline(mVulkanDevice->mLogicalDevice, pipelines.wire, nullptr);
		vkDestroyPipeline(mVulkanDevice->mLogicalDevice, pipelines.solidPassThrough, nullptr);
		vkDestroyPipeline(mVulkanDevice->mLogicalDevice, pipelines.wirePassThrough, nullptr);

		vkDestroyPipelineLayout(mVulkanDevice->mLogicalDevice, pipelineLayout, nullptr);
		vkDestroyDescriptorSetLayout(mVulkanDevice->mLogicalDevice, descriptorSetLayout, nullptr);

		vkMeshLoader::freeMeshBufferResources(mVulkanDevice->mLogicalDevice, &meshes.object);

		vkDestroyBuffer(mVulkanDevice->mLogicalDevice, uniformDataTC.buffer, nullptr);
		vkFreeMemory(mVulkanDevice->mLogicalDevice, uniformDataTC.memory, nullptr);

		vkDestroyBuffer(mVulkanDevice->mLogicalDevice, uniformDataTE.buffer, nullptr);
		vkFreeMemory(mVulkanDevice->mLogicalDevice, uniformDataTE.memory, nullptr);

		textureLoader->destroyTexture(textures.colorMap);
	}

	void reBuildCommandBuffers()
	{
		if (!checkCommandBuffers())
		{
			destroyCommandBuffers();
			createCommandBuffers();
		}
		buildCommandBuffers();
	}

	void buildCommandBuffers()
	{
		VkCommandBufferBeginInfo cmdBufInfo = vkTools::commandBufferBeginInfo();

		VkClearValue clearValues[2];
		clearValues[0].color = { { 0.5f, 0.5f, 0.5f, 0.0f } };
		clearValues[1].depthStencil = { 1.0f, 0 };

		VkRenderPassBeginInfo renderPassBeginInfo = vkTools::renderPassBeginInfo();
		renderPassBeginInfo.renderPass = mRenderPass;
		renderPassBeginInfo.renderArea.offset.x = 0;
		renderPassBeginInfo.renderArea.offset.y = 0;
		renderPassBeginInfo.renderArea.extent.width = width;
		renderPassBeginInfo.renderArea.extent.height = height;
		renderPassBeginInfo.clearValueCount = 2;
		renderPassBeginInfo.pClearValues = clearValues;

		for (int32_t i = 0; i < mDrawCmdBuffers.size(); ++i)
		{
			// Set target frame buffer
			renderPassBeginInfo.framebuffer = mFrameBuffers[i];

			VK_CHECK_RESULT(vkBeginCommandBuffer(mDrawCmdBuffers[i], &cmdBufInfo));

			vkCmdBeginRenderPass(mDrawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

			VkViewport viewport = vkTools::viewport(splitScreen ? (float)width / 2.0f : (float)width, (float)height, 0.0f, 1.0f);
			vkCmdSetViewport(mDrawCmdBuffers[i], 0, 1, &viewport);

			VkRect2D scissor = vkTools::rect2D(width, height, 0, 0);
			vkCmdSetScissor(mDrawCmdBuffers[i], 0, 1, &scissor);

			vkCmdSetLineWidth(mDrawCmdBuffers[i], 1.0f);

			vkCmdBindDescriptorSets(mDrawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, NULL);

			VkDeviceSize offsets[1] = { 0 };
			vkCmdBindVertexBuffers(mDrawCmdBuffers[i], VERTEX_BUFFER_BIND_ID, 1, &meshes.object.vertices.buf, offsets);
			vkCmdBindIndexBuffer(mDrawCmdBuffers[i], meshes.object.indices.buf, 0, VK_INDEX_TYPE_UINT32);

			if (splitScreen)
			{
				vkCmdSetViewport(mDrawCmdBuffers[i], 0, 1, &viewport);
				vkCmdBindPipeline(mDrawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, *pipelineLeft);
				vkCmdDrawIndexed(mDrawCmdBuffers[i], meshes.object.indexCount, 1, 0, 0, 0);
				viewport.x = float(width) / 2;
			}

			vkCmdSetViewport(mDrawCmdBuffers[i], 0, 1, &viewport);
			vkCmdBindPipeline(mDrawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, *pipelineRight);
			vkCmdDrawIndexed(mDrawCmdBuffers[i], meshes.object.indexCount, 1, 0, 0, 0);

			vkCmdEndRenderPass(mDrawCmdBuffers[i]);

			VK_CHECK_RESULT(vkEndCommandBuffer(mDrawCmdBuffers[i]));
		}
	}

	void loadMeshes()
	{
		loadMesh(getAssetPath() + "models/lowpoly/deer.dae", &meshes.object, vertexLayout, 1.0f);
	}

	void loadTextures()
	{
		textureLoader->loadTexture(
			getAssetPath() + "textures/deer.ktx",
			VK_FORMAT_BC3_UNORM_BLOCK,
			&textures.colorMap);
	}

	void setupVertexDescriptions()
	{
		// Binding description
		vertices.bindingDescriptions.resize(1);
		vertices.bindingDescriptions[0] =
			vkTools::vertexInputBindingDescription(
				VERTEX_BUFFER_BIND_ID,
				vkMeshLoader::vertexSize(vertexLayout),
				VK_VERTEX_INPUT_RATE_VERTEX);

		// Attribute descriptions
		// Describes memory layout and shader positions
		vertices.attributeDescriptions.resize(3);

		// Location 0 : Position
		vertices.attributeDescriptions[0] =
			vkTools::vertexInputAttributeDescription(
				VERTEX_BUFFER_BIND_ID,
				0,
				VK_FORMAT_R32G32B32_SFLOAT,
				0);

		// Location 1 : Normals
		vertices.attributeDescriptions[1] =
			vkTools::vertexInputAttributeDescription(
				VERTEX_BUFFER_BIND_ID,
				1,
				VK_FORMAT_R32G32B32_SFLOAT,
				sizeof(float) * 3);

		// Location 2 : Texture coordinates
		vertices.attributeDescriptions[2] =
			vkTools::vertexInputAttributeDescription(
				VERTEX_BUFFER_BIND_ID,
				2,
				VK_FORMAT_R32G32_SFLOAT,
				sizeof(float) * 6);

		vertices.inputState = vkTools::pipelineVertexInputStateCreateInfo();
		vertices.inputState.vertexBindingDescriptionCount = vertices.bindingDescriptions.size();
		vertices.inputState.pVertexBindingDescriptions = vertices.bindingDescriptions.data();
		vertices.inputState.vertexAttributeDescriptionCount = vertices.attributeDescriptions.size();
		vertices.inputState.pVertexAttributeDescriptions = vertices.attributeDescriptions.data();
	}

	void setupDescriptorPool()
	{
		// Example uses two ubos and one combined image sampler
		std::vector<VkDescriptorPoolSize> poolSizes =
		{
			vkTools::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2),
			vkTools::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1),
		};

		VkDescriptorPoolCreateInfo descriptorPoolInfo =
			vkTools::descriptorPoolCreateInfo(
				poolSizes.size(),
				poolSizes.data(),
				1);

		VK_CHECK_RESULT(vkCreateDescriptorPool(mVulkanDevice->mLogicalDevice, &descriptorPoolInfo, nullptr, &descriptorPool));
	}

	void setupDescriptorSetLayout()
	{
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings =
		{
			// Binding 0 : Tessellation control shader ubo
			vkTools::descriptorSetLayoutBinding(
				VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,
				0),
			// Binding 1 : Tessellation evaluation shader ubo
			vkTools::descriptorSetLayoutBinding(
				VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
				1),
			// Binding 2 : Fragment shader combined sampler
			vkTools::descriptorSetLayoutBinding(
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT,
				2),
		};

		VkDescriptorSetLayoutCreateInfo descriptorLayout =
			vkTools::descriptorSetLayoutCreateInfo(
				setLayoutBindings.data(),
				setLayoutBindings.size());

		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(mVulkanDevice->mLogicalDevice, &descriptorLayout, nullptr, &descriptorSetLayout));

		VkPipelineLayoutCreateInfo pPipelineLayoutCreateInfo =
			vkTools::pipelineLayoutCreateInfo(
				&descriptorSetLayout,
				1);

		VK_CHECK_RESULT(vkCreatePipelineLayout(mVulkanDevice->mLogicalDevice, &pPipelineLayoutCreateInfo, nullptr, &pipelineLayout));
	}

	void setupDescriptorSet()
	{
		VkDescriptorSetAllocateInfo allocInfo =
			vkTools::descriptorSetAllocateInfo(
				descriptorPool,
				&descriptorSetLayout,
				1);

		VK_CHECK_RESULT(vkAllocateDescriptorSets(mVulkanDevice->mLogicalDevice, &allocInfo, &descriptorSet));

		VkDescriptorImageInfo texDescriptor =
			vkTools::descriptorImageInfo(
				textures.colorMap.sampler,
				textures.colorMap.view,
				VK_IMAGE_LAYOUT_GENERAL);

		std::vector<VkWriteDescriptorSet> writeDescriptorSets =
		{
			// Binding 0 : Tessellation control shader ubo
			vkTools::writeDescriptorSet(
				descriptorSet,
				VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				0,
				&uniformDataTC.descriptor),
			// Binding 1 : Tessellation evaluation shader ubo
			vkTools::writeDescriptorSet(
				descriptorSet,
				VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				1,
				&uniformDataTE.descriptor),
			// Binding 2 : Color map 
			vkTools::writeDescriptorSet(
				descriptorSet,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				2,
				&texDescriptor)
		};

		vkUpdateDescriptorSets(mVulkanDevice->mLogicalDevice, writeDescriptorSets.size(), writeDescriptorSets.data(), 0, NULL);
	}

	void preparePipelines()
	{
		VkPipelineInputAssemblyStateCreateInfo inputAssemblyState =
			vkTools::pipelineInputAssemblyStateCreateInfo(
				VK_PRIMITIVE_TOPOLOGY_PATCH_LIST,
				0,
				VK_FALSE);

		VkPipelineRasterizationStateCreateInfo rasterizationState =
			vkTools::pipelineRasterizationStateCreateInfo(
				VK_POLYGON_MODE_FILL,
				VK_CULL_MODE_BACK_BIT,
				VK_FRONT_FACE_COUNTER_CLOCKWISE,
				0);

		VkPipelineColorBlendAttachmentState blendAttachmentState =
			vkTools::pipelineColorBlendAttachmentState(
				0xf,
				VK_FALSE);

		VkPipelineColorBlendStateCreateInfo colorBlendState =
			vkTools::pipelineColorBlendStateCreateInfo(
				1,
				&blendAttachmentState);

		VkPipelineDepthStencilStateCreateInfo depthStencilState =
			vkTools::pipelineDepthStencilStateCreateInfo(
				VK_TRUE,
				VK_TRUE,
				VK_COMPARE_OP_LESS_OR_EQUAL);

		VkPipelineViewportStateCreateInfo viewportState =
			vkTools::pipelineViewportStateCreateInfo(1, 1, 0);

		VkPipelineMultisampleStateCreateInfo multisampleState =
			vkTools::pipelineMultisampleStateCreateInfo(
				VK_SAMPLE_COUNT_1_BIT,
				0);

		std::vector<VkDynamicState> dynamicStateEnables = {
			VK_DYNAMIC_STATE_VIEWPORT,
			VK_DYNAMIC_STATE_SCISSOR,
			VK_DYNAMIC_STATE_LINE_WIDTH
		};
		VkPipelineDynamicStateCreateInfo dynamicState =
			vkTools::pipelineDynamicStateCreateInfo(
				dynamicStateEnables.data(),
				dynamicStateEnables.size(),
				0);

		VkPipelineTessellationStateCreateInfo tessellationState =
			vkTools::pipelineTessellationStateCreateInfo(3);

		// Tessellation pipelines
		// Load shaders
		std::array<VkPipelineShaderStageCreateInfo, 4> shaderStages;

		shaderStages[0] = loadShader(getAssetPath() + "shaders/tessellation/base.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getAssetPath() + "shaders/tessellation/base.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		shaderStages[2] = loadShader(getAssetPath() + "shaders/tessellation/pntriangles.tesc.spv", VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT);
		shaderStages[3] = loadShader(getAssetPath() + "shaders/tessellation/pntriangles.tese.spv", VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT);

		VkGraphicsPipelineCreateInfo pipelineCreateInfo =
			vkTools::pipelineCreateInfo(
				pipelineLayout,
				mRenderPass,
				0);

		pipelineCreateInfo.pVertexInputState = &vertices.inputState;
		pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
		pipelineCreateInfo.pRasterizationState = &rasterizationState;
		pipelineCreateInfo.pColorBlendState = &colorBlendState;
		pipelineCreateInfo.pMultisampleState = &multisampleState;
		pipelineCreateInfo.pViewportState = &viewportState;
		pipelineCreateInfo.pDepthStencilState = &depthStencilState;
		pipelineCreateInfo.pDynamicState = &dynamicState;
		pipelineCreateInfo.pTessellationState = &tessellationState;
		pipelineCreateInfo.stageCount = shaderStages.size();
		pipelineCreateInfo.pStages = shaderStages.data();
		pipelineCreateInfo.renderPass = mRenderPass;

		// Tessellation pipelines
		// Solid
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(mVulkanDevice->mLogicalDevice, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.solid));
		// Wireframe
		rasterizationState.polygonMode = VK_POLYGON_MODE_LINE;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(mVulkanDevice->mLogicalDevice, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.wire));

		// Pass through pipelines
		// Load pass through tessellation shaders (Vert and frag are reused)
		shaderStages[2] = loadShader(getAssetPath() + "shaders/tessellation/passthrough.tesc.spv", VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT);
		shaderStages[3] = loadShader(getAssetPath() + "shaders/tessellation/passthrough.tese.spv", VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT);

		// Solid
		rasterizationState.polygonMode = VK_POLYGON_MODE_FILL;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(mVulkanDevice->mLogicalDevice, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.solidPassThrough));
		// Wireframe
		rasterizationState.polygonMode = VK_POLYGON_MODE_LINE;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(mVulkanDevice->mLogicalDevice, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.wirePassThrough));
	}

	// Prepare and initialize uniform buffer containing shader uniforms
	void prepareUniformBuffers()
	{
		// Tessellation evaluation shader uniform buffer
		createBuffer(
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			sizeof(uboTE),
			&uboTE,
			&uniformDataTE.buffer,
			&uniformDataTE.memory,
			&uniformDataTE.descriptor);

		// Tessellation control shader uniform buffer
		createBuffer(
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			sizeof(uboTC),
			&uboTC,
			&uniformDataTC.buffer,
			&uniformDataTC.memory,
			&uniformDataTC.descriptor);

		updateUniformBuffers();
	}

	void updateUniformBuffers()
	{
		// Tessellation eval
		Matrix viewMatrix, matTmp;
		Matrix::createPerspectiveVK(MATH_DEG_TO_RAD(45.0f), (float)(width* ((splitScreen) ? 0.5f : 1.0f)) / (float)height, 0.1f, 256.0f, &uboTE.projection);
		viewMatrix.translate(0.0f, 0.0f, mZoom);
		//uboTE.projection = glm::perspective(glm::radians(45.0f), (float)(width* ((splitScreen) ? 0.5f : 1.0f)) / (float)height, 0.1f, 256.0f);
		//viewMatrix = glm::translate(viewMatrix, glm::vec3(0.0f, 0.0f, mZoom));

		uboTE.model.setIdentity();
		matTmp.translate(cameraPos);
		uboTE.model = viewMatrix * matTmp;
		uboTE.model.rotateX(MATH_DEG_TO_RAD(mRotation.x));
		uboTE.model.rotateY(MATH_DEG_TO_RAD(mRotation.y));
		uboTE.model.rotateZ(MATH_DEG_TO_RAD(mRotation.z));
		//uboTE.model = viewMatrix * glm::translate(uboTE.model, cameraPos);
		//uboTE.model = glm::rotate(uboTE.model, glm::radians(mRotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
		//uboTE.model = glm::rotate(uboTE.model, glm::radians(mRotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
		//uboTE.model = glm::rotate(uboTE.model, glm::radians(mRotation.z), glm::vec3(0.0f, 0.0f, 1.0f));

		uint8_t *pData;

		// Tessellatione evaulation uniform block
		VK_CHECK_RESULT(vkMapMemory(mVulkanDevice->mLogicalDevice, uniformDataTE.memory, 0, sizeof(uboTE), 0, (void **)&pData));
		memcpy(pData, &uboTE, sizeof(uboTE));
		vkUnmapMemory(mVulkanDevice->mLogicalDevice, uniformDataTE.memory);

		// Tessellation control uniform block
		VK_CHECK_RESULT(vkMapMemory(mVulkanDevice->mLogicalDevice, uniformDataTC.memory, 0, sizeof(uboTC), 0, (void **)&pData));
		memcpy(pData, &uboTC, sizeof(uboTC));
		vkUnmapMemory(mVulkanDevice->mLogicalDevice, uniformDataTC.memory);
	}

	void draw()
	{
		VulkanBase::prepareFrame();

		mSubmitInfo.commandBufferCount = 1;
		mSubmitInfo.pCommandBuffers = &mDrawCmdBuffers[gSwapChain.mCurrentBuffer];
		VK_CHECK_RESULT(vkQueueSubmit(mQueue, 1, &mSubmitInfo, VK_NULL_HANDLE));

		VulkanBase::submitFrame();
	}

	void prepare()
	{
		VulkanBase::prepare();
		loadTextures();
		loadMeshes();
		setupVertexDescriptions();
		prepareUniformBuffers();
		setupDescriptorSetLayout();
		preparePipelines();
		setupDescriptorPool();
		setupDescriptorSet();
		buildCommandBuffers();
		prepared = true;
	}

	virtual void render()
	{
		if (!prepared)
			return;
		vkDeviceWaitIdle(mVulkanDevice->mLogicalDevice);
		draw();
		vkDeviceWaitIdle(mVulkanDevice->mLogicalDevice);
	}

	virtual void viewChanged()
	{
		updateUniformBuffers();
	}

	virtual void keyPressed(uint32_t keyCode)
	{
		switch (keyCode)
		{
		case Keyboard::KEY_KPADD:
		case GAMEPAD_BUTTON_R1:
			changeTessellationLevel(0.25);
			break;
		case Keyboard::KEY_KPSUB:
		case GAMEPAD_BUTTON_L1:
			changeTessellationLevel(-0.25);
			break;
		case Keyboard::KEY_W:
		case GAMEPAD_BUTTON_A:
			togglePipelines();
			break;
		case Keyboard::KEY_S:
		case GAMEPAD_BUTTON_X:
			toggleSplitScreen();
			break;
		}
	}

	virtual void getOverlayText(VulkanTextOverlay *textOverlay)
	{
		std::stringstream ss;
		ss << std::setprecision(2) << std::fixed << uboTC.tessLevel;
#if defined(__ANDROID__)
		textOverlay->addText("Tessellation level: " + ss.str() + " (Buttons L1/R1 to change)", 5.0f, 85.0f, VulkanTextOverlay::alignLeft);
		textOverlay->addText("Press \"Button X\" to toggle splitscreen", 5.0f, 100.0f, VulkanTextOverlay::alignLeft);
#else
		textOverlay->addText("Tessellation level: " + ss.str() + " (NUMPAD +/- to change)", 5.0f, 85.0f, VulkanTextOverlay::alignLeft);
		textOverlay->addText("Press \"s\" to toggle splitscreen", 5.0f, 100.0f, VulkanTextOverlay::alignLeft);
#endif
	}

	void changeTessellationLevel(float delta)
	{
		uboTC.tessLevel += delta;
		// Clamp
		uboTC.tessLevel = fmax(1.0f, fmin(uboTC.tessLevel, 32.0f));
		updateUniformBuffers();
		updateTextOverlay();
	}

	void togglePipelines()
	{
		if (pipelineRight == &pipelines.solid)
		{
			pipelineRight = &pipelines.wire;
			pipelineLeft = &pipelines.wirePassThrough;
		}
		else
		{
			pipelineRight = &pipelines.solid;
			pipelineLeft = &pipelines.solidPassThrough;
		}
		reBuildCommandBuffers();
	}

	void toggleSplitScreen()
	{
		splitScreen = !splitScreen;
		updateUniformBuffers();
		reBuildCommandBuffers();
	}

};
