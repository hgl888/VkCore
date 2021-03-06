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

// Offscreen frame buffer properties
#define FB_DIM 256
#define FB_COLOR_FORMAT VK_FORMAT_R8G8B8A8_UNORM


class VkBloom : public VulkanBase
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
	bool bloom = true;

	struct {
		vkTools::VulkanTexture cubemap;
	} textures;

	struct {
		vkMeshLoader::MeshBuffer ufo;
		vkMeshLoader::MeshBuffer ufoGlow;
		vkMeshLoader::MeshBuffer skyBox;
		vkMeshLoader::MeshBuffer quad;
	} meshes;

	struct {
		VkPipelineVertexInputStateCreateInfo inputState;
		std::vector<VkVertexInputBindingDescription> bindingDescriptions;
		std::vector<VkVertexInputAttributeDescription> attributeDescriptions;
	} vertices;

	struct {
		vkTools::UniformData vsScene;
		vkTools::UniformData vsFullScreen;
		vkTools::UniformData vsSkyBox;
		vkTools::UniformData fsVertBlur;
		vkTools::UniformData fsHorzBlur;
	} uniformData;

	struct UBO {
		Matrix projection;
		Matrix model;
	};

	struct UBOBlur {
		float blurScale = 1.0f;
		float blurStrength = 1.5f;
		uint32_t horizontal;
	};

	struct {
		UBO scene, fullscreen, skyBox;
		UBOBlur vertBlur, horzBlur;
	} ubos;

	struct {
		VkPipeline blurVert;
		VkPipeline blurHorz;
		VkPipeline glowPass;
		VkPipeline phongPass;
		VkPipeline skyBox;
	} pipelines;

	// Pipeline layout is shared amongst all descriptor sets
	VkPipelineLayout pipelineLayout;

	struct {
		VkDescriptorSet scene;
		VkDescriptorSet verticalBlur;
		VkDescriptorSet horizontalBlur;
		VkDescriptorSet skyBox;
	} descriptorSets;

	// Descriptor set layout is shared amongst all descriptor sets
	VkDescriptorSetLayout descriptorSetLayout;

	// Framebuffer for offscreen rendering
	struct FrameBufferAttachment {
		VkImage image;
		VkDeviceMemory mem;
		VkImageView view;
	};
	struct FrameBuffer {
		VkFramebuffer framebuffer;
		FrameBufferAttachment color, depth;
		VkDescriptorImageInfo descriptor;
	};
	struct OffscreenPass {
		int32_t width, height;
		VkRenderPass renderPass;
		VkSampler sampler;
		VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
		// Semaphore used to synchronize between offscreen and final scene rendering
		VkSemaphore semaphore = VK_NULL_HANDLE;
		std::array<FrameBuffer, 2> framebuffers;
	} offscreenPass;

	VkBloom() : VulkanBase(ENABLE_VALIDATION)
	{
		mZoom = -10.25f;
		mRotation = { 7.5f, -343.0f, 0.0f };
		timerSpeed *= 0.5f;
		mEnableTextOverlay = true;
		title = "Vulkan Example - Bloom";
	}

	~VkBloom()
	{
		// Clean up used Vulkan resources 
		// Note : Inherited destructor cleans up resources stored in base class

		vkDestroySampler(mVulkanDevice->mLogicalDevice, offscreenPass.sampler, nullptr);

		// Frame buffer
		for (auto& framebuffer : offscreenPass.framebuffers)
		{
			// Attachments
			vkDestroyImageView(mVulkanDevice->mLogicalDevice, framebuffer.color.view, nullptr);
			vkDestroyImage(mVulkanDevice->mLogicalDevice, framebuffer.color.image, nullptr);
			vkFreeMemory(mVulkanDevice->mLogicalDevice, framebuffer.color.mem, nullptr);
			vkDestroyImageView(mVulkanDevice->mLogicalDevice, framebuffer.depth.view, nullptr);
			vkDestroyImage(mVulkanDevice->mLogicalDevice, framebuffer.depth.image, nullptr);
			vkFreeMemory(mVulkanDevice->mLogicalDevice, framebuffer.depth.mem, nullptr);

			vkDestroyFramebuffer(mVulkanDevice->mLogicalDevice, framebuffer.framebuffer, nullptr);
		}
		vkDestroyRenderPass(mVulkanDevice->mLogicalDevice, offscreenPass.renderPass, nullptr);
		vkFreeCommandBuffers(mVulkanDevice->mLogicalDevice, mCmdPool, 1, &offscreenPass.commandBuffer);
		vkDestroySemaphore(mVulkanDevice->mLogicalDevice, offscreenPass.semaphore, nullptr);

		vkDestroyPipeline(mVulkanDevice->mLogicalDevice, pipelines.blurHorz, nullptr);
		vkDestroyPipeline(mVulkanDevice->mLogicalDevice, pipelines.blurVert, nullptr);
		vkDestroyPipeline(mVulkanDevice->mLogicalDevice, pipelines.phongPass, nullptr);
		vkDestroyPipeline(mVulkanDevice->mLogicalDevice, pipelines.glowPass, nullptr);
		vkDestroyPipeline(mVulkanDevice->mLogicalDevice, pipelines.skyBox, nullptr);

		vkDestroyPipelineLayout(mVulkanDevice->mLogicalDevice, pipelineLayout, nullptr);

		vkDestroyDescriptorSetLayout(mVulkanDevice->mLogicalDevice, descriptorSetLayout, nullptr);

		// Meshes
		vkMeshLoader::freeMeshBufferResources(mVulkanDevice->mLogicalDevice, &meshes.ufo);
		vkMeshLoader::freeMeshBufferResources(mVulkanDevice->mLogicalDevice, &meshes.ufoGlow);
		vkMeshLoader::freeMeshBufferResources(mVulkanDevice->mLogicalDevice, &meshes.skyBox);
		vkMeshLoader::freeMeshBufferResources(mVulkanDevice->mLogicalDevice, &meshes.quad);

		// Uniform buffers
		vkTools::destroyUniformData(mVulkanDevice->mLogicalDevice, &uniformData.vsScene);
		vkTools::destroyUniformData(mVulkanDevice->mLogicalDevice, &uniformData.vsFullScreen);
		vkTools::destroyUniformData(mVulkanDevice->mLogicalDevice, &uniformData.vsSkyBox);
		vkTools::destroyUniformData(mVulkanDevice->mLogicalDevice, &uniformData.fsVertBlur);
		vkTools::destroyUniformData(mVulkanDevice->mLogicalDevice, &uniformData.fsHorzBlur);

		textureLoader->destroyTexture(textures.cubemap);
	}

	// Setup the offscreen framebuffer for rendering the mirrored scene
	// The color attachment of this framebuffer will then be sampled from
	void prepareOffscreenFramebuffer(FrameBuffer *frameBuf, VkFormat colorFormat, VkFormat depthFormat)
	{
		// Color attachment
		VkImageCreateInfo image = vkTools::imageCreateInfo();
		image.imageType = VK_IMAGE_TYPE_2D;
		image.format = colorFormat;
		image.extent.width = FB_DIM;
		image.extent.height = FB_DIM;
		image.extent.depth = 1;
		image.mipLevels = 1;
		image.arrayLayers = 1;
		image.samples = VK_SAMPLE_COUNT_1_BIT;
		image.tiling = VK_IMAGE_TILING_OPTIMAL;
		// We will sample directly from the color attachment
		image.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

		VkMemoryAllocateInfo memAlloc = vkTools::memoryAllocateInfo();
		VkMemoryRequirements memReqs;

		VkImageViewCreateInfo colorImageView = vkTools::imageViewCreateInfo();
		colorImageView.viewType = VK_IMAGE_VIEW_TYPE_2D;
		colorImageView.format = colorFormat;
		colorImageView.flags = 0;
		colorImageView.subresourceRange = {};
		colorImageView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		colorImageView.subresourceRange.baseMipLevel = 0;
		colorImageView.subresourceRange.levelCount = 1;
		colorImageView.subresourceRange.baseArrayLayer = 0;
		colorImageView.subresourceRange.layerCount = 1;

		VK_CHECK_RESULT(vkCreateImage(mVulkanDevice->mLogicalDevice, &image, nullptr, &frameBuf->color.image));
		vkGetImageMemoryRequirements(mVulkanDevice->mLogicalDevice, frameBuf->color.image, &memReqs);
		memAlloc.allocationSize = memReqs.size;
		memAlloc.memoryTypeIndex = mVulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		VK_CHECK_RESULT(vkAllocateMemory(mVulkanDevice->mLogicalDevice, &memAlloc, nullptr, &frameBuf->color.mem));
		VK_CHECK_RESULT(vkBindImageMemory(mVulkanDevice->mLogicalDevice, frameBuf->color.image, frameBuf->color.mem, 0));

		colorImageView.image = frameBuf->color.image;
		VK_CHECK_RESULT(vkCreateImageView(mVulkanDevice->mLogicalDevice, &colorImageView, nullptr, &frameBuf->color.view));

		// Depth stencil attachment
		image.format = depthFormat;
		image.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

		VkImageViewCreateInfo depthStencilView = vkTools::imageViewCreateInfo();
		depthStencilView.viewType = VK_IMAGE_VIEW_TYPE_2D;
		depthStencilView.format = depthFormat;
		depthStencilView.flags = 0;
		depthStencilView.subresourceRange = {};
		depthStencilView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
		depthStencilView.subresourceRange.baseMipLevel = 0;
		depthStencilView.subresourceRange.levelCount = 1;
		depthStencilView.subresourceRange.baseArrayLayer = 0;
		depthStencilView.subresourceRange.layerCount = 1;

		VK_CHECK_RESULT(vkCreateImage(mVulkanDevice->mLogicalDevice, &image, nullptr, &frameBuf->depth.image));
		vkGetImageMemoryRequirements(mVulkanDevice->mLogicalDevice, frameBuf->depth.image, &memReqs);
		memAlloc.allocationSize = memReqs.size;
		memAlloc.memoryTypeIndex = mVulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		VK_CHECK_RESULT(vkAllocateMemory(mVulkanDevice->mLogicalDevice, &memAlloc, nullptr, &frameBuf->depth.mem));
		VK_CHECK_RESULT(vkBindImageMemory(mVulkanDevice->mLogicalDevice, frameBuf->depth.image, frameBuf->depth.mem, 0));

		depthStencilView.image = frameBuf->depth.image;
		VK_CHECK_RESULT(vkCreateImageView(mVulkanDevice->mLogicalDevice, &depthStencilView, nullptr, &frameBuf->depth.view));

		VkImageView attachments[2];
		attachments[0] = frameBuf->color.view;
		attachments[1] = frameBuf->depth.view;

		VkFramebufferCreateInfo fbufCreateInfo = vkTools::framebufferCreateInfo();
		fbufCreateInfo.renderPass = offscreenPass.renderPass;
		fbufCreateInfo.attachmentCount = 2;
		fbufCreateInfo.pAttachments = attachments;
		fbufCreateInfo.width = FB_DIM;
		fbufCreateInfo.height = FB_DIM;
		fbufCreateInfo.layers = 1;

		VK_CHECK_RESULT(vkCreateFramebuffer(mVulkanDevice->mLogicalDevice, &fbufCreateInfo, nullptr, &frameBuf->framebuffer));

		// Fill a descriptor for later use in a descriptor set 
		frameBuf->descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		frameBuf->descriptor.imageView = frameBuf->color.view;
		frameBuf->descriptor.sampler = offscreenPass.sampler;
	}

	// Prepare the offscreen framebuffers used for the vertical- and horizontal blur 
	void prepareOffscreen()
	{
		offscreenPass.width = FB_DIM;
		offscreenPass.height = FB_DIM;

		// Find a suitable depth format
		VkFormat fbDepthFormat;
		VkBool32 validDepthFormat = vkTools::getSupportedDepthFormat(mVulkanDevice->mPhysicalDevice, &fbDepthFormat);
		assert(validDepthFormat);

		// Create a separate render pass for the offscreen rendering as it may differ from the one used for scene rendering

		std::array<VkAttachmentDescription, 2> attchmentDescriptions = {};
		// Color attachment
		attchmentDescriptions[0].format = FB_COLOR_FORMAT;
		attchmentDescriptions[0].samples = VK_SAMPLE_COUNT_1_BIT;
		attchmentDescriptions[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attchmentDescriptions[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attchmentDescriptions[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attchmentDescriptions[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attchmentDescriptions[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attchmentDescriptions[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		// Depth attachment
		attchmentDescriptions[1].format = fbDepthFormat;
		attchmentDescriptions[1].samples = VK_SAMPLE_COUNT_1_BIT;
		attchmentDescriptions[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attchmentDescriptions[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attchmentDescriptions[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attchmentDescriptions[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attchmentDescriptions[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attchmentDescriptions[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkAttachmentReference colorReference = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
		VkAttachmentReference depthReference = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

		VkSubpassDescription subpassDescription = {};
		subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpassDescription.colorAttachmentCount = 1;
		subpassDescription.pColorAttachments = &colorReference;
		subpassDescription.pDepthStencilAttachment = &depthReference;

		// Use subpass dependencies for layout transitions
		std::array<VkSubpassDependency, 2> dependencies;

		dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[0].dstSubpass = 0;
		dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
		dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
		dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		dependencies[1].srcSubpass = 0;
		dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
		dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
		dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		// Create the actual renderpass
		VkRenderPassCreateInfo renderPassInfo = {};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		renderPassInfo.attachmentCount = static_cast<uint32_t>(attchmentDescriptions.size());
		renderPassInfo.pAttachments = attchmentDescriptions.data();
		renderPassInfo.subpassCount = 1;
		renderPassInfo.pSubpasses = &subpassDescription;
		renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
		renderPassInfo.pDependencies = dependencies.data();

		VK_CHECK_RESULT(vkCreateRenderPass(mVulkanDevice->mLogicalDevice, &renderPassInfo, nullptr, &offscreenPass.renderPass));

		// Create sampler to sample from the color attachments
		VkSamplerCreateInfo sampler = vkTools::samplerCreateInfo();
		sampler.magFilter = VK_FILTER_LINEAR;
		sampler.minFilter = VK_FILTER_LINEAR;
		sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sampler.addressModeV = sampler.addressModeU;
		sampler.addressModeW = sampler.addressModeU;
		sampler.mipLodBias = 0.0f;
		sampler.maxAnisotropy = 0;
		sampler.minLod = 0.0f;
		sampler.maxLod = 1.0f;
		sampler.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
		VK_CHECK_RESULT(vkCreateSampler(mVulkanDevice->mLogicalDevice, &sampler, nullptr, &offscreenPass.sampler));

		// Create two frame buffers
		prepareOffscreenFramebuffer(&offscreenPass.framebuffers[0], FB_COLOR_FORMAT, fbDepthFormat);
		prepareOffscreenFramebuffer(&offscreenPass.framebuffers[1], FB_COLOR_FORMAT, fbDepthFormat);
	}

	// Sets up the command buffer that renders the scene to the offscreen frame buffer
	// The blur method used in this example is multi pass and renders the vertical
	// blur first and then the horizontal one.
	// While it's possible to blur in one pass, this method is widely used as it
	// requires far less samples to generate the blur
	void buildOffscreenCommandBuffer()
	{
		if (offscreenPass.commandBuffer == VK_NULL_HANDLE)
		{
			offscreenPass.commandBuffer = VulkanBase::createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, false);
		}

		if (offscreenPass.semaphore == VK_NULL_HANDLE)
		{
			VkSemaphoreCreateInfo semaphoreCreateInfo = vkTools::semaphoreCreateInfo();
			VK_CHECK_RESULT(vkCreateSemaphore(mVulkanDevice->mLogicalDevice, &semaphoreCreateInfo, nullptr, &offscreenPass.semaphore));
		}

		VkCommandBufferBeginInfo cmdBufInfo = vkTools::commandBufferBeginInfo();

		// First pass: Render glow parts of the model (separate mesh)
		// -------------------------------------------------------------------------------------------------------

		VkClearValue clearValues[2];
		clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
		clearValues[1].depthStencil = { 1.0f, 0 };

		VkRenderPassBeginInfo renderPassBeginInfo = vkTools::renderPassBeginInfo();
		renderPassBeginInfo.renderPass = offscreenPass.renderPass;
		renderPassBeginInfo.framebuffer = offscreenPass.framebuffers[0].framebuffer;
		renderPassBeginInfo.renderArea.extent.width = offscreenPass.width;
		renderPassBeginInfo.renderArea.extent.height = offscreenPass.height;
		renderPassBeginInfo.clearValueCount = 2;
		renderPassBeginInfo.pClearValues = clearValues;

		VK_CHECK_RESULT(vkBeginCommandBuffer(offscreenPass.commandBuffer, &cmdBufInfo));

		VkViewport viewport = vkTools::viewport((float)offscreenPass.width, (float)offscreenPass.height, 0.0f, 1.0f);
		vkCmdSetViewport(offscreenPass.commandBuffer, 0, 1, &viewport);

		VkRect2D scissor = vkTools::rect2D(offscreenPass.width, offscreenPass.height, 0, 0);
		vkCmdSetScissor(offscreenPass.commandBuffer, 0, 1, &scissor);

		vkCmdBeginRenderPass(offscreenPass.commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

		vkCmdBindDescriptorSets(offscreenPass.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets.scene, 0, NULL);
		vkCmdBindPipeline(offscreenPass.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.glowPass);

		VkDeviceSize offsets[1] = { 0 };
		vkCmdBindVertexBuffers(offscreenPass.commandBuffer, VERTEX_BUFFER_BIND_ID, 1, &meshes.ufoGlow.vertices.buf, offsets);
		vkCmdBindIndexBuffer(offscreenPass.commandBuffer, meshes.ufoGlow.indices.buf, 0, VK_INDEX_TYPE_UINT32);
		vkCmdDrawIndexed(offscreenPass.commandBuffer, meshes.ufoGlow.indexCount, 1, 0, 0, 0);

		vkCmdEndRenderPass(offscreenPass.commandBuffer);

		// Second pass: Render contents of the first pass into second framebuffer and apply a vertical blur
		// This is the first blur pass, the horizontal blur is applied when rendering on top of the scene
		// -------------------------------------------------------------------------------------------------------

		renderPassBeginInfo.framebuffer = offscreenPass.framebuffers[1].framebuffer;

		vkCmdBeginRenderPass(offscreenPass.commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

		// Draw horizontally blurred texture 
		vkCmdBindDescriptorSets(offscreenPass.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets.verticalBlur, 0, NULL);
		vkCmdBindPipeline(offscreenPass.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.blurVert);
		vkCmdBindVertexBuffers(offscreenPass.commandBuffer, VERTEX_BUFFER_BIND_ID, 1, &meshes.quad.vertices.buf, offsets);
		vkCmdBindIndexBuffer(offscreenPass.commandBuffer, meshes.quad.indices.buf, 0, VK_INDEX_TYPE_UINT32);
		vkCmdDrawIndexed(offscreenPass.commandBuffer, meshes.quad.indexCount, 1, 0, 0, 0);

		vkCmdEndRenderPass(offscreenPass.commandBuffer);

		VK_CHECK_RESULT(vkEndCommandBuffer(offscreenPass.commandBuffer));
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

			// Skybox 
			vkCmdBindDescriptorSets(mDrawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets.skyBox, 0, NULL);
			vkCmdBindPipeline(mDrawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.skyBox);

			vkCmdBindVertexBuffers(mDrawCmdBuffers[i], VERTEX_BUFFER_BIND_ID, 1, &meshes.skyBox.vertices.buf, offsets);
			vkCmdBindIndexBuffer(mDrawCmdBuffers[i], meshes.skyBox.indices.buf, 0, VK_INDEX_TYPE_UINT32);
			vkCmdDrawIndexed(mDrawCmdBuffers[i], meshes.skyBox.indexCount, 1, 0, 0, 0);

			// 3D scene
			vkCmdBindDescriptorSets(mDrawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets.scene, 0, NULL);
			vkCmdBindPipeline(mDrawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.phongPass);

			vkCmdBindVertexBuffers(mDrawCmdBuffers[i], VERTEX_BUFFER_BIND_ID, 1, &meshes.ufo.vertices.buf, offsets);
			vkCmdBindIndexBuffer(mDrawCmdBuffers[i], meshes.ufo.indices.buf, 0, VK_INDEX_TYPE_UINT32);
			vkCmdDrawIndexed(mDrawCmdBuffers[i], meshes.ufo.indexCount, 1, 0, 0, 0);

			// Render vertical blurred scene applying a horizontal blur
			// Render the (vertically blurred) contents of the second framebuffer and apply a horizontal blur
			// -------------------------------------------------------------------------------------------------------
			if (bloom)
			{
				vkCmdBindDescriptorSets(mDrawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets.horizontalBlur, 0, NULL);
				vkCmdBindPipeline(mDrawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.blurHorz);
				vkCmdBindVertexBuffers(mDrawCmdBuffers[i], VERTEX_BUFFER_BIND_ID, 1, &meshes.quad.vertices.buf, offsets);
				vkCmdBindIndexBuffer(mDrawCmdBuffers[i], meshes.quad.indices.buf, 0, VK_INDEX_TYPE_UINT32);
				vkCmdDrawIndexed(mDrawCmdBuffers[i], meshes.quad.indexCount, 1, 0, 0, 0);
			}

			vkCmdEndRenderPass(mDrawCmdBuffers[i]);

			VK_CHECK_RESULT(vkEndCommandBuffer(mDrawCmdBuffers[i]));
		}

		if (bloom)
		{
			buildOffscreenCommandBuffer();
		}
	}

	void loadAssets()
	{
		loadMesh(getAssetPath() + "models/retroufo.dae", &meshes.ufo, vertexLayout, 0.05f);
		loadMesh(getAssetPath() + "models/retroufo_glow.dae", &meshes.ufoGlow, vertexLayout, 0.05f);
		loadMesh(getAssetPath() + "models/cube.obj", &meshes.skyBox, vertexLayout, 1.0f);
		textureLoader->loadCubemap(getAssetPath() + "textures/cubemap_space.ktx", VK_FORMAT_R8G8B8A8_UNORM, &textures.cubemap);
	}

	// Setup vertices for a single uv-mapped quad
	void generateQuad()
	{
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
		// Same for all meshes used in this example
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
		std::vector<VkDescriptorPoolSize> poolSizes =
		{
			vkTools::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 8),
			vkTools::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 6)
		};

		VkDescriptorPoolCreateInfo descriptorPoolInfo =
			vkTools::descriptorPoolCreateInfo(
				poolSizes.size(),
				poolSizes.data(),
				5);

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
				1),
			// Binding 2 : Framgnet shader image sampler
			vkTools::descriptorSetLayoutBinding(
				VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
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

		std::vector<VkWriteDescriptorSet> writeDescriptorSets;

		// Full screen blur descriptor sets

		// Vertical blur
		VK_CHECK_RESULT(vkAllocateDescriptorSets(mVulkanDevice->mLogicalDevice, &allocInfo, &descriptorSets.verticalBlur));
		writeDescriptorSets =
		{
			// Binding 0: Vertex shader uniform buffer
			vkTools::writeDescriptorSet(descriptorSets.verticalBlur, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &uniformData.vsScene.descriptor),
			// Binding 1: Fragment shader texture sampler
			vkTools::writeDescriptorSet(descriptorSets.verticalBlur, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &offscreenPass.framebuffers[0].descriptor),
			// Binding 2: Fragment shader uniform buffer
			vkTools::writeDescriptorSet(descriptorSets.verticalBlur, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2, &uniformData.fsVertBlur.descriptor)
		};
		vkUpdateDescriptorSets(mVulkanDevice->mLogicalDevice, writeDescriptorSets.size(), writeDescriptorSets.data(), 0, NULL);

		// Horizontal blur
		VK_CHECK_RESULT(vkAllocateDescriptorSets(mVulkanDevice->mLogicalDevice, &allocInfo, &descriptorSets.horizontalBlur));
		writeDescriptorSets =
		{
			// Binding 0: Vertex shader uniform buffer
			vkTools::writeDescriptorSet(descriptorSets.horizontalBlur, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &uniformData.vsScene.descriptor),
			// Binding 1: Fragment shader texture sampler
			vkTools::writeDescriptorSet(descriptorSets.horizontalBlur, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,	1, &offscreenPass.framebuffers[1].descriptor),
			// Binding 2: Fragment shader uniform buffer
			vkTools::writeDescriptorSet(descriptorSets.horizontalBlur, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,	2, &uniformData.fsHorzBlur.descriptor)
		};
		vkUpdateDescriptorSets(mVulkanDevice->mLogicalDevice, writeDescriptorSets.size(), writeDescriptorSets.data(), 0, NULL);

		// 3D scene
		VK_CHECK_RESULT(vkAllocateDescriptorSets(mVulkanDevice->mLogicalDevice, &allocInfo, &descriptorSets.scene));
		writeDescriptorSets =
		{
			// Binding 0: Vertex shader uniform buffer
			vkTools::writeDescriptorSet(descriptorSets.scene, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &uniformData.vsFullScreen.descriptor)
		};
		vkUpdateDescriptorSets(mVulkanDevice->mLogicalDevice, writeDescriptorSets.size(), writeDescriptorSets.data(), 0, NULL);

		// Skybox
		VK_CHECK_RESULT(vkAllocateDescriptorSets(mVulkanDevice->mLogicalDevice, &allocInfo, &descriptorSets.skyBox));
		writeDescriptorSets =
		{
			// Binding 0: Vertex shader uniform buffer
			vkTools::writeDescriptorSet(descriptorSets.skyBox, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &uniformData.vsSkyBox.descriptor),
			// Binding 1: Fragment shader texture sampler
			vkTools::writeDescriptorSet(descriptorSets.skyBox, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,	1, &textures.cubemap.descriptor),
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

		std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

		// Vertical gauss blur
		// Load shaders
		shaderStages[0] = loadShader(getAssetPath() + "shaders/bloom/gaussblur.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getAssetPath() + "shaders/bloom/gaussblur.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

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
		pipelineCreateInfo.stageCount = shaderStages.size();
		pipelineCreateInfo.pStages = shaderStages.data();

		// Additive blending
		blendAttachmentState.colorWriteMask = 0xF;
		blendAttachmentState.blendEnable = VK_TRUE;
		blendAttachmentState.colorBlendOp = VK_BLEND_OP_ADD;
		blendAttachmentState.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
		blendAttachmentState.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
		blendAttachmentState.alphaBlendOp = VK_BLEND_OP_ADD;
		blendAttachmentState.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		blendAttachmentState.dstAlphaBlendFactor = VK_BLEND_FACTOR_DST_ALPHA;

		pipelineCreateInfo.renderPass = offscreenPass.renderPass;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(mVulkanDevice->mLogicalDevice, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.blurVert));
		pipelineCreateInfo.renderPass = mRenderPass;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(mVulkanDevice->mLogicalDevice, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.blurHorz));

		// Phong pass (3D model)
		shaderStages[0] = loadShader(getAssetPath() + "shaders/bloom/phongpass.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getAssetPath() + "shaders/bloom/phongpass.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		blendAttachmentState.blendEnable = VK_FALSE;
		depthStencilState.depthWriteEnable = VK_TRUE;
		rasterizationState.cullMode = VK_CULL_MODE_BACK_BIT;
		pipelineCreateInfo.renderPass = mRenderPass;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(mVulkanDevice->mLogicalDevice, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.phongPass));

		// Color only pass (offscreen blur base)
		shaderStages[0] = loadShader(getAssetPath() + "shaders/bloom/colorpass.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getAssetPath() + "shaders/bloom/colorpass.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		pipelineCreateInfo.renderPass = offscreenPass.renderPass;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(mVulkanDevice->mLogicalDevice, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.glowPass));

		// Skybox (cubemap)
		shaderStages[0] = loadShader(getAssetPath() + "shaders/bloom/skybox.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getAssetPath() + "shaders/bloom/skybox.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		depthStencilState.depthWriteEnable = VK_FALSE;
		rasterizationState.cullMode = VK_CULL_MODE_FRONT_BIT;
		pipelineCreateInfo.renderPass = mRenderPass;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(mVulkanDevice->mLogicalDevice, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.skyBox));
	}

	// Prepare and initialize uniform buffer containing shader uniforms
	void prepareUniformBuffers()
	{
		// Phong and color pass vertex shader uniform buffer
		createBuffer(
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			sizeof(ubos.scene),
			&ubos.scene,
			&uniformData.vsScene.buffer,
			&uniformData.vsScene.memory,
			&uniformData.vsScene.descriptor);

		// Fullscreen quad display vertex shader uniform buffer
		createBuffer(
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			sizeof(ubos.fullscreen),
			&ubos.fullscreen,
			&uniformData.vsFullScreen.buffer,
			&uniformData.vsFullScreen.memory,
			&uniformData.vsFullScreen.descriptor);

		// Fullscreen quad fragment shader uniform buffers
		// Vertical blur
		createBuffer(
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			sizeof(ubos.vertBlur),
			&ubos.vertBlur,
			&uniformData.fsVertBlur.buffer,
			&uniformData.fsVertBlur.memory,
			&uniformData.fsVertBlur.descriptor);
		// Horizontal blur
		createBuffer(
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			sizeof(ubos.horzBlur),
			&ubos.horzBlur,
			&uniformData.fsHorzBlur.buffer,
			&uniformData.fsHorzBlur.memory,
			&uniformData.fsHorzBlur.descriptor);

		// Skybox
		createBuffer(
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			sizeof(ubos.skyBox),
			&ubos.skyBox,
			&uniformData.vsSkyBox.buffer,
			&uniformData.vsSkyBox.memory,
			&uniformData.vsSkyBox.descriptor);

		// Intialize uniform buffers
		updateUniformBuffersScene();
		updateUniformBuffersScreen();
	}

	// Update uniform buffers for rendering the 3D scene
	void updateUniformBuffersScene()
	{
		Matrix::createPerspectiveVK(MATH_DEG_TO_RAD(45.0f), (float)width / (float)height, 0.1f, 256.0f, &ubos.fullscreen.projection);

		Matrix viewMatrix, tmpMat;
		viewMatrix.translate(Vector3(0.0f, -1.0f, mZoom));
		tmpMat.translate(Vector3(sin(MATH_DEG_TO_RAD(timer * 360.0f)) * 0.25f, 0.0f, cos(MATH_DEG_TO_RAD(timer * 360.0f)) * 0.25f) + cameraPos);
		// UFO
		//ubos.fullscreen.projection = glm::perspective(glm::radians(45.0f), (float)width / (float)height, 0.1f, 256.0f);
		//glm::mat4 viewMatrix = glm::translate(glm::mat4(), glm::vec3(0.0f, -1.0f, mZoom));

		ubos.fullscreen.model = viewMatrix * tmpMat;
		//glm::translate(glm::mat4(), glm::vec3(sin(glm::radians(timer * 360.0f)) * 0.25f, 0.0f, cos(glm::radians(timer * 360.0f)) * 0.25f) + cameraPos);

		ubos.fullscreen.model.rotateX(MATH_DEG_TO_RAD(mRotation.x));
		ubos.fullscreen.model.rotateX(-sinf(MATH_DEG_TO_RAD(timer * 360.0f)) * 0.15f);
		ubos.fullscreen.model.rotateY(MATH_DEG_TO_RAD(mRotation.y));
		ubos.fullscreen.model.rotateY(MATH_DEG_TO_RAD(timer * 360.0f));
		ubos.fullscreen.model.rotateZ(MATH_DEG_TO_RAD(mRotation.z));

		//ubos.fullscreen.model = glm::rotate(ubos.fullscreen.model, glm::radians(mRotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
		//ubos.fullscreen.model = glm::rotate(ubos.fullscreen.model, -sinf(glm::radians(timer * 360.0f)) * 0.15f, glm::vec3(1.0f, 0.0f, 0.0f));
		//ubos.fullscreen.model = glm::rotate(ubos.fullscreen.model, glm::radians(mRotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
		//ubos.fullscreen.model = glm::rotate(ubos.fullscreen.model, glm::radians(timer * 360.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		//ubos.fullscreen.model = glm::rotate(ubos.fullscreen.model, glm::radians(mRotation.z), glm::vec3(0.0f, 0.0f, 1.0f));

		uint8_t *pData;
		VK_CHECK_RESULT(vkMapMemory(mVulkanDevice->mLogicalDevice, uniformData.vsFullScreen.memory, 0, sizeof(ubos.fullscreen), 0, (void **)&pData));
		memcpy(pData, &ubos.fullscreen, sizeof(ubos.fullscreen));
		vkUnmapMemory(mVulkanDevice->mLogicalDevice, uniformData.vsFullScreen.memory);

		Matrix::createPerspectiveVK(MATH_DEG_TO_RAD(45.0f), (float)width / (float)height, 0.1f, 256.0f, &ubos.skyBox.projection);
		// Skybox
		//ubos.skyBox.projection = glm::perspective(glm::radians(45.0f), (float)width / (float)height, 0.1f, 256.0f);
		Matrix::createRotationX(MATH_DEG_TO_RAD(mRotation.x), &ubos.skyBox.model);
		ubos.skyBox.model.rotateY(MATH_DEG_TO_RAD(mRotation.y));
		ubos.skyBox.model.rotateZ(MATH_DEG_TO_RAD(mRotation.z));
		//ubos.skyBox.model = glm::mat4();
		//ubos.skyBox.model = glm::rotate(ubos.skyBox.model, glm::radians(mRotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
		//ubos.skyBox.model = glm::rotate(ubos.skyBox.model, glm::radians(mRotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
		//ubos.skyBox.model = glm::rotate(ubos.skyBox.model, glm::radians(mRotation.z), glm::vec3(0.0f, 0.0f, 1.0f));

		VK_CHECK_RESULT(vkMapMemory(mVulkanDevice->mLogicalDevice, uniformData.vsSkyBox.memory, 0, sizeof(ubos.skyBox), 0, (void **)&pData));
		memcpy(pData, &ubos.skyBox, sizeof(ubos.skyBox));
		vkUnmapMemory(mVulkanDevice->mLogicalDevice, uniformData.vsSkyBox.memory);
	}

	// Update uniform buffers for the fullscreen quad
	void updateUniformBuffersScreen()
	{
		Matrix::createOrthographicOffCenter(0.0f, 1.0f, 0.0f, 1.0f, -1.0f, 1.0f, &ubos.scene.projection);
		// Vertex shader
		//ubos.scene.projection = glm::ortho(0.0f, 1.0f, 0.0f, 1.0f, -1.0f, 1.0f);
		//ubos.scene.model = glm::mat4();

		uint8_t *pData;
		VK_CHECK_RESULT(vkMapMemory(mVulkanDevice->mLogicalDevice, uniformData.vsScene.memory, 0, sizeof(ubos.scene), 0, (void **)&pData));
		memcpy(pData, &ubos.scene, sizeof(ubos.scene));
		vkUnmapMemory(mVulkanDevice->mLogicalDevice, uniformData.vsScene.memory);

		// Fragment shader
		// Vertical
		ubos.vertBlur.horizontal = 0;
		VK_CHECK_RESULT(vkMapMemory(mVulkanDevice->mLogicalDevice, uniformData.fsVertBlur.memory, 0, sizeof(ubos.vertBlur), 0, (void **)&pData));
		memcpy(pData, &ubos.vertBlur, sizeof(ubos.vertBlur));
		vkUnmapMemory(mVulkanDevice->mLogicalDevice, uniformData.fsVertBlur.memory);
		// Horizontal
		ubos.horzBlur.horizontal = 1;
		VK_CHECK_RESULT(vkMapMemory(mVulkanDevice->mLogicalDevice, uniformData.fsHorzBlur.memory, 0, sizeof(ubos.horzBlur), 0, (void **)&pData));
		memcpy(pData, &ubos.horzBlur, sizeof(ubos.horzBlur));
		vkUnmapMemory(mVulkanDevice->mLogicalDevice, uniformData.fsHorzBlur.memory);
	}

	void draw()
	{
		VulkanBase::prepareFrame();

		// The scene render command buffer has to wait for the offscreen rendering to be finished before we can use the framebuffer 
		// color image for sampling during final rendering
		// To ensure this we use a dedicated offscreen synchronization semaphore that will be signaled when offscreen rendering has been finished
		// This is necessary as an implementation may start both command buffers at the same time, there is no guarantee
		// that command buffers will be executed in the order they have been submitted by the application

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
		mSubmitInfo.pWaitSemaphores = &offscreenPass.semaphore;
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
		setupVertexDescriptions();
		prepareUniformBuffers();
		prepareOffscreen();
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
		if (!paused)
		{
			updateUniformBuffersScene();
		}
	}

	virtual void viewChanged()
	{
		updateUniformBuffersScene();
		updateUniformBuffersScreen();
	}

	virtual void keyPressed(uint32_t keyCode)
	{
		switch (keyCode)
		{
		case Keyboard::KEY_KPADD:
		case GAMEPAD_BUTTON_R1:
			changeBlurScale(0.25f);
			break;
		case Keyboard::KEY_KPSUB:
		case GAMEPAD_BUTTON_L1:
			changeBlurScale(-0.25f);
			break;
		case Keyboard::KEY_B:
		case GAMEPAD_BUTTON_A:
			toggleBloom();
			break;
		}
	}

	virtual void getOverlayText(VulkanTextOverlay *textOverlay)
	{
#if defined(__ANDROID__)
		textOverlay->addText("Press \"L1/R1\" to change blur scale", 5.0f, 85.0f, VulkanTextOverlay::alignLeft);
		textOverlay->addText("Press \"Button A\" to toggle bloom", 5.0f, 105.0f, VulkanTextOverlay::alignLeft);
#else
		textOverlay->addText("Press \"NUMPAD +/-\" to change blur scale", 5.0f, 85.0f, VulkanTextOverlay::alignLeft);
		textOverlay->addText("Press \"B\" to toggle bloom", 5.0f, 105.0f, VulkanTextOverlay::alignLeft);
#endif
	}

	void changeBlurScale(float delta)
	{
		ubos.vertBlur.blurScale += delta;
		ubos.horzBlur.blurScale += delta;
		updateUniformBuffersScreen();
	}

	void toggleBloom()
	{
		bloom = !bloom;
		reBuildCommandBuffers();
	}
};

