#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <vector>
#include "define.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <vulkan/vulkan.h>
#include "VulkanBase.h"
#include "vulkanMeshLoader.hpp"

#define ENABLE_VALIDATION false

// 16 bits of depth is enough for such a small scene
#define DEPTH_FORMAT VK_FORMAT_D16_UNORM

// Shadowmap properties
#if defined(__ANDROID__)
#define SHADOWMAP_DIM 1024
#else
#define SHADOWMAP_DIM 2048
#endif
#define SHADOWMAP_FILTER VK_FILTER_LINEAR

// Offscreen frame buffer properties
#define FB_COLOR_FORMAT VK_FORMAT_R8G8B8A8_UNORM



class VkShadowMapping : public VulkanBase
{
	// Vertex layout for this example
	std::vector<vkMeshLoader::VertexLayout> vertexLayout =
	{
		vkMeshLoader::VERTEX_LAYOUT_POSITION,
		vkMeshLoader::VERTEX_LAYOUT_UV,
		vkMeshLoader::VERTEX_LAYOUT_COLOR,
		vkMeshLoader::VERTEX_LAYOUT_NORMAL
	};
public:
	bool displayShadowMap = false;
	bool lightPOV = false;

	// Keep depth range as small as possible
	// for better shadow map precision
	float zNear = 1.0f;
	float zFar = 96.0f;

	// Depth bias (and slope) are used to avoid shadowing artefacts
	// Constant depth bias factor (always applied)
	float depthBiasConstant = 1.25f;
	// Slope depth bias factor, applied depending on polygon's slope
	float depthBiasSlope = 1.75f;

	glm::vec3 lightPos = glm::vec3();
	float lightFOV = 45.0f;

	struct {
		vkMeshLoader::MeshBuffer scene;
		vkMeshLoader::MeshBuffer quad;
	} meshes;

	struct {
		VkPipelineVertexInputStateCreateInfo inputState;
		std::vector<VkVertexInputBindingDescription> bindingDescriptions;
		std::vector<VkVertexInputAttributeDescription> attributeDescriptions;
	} vertices;

	vkTools::UniformData uniformDataVS;

	struct {
		vkTools::UniformData scene;
		vkTools::UniformData offscreen;
	} uniformData;

	struct {
		glm::mat4 projection;
		glm::mat4 model;
	} uboVSquad;

	struct {
		glm::mat4 projection;
		glm::mat4 view;
		glm::mat4 model;
		glm::mat4 depthBiasMVP;
		glm::vec3 lightPos;
	} uboVSscene;

	struct {
		glm::mat4 depthMVP;
	} uboOffscreenVS;

	struct {
		VkPipeline quad;
		VkPipeline offscreen;
		VkPipeline scene;
	} pipelines;

	struct {
		VkPipelineLayout quad;
		VkPipelineLayout offscreen;
	} pipelineLayouts;

	struct {
		VkDescriptorSet offscreen;
		VkDescriptorSet scene;
	} descriptorSets;

	VkDescriptorSet descriptorSet;
	VkDescriptorSetLayout descriptorSetLayout;

	// Framebuffer for offscreen rendering
	struct FrameBufferAttachment {
		VkImage image;
		VkDeviceMemory mem;
		VkImageView view;
	};
	struct OffscreenPass {
		int32_t width, height;
		VkFramebuffer frameBuffer;
		FrameBufferAttachment depth;
		VkRenderPass renderPass;
		VkSampler depthSampler;
		VkDescriptorImageInfo descriptor;
		VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
		// Semaphore used to synchronize between offscreen and final scene render pass
		VkSemaphore semaphore = VK_NULL_HANDLE;
	} offscreenPass;

	VkShadowMapping() : VulkanBase(ENABLE_VALIDATION)
	{
		mZoom = -20.0f;
		mRotation = { -15.0f, -390.0f, 0.0f };
		mEnableTextOverlay = true;
		title = "Vulkan Example - Projected shadow mapping";
		timerSpeed *= 0.5f;
	}

	~VkShadowMapping()
	{
		// Clean up used Vulkan resources 
		// Note : Inherited destructor cleans up resources stored in base class

		// Frame buffer
		vkDestroySampler(mVulkanDevice->mLogicalDevice, offscreenPass.depthSampler, nullptr);

		// Depth attachment
		vkDestroyImageView(mVulkanDevice->mLogicalDevice, offscreenPass.depth.view, nullptr);
		vkDestroyImage(mVulkanDevice->mLogicalDevice, offscreenPass.depth.image, nullptr);
		vkFreeMemory(mVulkanDevice->mLogicalDevice, offscreenPass.depth.mem, nullptr);

		vkDestroyFramebuffer(mVulkanDevice->mLogicalDevice, offscreenPass.frameBuffer, nullptr);

		vkDestroyRenderPass(mVulkanDevice->mLogicalDevice, offscreenPass.renderPass, nullptr);

		vkDestroyPipeline(mVulkanDevice->mLogicalDevice, pipelines.quad, nullptr);
		vkDestroyPipeline(mVulkanDevice->mLogicalDevice, pipelines.offscreen, nullptr);
		vkDestroyPipeline(mVulkanDevice->mLogicalDevice, pipelines.scene, nullptr);

		vkDestroyPipelineLayout(mVulkanDevice->mLogicalDevice, pipelineLayouts.quad, nullptr);
		vkDestroyPipelineLayout(mVulkanDevice->mLogicalDevice, pipelineLayouts.offscreen, nullptr);

		vkDestroyDescriptorSetLayout(mVulkanDevice->mLogicalDevice, descriptorSetLayout, nullptr);

		// Meshes
		vkMeshLoader::freeMeshBufferResources(mVulkanDevice->mLogicalDevice, &meshes.scene);
		vkMeshLoader::freeMeshBufferResources(mVulkanDevice->mLogicalDevice, &meshes.quad);

		// Uniform buffers
		vkTools::destroyUniformData(mVulkanDevice->mLogicalDevice, &uniformDataVS);
		vkTools::destroyUniformData(mVulkanDevice->mLogicalDevice, &uniformData.offscreen);
		vkTools::destroyUniformData(mVulkanDevice->mLogicalDevice, &uniformData.scene);

		vkFreeCommandBuffers(mVulkanDevice->mLogicalDevice, mCmdPool, 1, &offscreenPass.commandBuffer);
		vkDestroySemaphore(mVulkanDevice->mLogicalDevice, offscreenPass.semaphore, nullptr);
	}

	// Set up a separate render pass for the offscreen frame buffer
	// This is necessary as the offscreen frame buffer attachments use formats different to those from the example render pass
	void prepareOffscreenRenderpass()
	{
		VkAttachmentDescription attchmentDescription{};
		attchmentDescription.format = DEPTH_FORMAT;
		attchmentDescription.samples = VK_SAMPLE_COUNT_1_BIT;
		attchmentDescription.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;						// Clear depth at beginning of the render pass
		attchmentDescription.storeOp = VK_ATTACHMENT_STORE_OP_STORE;					// We will read from depth, so it's important to store the depth attachment results
		attchmentDescription.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attchmentDescription.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attchmentDescription.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;					// We don't care about initial layout of the attachment
		attchmentDescription.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;	// Attachment will be transitioned to shader read at render pass end

		VkAttachmentReference depthReference = {};
		depthReference.attachment = 0;
		depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;		// Attachment will be used as depth/stencil during render pass

		VkSubpassDescription subpass = {};
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = 0;												// No color attachments
		subpass.pDepthStencilAttachment = &depthReference;								// Reference to our depth attachment

																						// Use subpass dependencies for layout transitions
		std::array<VkSubpassDependency, 2> dependencies;

		dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[0].dstSubpass = 0;
		dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
		dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
		dependencies[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		dependencies[1].srcSubpass = 0;
		dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
		dependencies[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
		dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		VkRenderPassCreateInfo renderPassCreateInfo = vkTools::renderPassCreateInfo();
		renderPassCreateInfo.attachmentCount = 1;
		renderPassCreateInfo.pAttachments = &attchmentDescription;
		renderPassCreateInfo.subpassCount = 1;
		renderPassCreateInfo.pSubpasses = &subpass;
		renderPassCreateInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
		renderPassCreateInfo.pDependencies = dependencies.data();

		VK_CHECK_RESULT(vkCreateRenderPass(mVulkanDevice->mLogicalDevice, &renderPassCreateInfo, nullptr, &offscreenPass.renderPass));
	}

	// Setup the offscreen framebuffer for rendering the scene from light's point-of-view to
	// The depth attachment of this framebuffer will then be used to sample from in the fragment shader of the shadowing pass
	void prepareOffscreenFramebuffer()
	{
		offscreenPass.width = SHADOWMAP_DIM;
		offscreenPass.height = SHADOWMAP_DIM;

		VkFormat fbColorFormat = FB_COLOR_FORMAT;

		// For shadow mapping we only need a depth attachment
		VkImageCreateInfo image = vkTools::imageCreateInfo();
		image.imageType = VK_IMAGE_TYPE_2D;
		image.extent.width = offscreenPass.width;
		image.extent.height = offscreenPass.height;
		image.extent.depth = 1;
		image.mipLevels = 1;
		image.arrayLayers = 1;
		image.samples = VK_SAMPLE_COUNT_1_BIT;
		image.tiling = VK_IMAGE_TILING_OPTIMAL;
		image.format = DEPTH_FORMAT;																// Depth stencil attachment
		image.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;		// We will sample directly from the depth attachment for the shadow mapping
		VK_CHECK_RESULT(vkCreateImage(mVulkanDevice->mLogicalDevice, &image, nullptr, &offscreenPass.depth.image));

		VkMemoryAllocateInfo memAlloc = vkTools::memoryAllocateInfo();
		VkMemoryRequirements memReqs;
		vkGetImageMemoryRequirements(mVulkanDevice->mLogicalDevice, offscreenPass.depth.image, &memReqs);
		memAlloc.allocationSize = memReqs.size;
		memAlloc.memoryTypeIndex = mVulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		VK_CHECK_RESULT(vkAllocateMemory(mVulkanDevice->mLogicalDevice, &memAlloc, nullptr, &offscreenPass.depth.mem));
		VK_CHECK_RESULT(vkBindImageMemory(mVulkanDevice->mLogicalDevice, offscreenPass.depth.image, offscreenPass.depth.mem, 0));

		VkImageViewCreateInfo depthStencilView = vkTools::imageViewCreateInfo();
		depthStencilView.viewType = VK_IMAGE_VIEW_TYPE_2D;
		depthStencilView.format = DEPTH_FORMAT;
		depthStencilView.subresourceRange = {};
		depthStencilView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		depthStencilView.subresourceRange.baseMipLevel = 0;
		depthStencilView.subresourceRange.levelCount = 1;
		depthStencilView.subresourceRange.baseArrayLayer = 0;
		depthStencilView.subresourceRange.layerCount = 1;
		depthStencilView.image = offscreenPass.depth.image;
		VK_CHECK_RESULT(vkCreateImageView(mVulkanDevice->mLogicalDevice, &depthStencilView, nullptr, &offscreenPass.depth.view));

		// Create sampler to sample from to depth attachment 
		// Used to sample in the fragment shader for shadowed rendering
		VkSamplerCreateInfo sampler = vkTools::samplerCreateInfo();
		sampler.magFilter = SHADOWMAP_FILTER;
		sampler.minFilter = SHADOWMAP_FILTER;
		sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sampler.addressModeV = sampler.addressModeU;
		sampler.addressModeW = sampler.addressModeU;
		sampler.mipLodBias = 0.0f;
		sampler.maxAnisotropy = 0;
		sampler.minLod = 0.0f;
		sampler.maxLod = 1.0f;
		sampler.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
		VK_CHECK_RESULT(vkCreateSampler(mVulkanDevice->mLogicalDevice, &sampler, nullptr, &offscreenPass.depthSampler));

		prepareOffscreenRenderpass();

		// Create frame buffer
		VkFramebufferCreateInfo fbufCreateInfo = vkTools::framebufferCreateInfo();
		fbufCreateInfo.renderPass = offscreenPass.renderPass;
		fbufCreateInfo.attachmentCount = 1;
		fbufCreateInfo.pAttachments = &offscreenPass.depth.view;
		fbufCreateInfo.width = offscreenPass.width;
		fbufCreateInfo.height = offscreenPass.height;
		fbufCreateInfo.layers = 1;

		VK_CHECK_RESULT(vkCreateFramebuffer(mVulkanDevice->mLogicalDevice, &fbufCreateInfo, nullptr, &offscreenPass.frameBuffer));
	}

	void buildOffscreenCommandBuffer()
	{
		if (offscreenPass.commandBuffer == VK_NULL_HANDLE)
		{
			offscreenPass.commandBuffer = VulkanBase::createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, false);
		}
		if (offscreenPass.semaphore == VK_NULL_HANDLE)
		{
			// Create a semaphore used to synchronize offscreen rendering and usage
			VkSemaphoreCreateInfo semaphoreCreateInfo = vkTools::semaphoreCreateInfo();
			VK_CHECK_RESULT(vkCreateSemaphore(mVulkanDevice->mLogicalDevice, &semaphoreCreateInfo, nullptr, &offscreenPass.semaphore));
		}

		VkCommandBufferBeginInfo cmdBufInfo = vkTools::commandBufferBeginInfo();

		VkClearValue clearValues[1];
		clearValues[0].depthStencil = { 1.0f, 0 };

		VkRenderPassBeginInfo renderPassBeginInfo = vkTools::renderPassBeginInfo();
		renderPassBeginInfo.renderPass = offscreenPass.renderPass;
		renderPassBeginInfo.framebuffer = offscreenPass.frameBuffer;
		renderPassBeginInfo.renderArea.offset.x = 0;
		renderPassBeginInfo.renderArea.offset.y = 0;
		renderPassBeginInfo.renderArea.extent.width = offscreenPass.width;
		renderPassBeginInfo.renderArea.extent.height = offscreenPass.height;
		renderPassBeginInfo.clearValueCount = 2;
		renderPassBeginInfo.pClearValues = clearValues;

		VK_CHECK_RESULT(vkBeginCommandBuffer(offscreenPass.commandBuffer, &cmdBufInfo));

		VkViewport viewport = vkTools::viewport((float)offscreenPass.width, (float)offscreenPass.height, 0.0f, 1.0f);
		vkCmdSetViewport(offscreenPass.commandBuffer, 0, 1, &viewport);

		VkRect2D scissor = vkTools::rect2D(offscreenPass.width, offscreenPass.height, 0, 0);
		vkCmdSetScissor(offscreenPass.commandBuffer, 0, 1, &scissor);

		// Set depth bias (aka "Polygon offset")
		// Required to avoid shadow mapping artefacts
		vkCmdSetDepthBias(
			offscreenPass.commandBuffer,
			depthBiasConstant,
			0.0f,
			depthBiasSlope);

		vkCmdBeginRenderPass(offscreenPass.commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

		vkCmdBindPipeline(offscreenPass.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.offscreen);
		vkCmdBindDescriptorSets(offscreenPass.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayouts.offscreen, 0, 1, &descriptorSets.offscreen, 0, NULL);

		VkDeviceSize offsets[1] = { 0 };
		vkCmdBindVertexBuffers(offscreenPass.commandBuffer, VERTEX_BUFFER_BIND_ID, 1, &meshes.scene.vertices.buf, offsets);
		vkCmdBindIndexBuffer(offscreenPass.commandBuffer, meshes.scene.indices.buf, 0, VK_INDEX_TYPE_UINT32);
		vkCmdDrawIndexed(offscreenPass.commandBuffer, meshes.scene.indexCount, 1, 0, 0, 0);

		vkCmdEndRenderPass(offscreenPass.commandBuffer);

		VK_CHECK_RESULT(vkEndCommandBuffer(offscreenPass.commandBuffer));
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
			// Set target frame buffer
			renderPassBeginInfo.framebuffer = mFrameBuffers[i];

			VK_CHECK_RESULT(vkBeginCommandBuffer(mDrawCmdBuffers[i], &cmdBufInfo));

			vkCmdBeginRenderPass(mDrawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

			VkViewport viewport = vkTools::viewport((float)width, (float)height, 0.0f, 1.0f);
			vkCmdSetViewport(mDrawCmdBuffers[i], 0, 1, &viewport);

			VkRect2D scissor = vkTools::rect2D(width, height, 0, 0);
			vkCmdSetScissor(mDrawCmdBuffers[i], 0, 1, &scissor);

			VkDeviceSize offsets[1] = { 0 };

			vkCmdBindDescriptorSets(mDrawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayouts.quad, 0, 1, &descriptorSet, 0, NULL);
			vkCmdBindPipeline(mDrawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.quad);

			// Visualize shadow map
			if (displayShadowMap)
			{
				vkCmdBindVertexBuffers(mDrawCmdBuffers[i], VERTEX_BUFFER_BIND_ID, 1, &meshes.quad.vertices.buf, offsets);
				vkCmdBindIndexBuffer(mDrawCmdBuffers[i], meshes.quad.indices.buf, 0, VK_INDEX_TYPE_UINT32);
				vkCmdDrawIndexed(mDrawCmdBuffers[i], meshes.quad.indexCount, 1, 0, 0, 0);
			}

			// 3D scene
			vkCmdBindDescriptorSets(mDrawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayouts.quad, 0, 1, &descriptorSets.scene, 0, NULL);
			vkCmdBindPipeline(mDrawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.scene);

			vkCmdBindVertexBuffers(mDrawCmdBuffers[i], VERTEX_BUFFER_BIND_ID, 1, &meshes.scene.vertices.buf, offsets);
			vkCmdBindIndexBuffer(mDrawCmdBuffers[i], meshes.scene.indices.buf, 0, VK_INDEX_TYPE_UINT32);
			vkCmdDrawIndexed(mDrawCmdBuffers[i], meshes.scene.indexCount, 1, 0, 0, 0);

			vkCmdEndRenderPass(mDrawCmdBuffers[i]);

			VK_CHECK_RESULT(vkEndCommandBuffer(mDrawCmdBuffers[i]));
		}
	}

	void loadAssets()
	{
		loadMesh(getAssetPath() + "models/vulkanscene_shadow.dae", &meshes.scene, vertexLayout, 4.0f);
	}

	void generateQuad()
	{
		// Setup vertices for a single uv-mapped quad
		struct Vertex {
			float pos[3];
			float uv[2];
			float col[3];
			float normal[3];
		};

#define QUAD_COLOR_NORMAL { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, 1.0f }
		std::vector<Vertex> vertexBuffer =
		{
			{ { 1.0f, 1.0f, 0.0f },{ 1.0f, 1.0f }, QUAD_COLOR_NORMAL },
			{ { 0.0f, 1.0f, 0.0f },{ 0.0f, 1.0f }, QUAD_COLOR_NORMAL },
			{ { 0.0f, 0.0f, 0.0f },{ 0.0f, 0.0f }, QUAD_COLOR_NORMAL },
			{ { 1.0f, 0.0f, 0.0f },{ 1.0f, 0.0f }, QUAD_COLOR_NORMAL }
		};
#undef QUAD_COLOR_NORMAL

		createBuffer(
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
			vertexBuffer.size() * sizeof(Vertex),
			vertexBuffer.data(),
			&meshes.quad.vertices.buf,
			&meshes.quad.vertices.mem);

		// Setup indices
		std::vector<uint32_t> indexBuffer = { 0,1,2, 2,3,0 };
		meshes.quad.indexCount = indexBuffer.size();

		createBuffer(
			VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
			indexBuffer.size() * sizeof(uint32_t),
			indexBuffer.data(),
			&meshes.quad.indices.buf,
			&meshes.quad.indices.mem);
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
		vertices.attributeDescriptions.resize(4);
		// Location 0 : Position
		vertices.attributeDescriptions[0] =
			vkTools::vertexInputAttributeDescription(
				VERTEX_BUFFER_BIND_ID,
				0,
				VK_FORMAT_R32G32B32_SFLOAT,
				0);
		// Location 1 : Texture coordinates
		vertices.attributeDescriptions[1] =
			vkTools::vertexInputAttributeDescription(
				VERTEX_BUFFER_BIND_ID,
				1,
				VK_FORMAT_R32G32_SFLOAT,
				sizeof(float) * 3);
		// Location 2 : Color
		vertices.attributeDescriptions[2] =
			vkTools::vertexInputAttributeDescription(
				VERTEX_BUFFER_BIND_ID,
				2,
				VK_FORMAT_R32G32B32_SFLOAT,
				sizeof(float) * 5);
		// Location 3 : Normal
		vertices.attributeDescriptions[3] =
			vkTools::vertexInputAttributeDescription(
				VERTEX_BUFFER_BIND_ID,
				3,
				VK_FORMAT_R32G32B32_SFLOAT,
				sizeof(float) * 8);

		vertices.inputState = vkTools::pipelineVertexInputStateCreateInfo();
		vertices.inputState.vertexBindingDescriptionCount = vertices.bindingDescriptions.size();
		vertices.inputState.pVertexBindingDescriptions = vertices.bindingDescriptions.data();
		vertices.inputState.vertexAttributeDescriptionCount = vertices.attributeDescriptions.size();
		vertices.inputState.pVertexAttributeDescriptions = vertices.attributeDescriptions.data();
	}

	void setupDescriptorPool()
	{
		// Example uses three ubos and two image samplers
		std::vector<VkDescriptorPoolSize> poolSizes =
		{
			vkTools::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 6),
			vkTools::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4)
		};

		VkDescriptorPoolCreateInfo descriptorPoolInfo =
			vkTools::descriptorPoolCreateInfo(
				poolSizes.size(),
				poolSizes.data(),
				3);

		VK_CHECK_RESULT(vkCreateDescriptorPool(mVulkanDevice->mLogicalDevice, &descriptorPoolInfo, nullptr, &descriptorPool));
	}

	void setupDescriptorSetLayout()
	{
		// Textured quad pipeline layout
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings =
		{
			// Binding 0 : Vertex shader uniform buffer
			vkTools::descriptorSetLayoutBinding(
				VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				VK_SHADER_STAGE_VERTEX_BIT,
				0),
			// Binding 1 : Fragment shader image sampler
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

		VK_CHECK_RESULT(vkCreatePipelineLayout(mVulkanDevice->mLogicalDevice, &pPipelineLayoutCreateInfo, nullptr, &pipelineLayouts.quad));

		// Offscreen pipeline layout
		VK_CHECK_RESULT(vkCreatePipelineLayout(mVulkanDevice->mLogicalDevice, &pPipelineLayoutCreateInfo, nullptr, &pipelineLayouts.offscreen));
	}

	void setupDescriptorSets()
	{
		// Textured quad descriptor set
		VkDescriptorSetAllocateInfo allocInfo =
			vkTools::descriptorSetAllocateInfo(
				descriptorPool,
				&descriptorSetLayout,
				1);

		VK_CHECK_RESULT(vkAllocateDescriptorSets(mVulkanDevice->mLogicalDevice, &allocInfo, &descriptorSet));

		// Image descriptor for the shadow map attachment
		VkDescriptorImageInfo texDescriptor =
			vkTools::descriptorImageInfo(
				offscreenPass.depthSampler,
				offscreenPass.depth.view,
				VK_IMAGE_LAYOUT_GENERAL);

		std::vector<VkWriteDescriptorSet> writeDescriptorSets =
		{
			// Binding 0 : Vertex shader uniform buffer
			vkTools::writeDescriptorSet(
				descriptorSet,
				VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				0,
				&uniformDataVS.descriptor),
			// Binding 1 : Fragment shader texture sampler
			vkTools::writeDescriptorSet(
				descriptorSet,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				1,
				&texDescriptor)
		};

		vkUpdateDescriptorSets(mVulkanDevice->mLogicalDevice, writeDescriptorSets.size(), writeDescriptorSets.data(), 0, NULL);

		// Offscreen
		VK_CHECK_RESULT(vkAllocateDescriptorSets(mVulkanDevice->mLogicalDevice, &allocInfo, &descriptorSets.offscreen));

		std::vector<VkWriteDescriptorSet> offScreenWriteDescriptorSets =
		{
			// Binding 0 : Vertex shader uniform buffer
			vkTools::writeDescriptorSet(
				descriptorSets.offscreen,
				VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				0,
				&uniformData.offscreen.descriptor),
		};
		vkUpdateDescriptorSets(mVulkanDevice->mLogicalDevice, offScreenWriteDescriptorSets.size(), offScreenWriteDescriptorSets.data(), 0, NULL);

		// 3D scene
		VK_CHECK_RESULT(vkAllocateDescriptorSets(mVulkanDevice->mLogicalDevice, &allocInfo, &descriptorSets.scene));

		// Image descriptor for the shadow map attachment
		texDescriptor.sampler = offscreenPass.depthSampler;
		texDescriptor.imageView = offscreenPass.depth.view;

		std::vector<VkWriteDescriptorSet> sceneDescriptorSets =
		{
			// Binding 0 : Vertex shader uniform buffer
			vkTools::writeDescriptorSet(
				descriptorSets.scene,
				VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				0,
				&uniformData.scene.descriptor),
			// Binding 1 : Fragment shader shadow sampler
			vkTools::writeDescriptorSet(
				descriptorSets.scene,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				1,
				&texDescriptor)
		};
		vkUpdateDescriptorSets(mVulkanDevice->mLogicalDevice, sceneDescriptorSets.size(), sceneDescriptorSets.data(), 0, NULL);

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
				VK_CULL_MODE_NONE,
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

		// Solid rendering pipeline
		// Load shaders
		std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

		shaderStages[0] = loadShader(getAssetPath() + "shaders/shadowmapping/quad.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getAssetPath() + "shaders/shadowmapping/quad.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

		VkGraphicsPipelineCreateInfo pipelineCreateInfo =
			vkTools::pipelineCreateInfo(
				pipelineLayouts.quad,
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
		pipelineCreateInfo.stageCount = shaderStages.size();
		pipelineCreateInfo.pStages = shaderStages.data();

		VK_CHECK_RESULT(vkCreateGraphicsPipelines(mVulkanDevice->mLogicalDevice, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.quad));

		// 3D scene
		shaderStages[0] = loadShader(getAssetPath() + "shaders/shadowmapping/scene.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getAssetPath() + "shaders/shadowmapping/scene.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		rasterizationState.cullMode = VK_CULL_MODE_BACK_BIT;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(mVulkanDevice->mLogicalDevice, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.scene));

		// Offscreen pipeline
		shaderStages[0] = loadShader(getAssetPath() + "shaders/shadowmapping/offscreen.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getAssetPath() + "shaders/shadowmapping/offscreen.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		// No blend attachment states (no color attachments used)
		colorBlendState.attachmentCount = 0;
		// Cull front faces
		depthStencilState.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
		// Enable depth bias
		rasterizationState.depthBiasEnable = VK_TRUE;
		// Add depth bias to dynamic state, so we can change it at runtime
		dynamicStateEnables.push_back(VK_DYNAMIC_STATE_DEPTH_BIAS);
		dynamicState =
			vkTools::pipelineDynamicStateCreateInfo(
				dynamicStateEnables.data(),
				dynamicStateEnables.size(),
				0);

		pipelineCreateInfo.layout = pipelineLayouts.offscreen;
		pipelineCreateInfo.renderPass = offscreenPass.renderPass;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(mVulkanDevice->mLogicalDevice, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.offscreen));
	}

	// Prepare and initialize uniform buffer containing shader uniforms
	void prepareUniformBuffers()
	{
		// Debug quad vertex shader uniform buffer block
		createBuffer(
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			sizeof(uboVSscene),
			nullptr,
			&uniformDataVS.buffer,
			&uniformDataVS.memory,
			&uniformDataVS.descriptor);

		// Offscreen vertex shader uniform buffer block
		createBuffer(
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			sizeof(uboOffscreenVS),
			nullptr,
			&uniformData.offscreen.buffer,
			&uniformData.offscreen.memory,
			&uniformData.offscreen.descriptor);

		// Scene vertex shader uniform buffer block 
		createBuffer(
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			sizeof(uboVSscene),
			nullptr,
			&uniformData.scene.buffer,
			&uniformData.scene.memory,
			&uniformData.scene.descriptor);

		updateLight();
		updateUniformBufferOffscreen();
		updateUniformBuffers();
	}

	void updateLight()
	{
		// Animate the light source
		lightPos.x = cos(glm::radians(timer * 360.0f)) * 40.0f;
		lightPos.y = -50.0f + sin(glm::radians(timer * 360.0f)) * 20.0f;
		lightPos.z = 25.0f + sin(glm::radians(timer * 360.0f)) * 5.0f;
	}

	void updateUniformBuffers()
	{
		// Shadow map debug quad
		float AR = (float)height / (float)width;

		uboVSquad.projection = glm::ortho(2.5f / AR, 0.0f, 0.0f, 2.5f, -1.0f, 1.0f);
		uboVSquad.model = glm::mat4();

		uint8_t *pData;
		VK_CHECK_RESULT(vkMapMemory(mVulkanDevice->mLogicalDevice, uniformDataVS.memory, 0, sizeof(uboVSquad), 0, (void **)&pData));
		memcpy(pData, &uboVSquad, sizeof(uboVSquad));
		vkUnmapMemory(mVulkanDevice->mLogicalDevice, uniformDataVS.memory);

		// 3D scene
		uboVSscene.projection = glm::perspective(glm::radians(45.0f), (float)width / (float)height, zNear, zFar);

		uboVSscene.view = glm::translate(glm::mat4(), glm::vec3(0.0f, 0.0f, mZoom));
		uboVSscene.view = glm::rotate(uboVSscene.view, glm::radians(mRotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
		uboVSscene.view = glm::rotate(uboVSscene.view, glm::radians(mRotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
		uboVSscene.view = glm::rotate(uboVSscene.view, glm::radians(mRotation.z), glm::vec3(0.0f, 0.0f, 1.0f));

		uboVSscene.model = glm::mat4();

		uboVSscene.lightPos = lightPos;

		// Render scene from light's point of view
		if (lightPOV)
		{
			uboVSscene.projection = glm::perspective(glm::radians(lightFOV), (float)width / (float)height, zNear, zFar);
			uboVSscene.view = glm::lookAt(lightPos, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		}

		uboVSscene.depthBiasMVP = uboOffscreenVS.depthMVP;

		VK_CHECK_RESULT(vkMapMemory(mVulkanDevice->mLogicalDevice, uniformData.scene.memory, 0, sizeof(uboVSscene), 0, (void **)&pData));
		memcpy(pData, &uboVSscene, sizeof(uboVSscene));
		vkUnmapMemory(mVulkanDevice->mLogicalDevice, uniformData.scene.memory);
	}

	void updateUniformBufferOffscreen()
	{
		// Matrix from light's point of view
		glm::mat4 depthProjectionMatrix = glm::perspective(glm::radians(lightFOV), 1.0f, zNear, zFar);
		glm::mat4 depthViewMatrix = glm::lookAt(lightPos, glm::vec3(0.0f), glm::vec3(0, 1, 0));
		glm::mat4 depthModelMatrix = glm::mat4();

		uboOffscreenVS.depthMVP = depthProjectionMatrix * depthViewMatrix * depthModelMatrix;

		uint8_t *pData;
		VK_CHECK_RESULT(vkMapMemory(mVulkanDevice->mLogicalDevice, uniformData.offscreen.memory, 0, sizeof(uboOffscreenVS), 0, (void **)&pData));
		memcpy(pData, &uboOffscreenVS, sizeof(uboOffscreenVS));
		vkUnmapMemory(mVulkanDevice->mLogicalDevice, uniformData.offscreen.memory);
	}

	void draw()
	{
		VulkanBase::prepareFrame();

		// The scene render command buffer has to wait for the offscreen rendering (and transfer) to be finished before using the shadow map
		// Therefore we synchronize using an additional semaphore

		// Offscreen rendering

		// Wait for swap chain presentation to finish
		mSubmitInfo.pWaitSemaphores = &semaphores.presentComplete;
		// Signal ready with offscreen semaphore
		mSubmitInfo.pSignalSemaphores = &offscreenPass.semaphore;

		// Submit work
		mSubmitInfo.commandBufferCount = 1;
		mSubmitInfo.pCommandBuffers = &offscreenPass.commandBuffer;
		VK_CHECK_RESULT(vkQueueSubmit(mQueue, 1, &mSubmitInfo, VK_NULL_HANDLE));

		// Scene rendering

		// Wait for offscreen semaphore
		mSubmitInfo.pWaitSemaphores = &offscreenPass.semaphore;;
		// Signal ready with render complete semaphpre
		mSubmitInfo.pSignalSemaphores = &semaphores.renderComplete;

		// Submit work
		mSubmitInfo.pCommandBuffers = &mDrawCmdBuffers[gSwapChain.mCurrentBuffer];
		VK_CHECK_RESULT(vkQueueSubmit(mQueue, 1, &mSubmitInfo, VK_NULL_HANDLE));

		VulkanBase::submitFrame();
	}

	void prepare()
	{
		VulkanBase::prepare();
		loadAssets();
		generateQuad();
		prepareOffscreenFramebuffer();
		setupVertexDescriptions();
		prepareUniformBuffers();
		setupDescriptorSetLayout();
		preparePipelines();
		setupDescriptorPool();
		setupDescriptorSets();
		buildCommandBuffers();
		buildOffscreenCommandBuffer();
		prepared = true;
	}

	virtual void render()
	{
		if (!prepared)
			return;
		draw();
		if (!paused)
		{
			updateLight();
			updateUniformBufferOffscreen();
			updateUniformBuffers();
		}
	}

	virtual void viewChanged()
	{
		updateUniformBufferOffscreen();
		updateUniformBuffers();
	}

	void toggleShadowMapDisplay()
	{
		displayShadowMap = !displayShadowMap;
		buildCommandBuffers();
	}

	void toogleLightPOV()
	{
		lightPOV = !lightPOV;
		viewChanged();
	}

	virtual void keyPressed(uint32_t keyCode)
	{
		switch (keyCode)
		{
		case Keyboard::KEY_S:
		case GAMEPAD_BUTTON_A:
			toggleShadowMapDisplay();
			break;
		case Keyboard::KEY_L:
		case GAMEPAD_BUTTON_X:
			toogleLightPOV();
			break;
		}
	}

	virtual void getOverlayText(VulkanTextOverlay *textOverlay)
	{
#if defined(__ANDROID__)
		textOverlay->addText("Press \"Button A\" to toggle shadow map", 5.0f, 85.0f, VulkanTextOverlay::alignLeft);
		textOverlay->addText("Press \"Button X\" to toggle light's pov", 5.0f, 100.0f, VulkanTextOverlay::alignLeft);
#else
		textOverlay->addText("Press \"s\" to toggle shadow map", 5.0f, 85.0f, VulkanTextOverlay::alignLeft);
		textOverlay->addText("Press \"l\" to toggle light's pov", 5.0f, 100.0f, VulkanTextOverlay::alignLeft);
#endif
	}

};

