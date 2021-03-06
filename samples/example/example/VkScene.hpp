#pragma once
#include "define.h"

#include <vulkan/vulkan.h>
#include "VulkanBase.h"

#define ENABLE_VALIDATION false

class VkScene : public VulkanBase
{
public:

	struct DemoMesh
	{
		vk::Buffer vertexBuffer;
		vk::Buffer indexBuffer;
		uint32_t indexCount;
		VkPipeline *pipeline;

		void draw(VkCommandBuffer cmdBuffer)
		{
			VkDeviceSize offsets[1] = { 0 };
			vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, *pipeline);
			vkCmdBindVertexBuffers(cmdBuffer, VERTEX_BUFFER_BIND_ID, 1, &vertexBuffer.buffer, offsets);
			vkCmdBindIndexBuffer(cmdBuffer, indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
			vkCmdDrawIndexed(cmdBuffer, indexCount, 1, 0, 0, 0);
		}
	};

	struct DemoMeshes
	{
		std::vector<std::string> names{ "logos", "background", "models", "skybox" };
		VkPipelineVertexInputStateCreateInfo inputState;
		std::vector<VkVertexInputBindingDescription> bindingDescriptions;
		std::vector<VkVertexInputAttributeDescription> attributeDescriptions;
		DemoMesh logos;
		DemoMesh background;
		DemoMesh models;
		DemoMesh skybox;
	} demoMeshes;
	std::vector<DemoMesh> meshes;

	struct {
		vk::Buffer meshVS;
	} uniformData;

	struct {
		Matrix projection;
		Matrix model;
		Matrix normal;
		Matrix view;
		Vector4 lightPos;
	} uboVS;

	struct
	{
		vkTools::VulkanTexture skybox;
	} textures;

	struct {
		VkPipeline logos;
		VkPipeline models;
		VkPipeline skybox;
	} pipelines;

	VkPipelineLayout pipelineLayout;
	VkDescriptorSet descriptorSet;
	VkDescriptorSetLayout descriptorSetLayout;

	Vector4 lightPos = Vector4(1.0f, 2.0f, 0.0f, 0.0f);

	VkScene() : VulkanBase(ENABLE_VALIDATION)
	{
		width = 1280;
		height = 720;
		mZoom = -3.75f;
		rotationSpeed = 0.5f;
		mRotation = Vector3(15.0f, 0.f, 0.0f);
		mEnableTextOverlay = true;
		title = "Vulkan Demo Scene - (c) 2016 by Sascha Willems";
	}

	~VkScene()
	{
		// Clean up used Vulkan resources 
		// Note : Inherited destructor cleans up resources stored in base class
		vkDestroyPipeline(mVulkanDevice->mLogicalDevice, pipelines.logos, nullptr);
		vkDestroyPipeline(mVulkanDevice->mLogicalDevice, pipelines.models, nullptr);
		vkDestroyPipeline(mVulkanDevice->mLogicalDevice, pipelines.skybox, nullptr);

		vkDestroyPipelineLayout(mVulkanDevice->mLogicalDevice, pipelineLayout, nullptr);
		vkDestroyDescriptorSetLayout(mVulkanDevice->mLogicalDevice, descriptorSetLayout, nullptr);

		uniformData.meshVS.destroy();

		for (auto mesh : meshes)
		{
			mesh.vertexBuffer.destroy();
			mesh.indexBuffer.destroy();
		}

		textureLoader->destroyTexture(textures.skybox);
	}

	void loadTextures()
	{
		textureLoader->loadCubemap(
			getAssetPath() + "textures/cubemap_vulkan.ktx",
			VK_FORMAT_R8G8B8A8_UNORM,
			&textures.skybox);
	}

	void buildCommandBuffers()
	{
		VkCommandBufferBeginInfo cmdBufInfo = vkTools::commandBufferBeginInfo();

		VkClearValue clearValues[2];
		clearValues[0].color = defaultClearColor;
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
			renderPassBeginInfo.framebuffer = mFrameBuffers[i];

			VK_CHECK_RESULT(vkBeginCommandBuffer(mDrawCmdBuffers[i], &cmdBufInfo));

			vkCmdBeginRenderPass(mDrawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

			VkViewport viewport = vkTools::viewport((float)width, (float)height, 0.0f, 1.0f);
			vkCmdSetViewport(mDrawCmdBuffers[i], 0, 1, &viewport);

			VkRect2D scissor = vkTools::rect2D(width, height, 0, 0);
			vkCmdSetScissor(mDrawCmdBuffers[i], 0, 1, &scissor);

			vkCmdBindDescriptorSets(mDrawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, NULL);

			VkDeviceSize offsets[1] = { 0 };
			for (auto mesh : meshes)
			{
				mesh.draw(mDrawCmdBuffers[i]);
			}

			vkCmdEndRenderPass(mDrawCmdBuffers[i]);

			VK_CHECK_RESULT(vkEndCommandBuffer(mDrawCmdBuffers[i]));
		}
	}

	void prepareVertices()
	{
		struct Vertex
		{
			float pos[3];
			float normal[3];
			float uv[2];
			float color[3];
		};

		std::vector<std::string> meshFiles = { "vulkanscenelogos.dae", "vulkanscenebackground.dae", "vulkanscenemodels.dae", "cube.obj" };
		std::vector<VkPipeline*> meshPipelines = { &pipelines.logos, &pipelines.models, &pipelines.models, &pipelines.skybox };

		// todo : Use mesh function for loading
		float scale = 1.0f;
		for (auto i = 0; i < meshFiles.size(); i++)
		{
			VulkanMeshLoader scene(mVulkanDevice);

#if defined(__ANDROID__)
			scene.assetManager = androidApp->activity->assetManager;
#endif
			scene.LoadMesh(getAssetPath() + "models/" + meshFiles[i]);

			// Generate vertex buffer (pos, normal, uv, color)
			std::vector<Vertex> vertexBuffer;
			Vector3 offset;
			// Offset on Y (except skypbox)
			if (meshFiles[i] != "cube.obj")
			{
				offset.y += 1.15f;
			}
			for (size_t m = 0; m < scene.m_Entries.size(); m++)
			{
				for (size_t v = 0; v < scene.m_Entries[m].Vertices.size(); v++)
				{
					Vector3 pos = (scene.m_Entries[m].Vertices[v].m_pos + offset) * scale;
					Vector3 normal = scene.m_Entries[m].Vertices[v].m_normal;
					Vector2 uv = scene.m_Entries[m].Vertices[v].m_tex;
					Vector3 col = scene.m_Entries[m].Vertices[v].m_color;
					Vertex vert =
					{
						{ pos.x, pos.y, pos.z },
						{ normal.x, -normal.y, normal.z },
						{ uv.x, uv.y },
						{ col.x, col.y, col.z }
					};

					vertexBuffer.push_back(vert);
				}
			}

			std::vector<uint32_t> indexBuffer;
			for (size_t m = 0; m < scene.m_Entries.size(); m++)
			{
				int indexBase = indexBuffer.size();
				for (size_t i = 0; i < scene.m_Entries[m].Indices.size(); i++) {
					indexBuffer.push_back(scene.m_Entries[m].Indices[i] + indexBase);
				}
			}

			DemoMesh mesh;

			mesh.indexCount = static_cast<uint32_t>(indexBuffer.size());
			mesh.pipeline = meshPipelines[i];

			uint32_t vertexBufferSize = static_cast<uint32_t>(vertexBuffer.size()) * sizeof(Vertex);
			uint32_t indexBufferSize = static_cast<uint32_t>(indexBuffer.size()) * sizeof(uint32_t);

			vk::Buffer vertexStaging, indexStaging;

			// Create staging buffers
			// Vertex data
			mVulkanDevice->createBuffer(
				VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
				&vertexStaging,
				vertexBufferSize,
				vertexBuffer.data());
			// Index data
			mVulkanDevice->createBuffer(
				VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
				&indexStaging,
				indexBufferSize,
				indexBuffer.data());

			// Create device local buffers
			// Vertex buffer
			mVulkanDevice->createBuffer(
				VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
				&mesh.vertexBuffer,
				vertexBufferSize);
			// Index buffer
			mVulkanDevice->createBuffer(
				VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
				&mesh.indexBuffer,
				indexBufferSize);

			// Copy from staging buffers
			mVulkanDevice->copyBuffer(&vertexStaging, &mesh.vertexBuffer, mQueue);
			mVulkanDevice->copyBuffer(&indexStaging, &mesh.indexBuffer, mQueue);

			vertexStaging.destroy();
			indexStaging.destroy();

			meshes.push_back(mesh);
		}

		// Binding description
		demoMeshes.bindingDescriptions.resize(1);
		demoMeshes.bindingDescriptions[0] =
			vkTools::vertexInputBindingDescription(
				VERTEX_BUFFER_BIND_ID,
				sizeof(Vertex),
				VK_VERTEX_INPUT_RATE_VERTEX);

		// Attribute descriptions
		// Location 0 : Position
		demoMeshes.attributeDescriptions.resize(4);
		demoMeshes.attributeDescriptions[0] =
			vkTools::vertexInputAttributeDescription(
				VERTEX_BUFFER_BIND_ID,
				0,
				VK_FORMAT_R32G32B32_SFLOAT,
				offsetof(Vertex, pos));
		// Location 1 : Normal
		demoMeshes.attributeDescriptions[1] =
			vkTools::vertexInputAttributeDescription(
				VERTEX_BUFFER_BIND_ID,
				1,
				VK_FORMAT_R32G32B32_SFLOAT,
				offsetof(Vertex, normal));
		// Location 2 : Texture coordinates
		demoMeshes.attributeDescriptions[2] =
			vkTools::vertexInputAttributeDescription(
				VERTEX_BUFFER_BIND_ID,
				2,
				VK_FORMAT_R32G32_SFLOAT,
				offsetof(Vertex, uv));
		// Location 3 : Color
		demoMeshes.attributeDescriptions[3] =
			vkTools::vertexInputAttributeDescription(
				VERTEX_BUFFER_BIND_ID,
				3,
				VK_FORMAT_R32G32B32_SFLOAT,
				offsetof(Vertex, color));

		demoMeshes.inputState = vkTools::pipelineVertexInputStateCreateInfo();
		demoMeshes.inputState.vertexBindingDescriptionCount = demoMeshes.bindingDescriptions.size();
		demoMeshes.inputState.pVertexBindingDescriptions = demoMeshes.bindingDescriptions.data();
		demoMeshes.inputState.vertexAttributeDescriptionCount = demoMeshes.attributeDescriptions.size();
		demoMeshes.inputState.pVertexAttributeDescriptions = demoMeshes.attributeDescriptions.data();
	}

	void setupDescriptorPool()
	{
		// Example uses one ubo and one image sampler
		std::vector<VkDescriptorPoolSize> poolSizes =
		{
			vkTools::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2),
			vkTools::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1)
		};

		VkDescriptorPoolCreateInfo descriptorPoolInfo =
			vkTools::descriptorPoolCreateInfo(
				poolSizes.size(),
				poolSizes.data(),
				2);

		VK_CHECK_RESULT(vkCreateDescriptorPool(mVulkanDevice->mLogicalDevice, &descriptorPoolInfo, nullptr, &descriptorPool));
	}

	void setupDescriptorSetLayout()
	{
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings =
		{
			// Binding 0 : Vertex shader uniform buffer
			vkTools::descriptorSetLayoutBinding(
				VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				VK_SHADER_STAGE_VERTEX_BIT,
				0),
			// Binding 1 : Fragment shader color map image sampler
			vkTools::descriptorSetLayoutBinding(
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT,
				1)
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

		// Cube map image descriptor
		VkDescriptorImageInfo texDescriptorCubeMap =
			vkTools::descriptorImageInfo(
				textures.skybox.sampler,
				textures.skybox.view,
				VK_IMAGE_LAYOUT_GENERAL);

		std::vector<VkWriteDescriptorSet> writeDescriptorSets =
		{
			// Binding 0 : Vertex shader uniform buffer
			vkTools::writeDescriptorSet(
				descriptorSet,
				VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				0,
				&uniformData.meshVS.descriptor),
			// Binding 1 : Fragment shader image sampler
			vkTools::writeDescriptorSet(
				descriptorSet,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				1,
				&texDescriptorCubeMap)
		};

		vkUpdateDescriptorSets(mVulkanDevice->mLogicalDevice, writeDescriptorSets.size(), writeDescriptorSets.data(), 0, NULL);
	}

	void preparePipelines()
	{
		VkPipelineInputAssemblyStateCreateInfo inputAssemblyState =
			vkTools::pipelineInputAssemblyStateCreateInfo(
				VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
				0,
				VK_FALSE);

		VkPipelineRasterizationStateCreateInfo rasterizationState =
			vkTools::pipelineRasterizationStateCreateInfo(
				VK_POLYGON_MODE_FILL,
				VK_CULL_MODE_BACK_BIT,
				VK_FRONT_FACE_CLOCKWISE,
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
			VK_DYNAMIC_STATE_SCISSOR
		};
		VkPipelineDynamicStateCreateInfo dynamicState =
			vkTools::pipelineDynamicStateCreateInfo(
				dynamicStateEnables.data(),
				dynamicStateEnables.size(),
				0);

		// Pipeline for the meshes (armadillo, bunny, etc.)
		// Load shaders
		std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;
		shaderStages[0] = loadShader(getAssetPath() + "shaders/vulkanscene/mesh.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getAssetPath() + "shaders/vulkanscene/mesh.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

		VkGraphicsPipelineCreateInfo pipelineCreateInfo =
			vkTools::pipelineCreateInfo(
				pipelineLayout,
				mRenderPass,
				0);

		pipelineCreateInfo.pVertexInputState = &demoMeshes.inputState;
		pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
		pipelineCreateInfo.pRasterizationState = &rasterizationState;
		pipelineCreateInfo.pColorBlendState = &colorBlendState;
		pipelineCreateInfo.pMultisampleState = &multisampleState;
		pipelineCreateInfo.pViewportState = &viewportState;
		pipelineCreateInfo.pDepthStencilState = &depthStencilState;
		pipelineCreateInfo.pDynamicState = &dynamicState;
		pipelineCreateInfo.stageCount = shaderStages.size();
		pipelineCreateInfo.pStages = shaderStages.data();

		VK_CHECK_RESULT(vkCreateGraphicsPipelines(mVulkanDevice->mLogicalDevice, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.models));

		// Pipeline for the logos
		shaderStages[0] = loadShader(getAssetPath() + "shaders/vulkanscene/logo.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getAssetPath() + "shaders/vulkanscene/logo.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(mVulkanDevice->mLogicalDevice, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.logos));

		// Pipeline for the sky sphere (todo)
		rasterizationState.cullMode = VK_CULL_MODE_FRONT_BIT; // Inverted culling
		depthStencilState.depthWriteEnable = VK_FALSE; // No depth writes
		shaderStages[0] = loadShader(getAssetPath() + "shaders/vulkanscene/skybox.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getAssetPath() + "shaders/vulkanscene/skybox.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(mVulkanDevice->mLogicalDevice, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.skybox));
	}

	// Prepare and initialize uniform buffer containing shader uniforms
	void prepareUniformBuffers()
	{
		mVulkanDevice->createBuffer(
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&uniformData.meshVS,
			sizeof(uboVS));

		updateUniformBuffers();
	}

	void updateUniformBuffers()
	{
		Matrix::createPerspectiveVK(MATH_DEG_TO_RAD(60.0f), (float)width / (float)height, 0.1f, 256.0f, &uboVS.projection);
		Matrix::createLookAt(Vector3(0, 0, -mZoom), cameraPos, Vector3(0, 1, 0), &uboVS.view);
		//uboVS.projection = glm::perspective(glm::radians(60.0f), (float)width / (float)height, 0.1f, 256.0f);
		//uboVS.view = glm::lookAt(
		//	glm::vec3(0, 0, -mZoom),
		//	cameraPos,
		//	glm::vec3(0, 1, 0)
		//);

		//uboVS.model = glm::mat4();
		//uboVS.model = glm::rotate(uboVS.model, glm::radians(mRotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
		//uboVS.model = glm::rotate(uboVS.model, glm::radians(mRotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
		//uboVS.model = glm::rotate(uboVS.model, glm::radians(mRotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
		//uboVS.normal = glm::inverseTranspose(uboVS.view * uboVS.model);

		uboVS.model.setIdentity();
		uboVS.model.rotateX(MATH_DEG_TO_RAD(mRotation.x));
		uboVS.model.rotateY(MATH_DEG_TO_RAD(mRotation.y));
		uboVS.model.rotateZ(MATH_DEG_TO_RAD(mRotation.z));

		uboVS.normal = uboVS.view * uboVS.model;
		uboVS.normal.invert();
		uboVS.normal.transpose();
		

		uboVS.lightPos = lightPos;

		VK_CHECK_RESULT(uniformData.meshVS.map());
		memcpy(uniformData.meshVS.mapped, &uboVS, sizeof(uboVS));
		uniformData.meshVS.unmap();
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
		prepareVertices();
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
		draw();
	}

	virtual void viewChanged()
	{
		updateUniformBuffers();
	}

};
