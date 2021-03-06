#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <vector>
#include <algorithm>

#include "define.h"

#include <vulkan/vulkan.h>
#include "VulkanBase.h"
#include "VkCoreDevice.hpp"
#include "vulkanbuffer.hpp"

#define ENABLE_VALIDATION false


class VkTexturemipmapgen : public VulkanBase
{
	std::vector<vkMeshLoader::VertexLayout> vertexLayout =
	{
		vkMeshLoader::VERTEX_LAYOUT_POSITION,
		vkMeshLoader::VERTEX_LAYOUT_UV,
		vkMeshLoader::VERTEX_LAYOUT_NORMAL
	};
public:
	struct Texture {
		VkImage image;
		VkImageLayout imageLayout;
		VkDeviceMemory deviceMemory;
		VkImageView view;
		uint32_t width, height;
		uint32_t mipLevels;
	} texture;

	// To demonstrate mip mapping and filtering this example uses separate samplers
	std::vector<std::string> samplerNames{ "No mip maps" , "With mip maps (bilinear)" , "With mip maps (anisotropic)" };
	std::vector<VkSampler> samplers;

	struct {
		vkMeshLoader::MeshBuffer tunnel;
	} meshes;

	struct {
		VkPipelineVertexInputStateCreateInfo inputState;
		std::vector<VkVertexInputBindingDescription> bindingDescriptions;
		std::vector<VkVertexInputAttributeDescription> attributeDescriptions;
	} vertices;

	vk::Buffer uniformBufferVS;

	struct uboVS {
		Matrix projection;
		Matrix view;
		Matrix model;
		Vector4 viewPos;
		float lodBias = 0.0f;
		uint32_t samplerIndex = 2;
	} uboVS;

	struct {
		VkPipeline solid;
	} pipelines;

	VkPipelineLayout pipelineLayout;
	VkDescriptorSet descriptorSet;
	VkDescriptorSetLayout descriptorSetLayout;

	VkTexturemipmapgen() : VulkanBase(ENABLE_VALIDATION)
	{
		title = "Vulkan Example - Runtime mip map generation";
		mEnableTextOverlay = true;
		mCamera.type = VkCamera::CameraType::firstperson;
		mCamera.setPerspective(60.0f, (float)width / (float)height, 0.1f, 1024.0f);
		mCamera.setRotation(Vector3(0.0f, 90.0f, 0.0f));
		mCamera.setTranslation(Vector3(40.75f, 0.0f, 0.0f));
		mCamera.movementSpeed = 2.5f;
		mCamera.rotationSpeed = 0.5f;
		timerSpeed *= 0.05f;
		paused = true;
	}

	~VkTexturemipmapgen()
	{
		destroyTextureImage(texture);
		vkDestroyPipeline(mVulkanDevice->mLogicalDevice, pipelines.solid, nullptr);
		vkDestroyPipelineLayout(mVulkanDevice->mLogicalDevice, pipelineLayout, nullptr);
		vkDestroyDescriptorSetLayout(mVulkanDevice->mLogicalDevice, descriptorSetLayout, nullptr);
		uniformBufferVS.destroy();
		for (auto sampler : samplers)
		{
			vkDestroySampler(mVulkanDevice->mLogicalDevice, sampler, nullptr);
		}
		vkMeshLoader::freeMeshBufferResources(mVulkanDevice->mLogicalDevice, &meshes.tunnel);
	}

	void loadTexture(std::string fileName, VkFormat format, bool forceLinearTiling)
	{
#if defined(__ANDROID__)
		// Textures are stored inside the apk on Android (compressed)
		// So they need to be loaded via the asset manager
		AAsset* asset = AAssetManager_open(androidApp->activity->assetManager, fileName.c_str(), AASSET_MODE_STREAMING);
		assert(asset);
		size_t size = AAsset_getLength(asset);
		assert(size > 0);

		void *textureData = malloc(size);
		AAsset_read(asset, textureData, size);
		AAsset_close(asset);

		gli::texture2D tex2D(gli::load((const char*)textureData, size));
#else
		gli::texture2D tex2D(gli::load(fileName));
#endif

		assert(!tex2D.empty());

		VkFormatProperties formatProperties;

		texture.width = static_cast<uint32_t>(tex2D[0].dimensions().x);
		texture.height = static_cast<uint32_t>(tex2D[0].dimensions().y);

		// calculate num of mip maps
		// numLevels = 1 + floor(log2(max(w, h, d)))
		// Calculated as log2(max(width, height, depth))c + 1 (see specs)
		texture.mipLevels = floor(log2(std::max(texture.width, texture.height))) + 1;

		// Get device properites for the requested texture format
		vkGetPhysicalDeviceFormatProperties(mVulkanDevice->mPhysicalDevice, format, &formatProperties);

		// Mip-chain generation requires support for blit source and destination
		assert(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT);
		assert(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT);

		VkMemoryAllocateInfo memAllocInfo = vkTools::memoryAllocateInfo();
		VkMemoryRequirements memReqs = {};

		// Create a host-visible staging buffer that contains the raw image data
		VkBuffer stagingBuffer;
		VkDeviceMemory stagingMemory;

		VkBufferCreateInfo bufferCreateInfo = vkTools::bufferCreateInfo();
		bufferCreateInfo.size = tex2D.size();
		// This buffer is used as a transfer source for the buffer copy
		bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		VK_CHECK_RESULT(vkCreateBuffer(mVulkanDevice->mLogicalDevice, &bufferCreateInfo, nullptr, &stagingBuffer));
		vkGetBufferMemoryRequirements(mVulkanDevice->mLogicalDevice, stagingBuffer, &memReqs);
		memAllocInfo.allocationSize = memReqs.size;
		memAllocInfo.memoryTypeIndex = mVulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
		VK_CHECK_RESULT(vkAllocateMemory(mVulkanDevice->mLogicalDevice, &memAllocInfo, nullptr, &stagingMemory));
		VK_CHECK_RESULT(vkBindBufferMemory(mVulkanDevice->mLogicalDevice, stagingBuffer, stagingMemory, 0));

		// Copy texture data into staging buffer
		uint8_t *data;
		VK_CHECK_RESULT(vkMapMemory(mVulkanDevice->mLogicalDevice, stagingMemory, 0, memReqs.size, 0, (void **)&data));
		memcpy(data, tex2D.data(), tex2D.size());
		vkUnmapMemory(mVulkanDevice->mLogicalDevice, stagingMemory);

		// Create optimal tiled target image
		VkImageCreateInfo imageCreateInfo = vkTools::imageCreateInfo();
		imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
		imageCreateInfo.format = format;
		imageCreateInfo.mipLevels = texture.mipLevels;
		imageCreateInfo.arrayLayers = 1;
		imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageCreateInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
		imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageCreateInfo.extent = { texture.width, texture.height, 1 };
		imageCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		VK_CHECK_RESULT(vkCreateImage(mVulkanDevice->mLogicalDevice, &imageCreateInfo, nullptr, &texture.image));
		vkGetImageMemoryRequirements(mVulkanDevice->mLogicalDevice, texture.image, &memReqs);
		memAllocInfo.allocationSize = memReqs.size;
		memAllocInfo.memoryTypeIndex = mVulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		VK_CHECK_RESULT(vkAllocateMemory(mVulkanDevice->mLogicalDevice, &memAllocInfo, nullptr, &texture.deviceMemory));
		VK_CHECK_RESULT(vkBindImageMemory(mVulkanDevice->mLogicalDevice, texture.image, texture.deviceMemory, 0));

		VkCommandBuffer copyCmd = VulkanBase::createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

		VkImageSubresourceRange subresourceRange = {};
		subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		subresourceRange.levelCount = 1;
		subresourceRange.layerCount = 1;

		// Optimal image will be used as destination for the copy, so we must transfer from our initial undefined image layout to the transfer destination layout
		vkTools::setImageLayout(copyCmd, texture.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, subresourceRange);

		// Copy the first mip of the chain, remaining mips will be generated
		VkBufferImageCopy bufferCopyRegion = {};
		bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		bufferCopyRegion.imageSubresource.mipLevel = 0;
		bufferCopyRegion.imageSubresource.baseArrayLayer = 0;
		bufferCopyRegion.imageSubresource.layerCount = 1;
		bufferCopyRegion.imageExtent.width = texture.width;
		bufferCopyRegion.imageExtent.height = texture.height;
		bufferCopyRegion.imageExtent.depth = 1;

		vkCmdCopyBufferToImage(copyCmd, stagingBuffer, texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bufferCopyRegion);

		// Transition first mip level to transfer source for read during blit
		texture.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		vkTools::setImageLayout(
			copyCmd,
			texture.image,
			VK_IMAGE_ASPECT_COLOR_BIT,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			subresourceRange);

		VulkanBase::flushCommandBuffer(copyCmd, mQueue, true);

		// Clean up staging resources
		vkFreeMemory(mVulkanDevice->mLogicalDevice, stagingMemory, nullptr);
		vkDestroyBuffer(mVulkanDevice->mLogicalDevice, stagingBuffer, nullptr);

		// Generate the mip chain
		// ---------------------------------------------------------------
		// We copy down the whole mip chain doing a blit from mip-1 to mip
		// An alternative way would be to always blit from the first mip level and sample that one down
		VkCommandBuffer blitCmd = VulkanBase::createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

		// Copy down mips from n-1 to n
		for (int32_t i = 1; i < texture.mipLevels; i++)
		{
			VkImageBlit imageBlit{};

			// Source
			imageBlit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			imageBlit.srcSubresource.layerCount = 1;
			imageBlit.srcSubresource.mipLevel = i - 1;
			imageBlit.srcOffsets[1].x = int32_t(texture.width >> (i - 1));
			imageBlit.srcOffsets[1].y = int32_t(texture.height >> (i - 1));
			imageBlit.srcOffsets[1].z = 1;

			// Destination
			imageBlit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			imageBlit.dstSubresource.layerCount = 1;
			imageBlit.dstSubresource.mipLevel = i;
			imageBlit.dstOffsets[1].x = int32_t(texture.width >> i);
			imageBlit.dstOffsets[1].y = int32_t(texture.height >> i);
			imageBlit.dstOffsets[1].z = 1;

			VkImageSubresourceRange mipSubRange = {};
			mipSubRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			mipSubRange.baseMipLevel = i;
			mipSubRange.levelCount = 1;
			mipSubRange.layerCount = 1;

			// Transiton current mip level to transfer dest
			vkTools::setImageLayout(
				blitCmd,
				texture.image,
				VK_IMAGE_ASPECT_COLOR_BIT,
				VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				mipSubRange);

			// Blit from previous level
			vkCmdBlitImage(
				blitCmd,
				texture.image,
				VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				texture.image,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				1,
				&imageBlit,
				VK_FILTER_LINEAR);

			// Transiton current mip level to transfer source for read in next iteration
			vkTools::setImageLayout(
				blitCmd,
				texture.image,
				VK_IMAGE_ASPECT_COLOR_BIT,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				mipSubRange);
		}

		// After the loop, all mip layers are in TRANSFER_SRC layout, so transition all to SHADER_READ
		subresourceRange.levelCount = texture.mipLevels;
		vkTools::setImageLayout(
			blitCmd,
			texture.image,
			VK_IMAGE_ASPECT_COLOR_BIT,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			texture.imageLayout,
			subresourceRange);

		VulkanBase::flushCommandBuffer(blitCmd, mQueue, true);
		// ---------------------------------------------------------------

		// Create samplers
		samplers.resize(3);
		VkSamplerCreateInfo sampler = vkTools::samplerCreateInfo();
		sampler.magFilter = VK_FILTER_LINEAR;
		sampler.minFilter = VK_FILTER_LINEAR;
		sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
		sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
		sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
		sampler.mipLodBias = 0.0f;
		sampler.compareOp = VK_COMPARE_OP_NEVER;
		sampler.minLod = 0.0f;
		sampler.maxLod = 0.0f;
		sampler.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
		sampler.maxAnisotropy = 1.0;
		sampler.anisotropyEnable = VK_FALSE;

		// Without mip mapping
		VK_CHECK_RESULT(vkCreateSampler(mVulkanDevice->mLogicalDevice, &sampler, nullptr, &samplers[0]));

		// With mip mapping
		sampler.maxLod = (float)texture.mipLevels;
		VK_CHECK_RESULT(vkCreateSampler(mVulkanDevice->mLogicalDevice, &sampler, nullptr, &samplers[1]));

		// With mip mapping and anisotropic filtering
		if (mVulkanDevice->mFeatures.samplerAnisotropy)
		{
			sampler.maxAnisotropy = mVulkanDevice->mProperties.limits.maxSamplerAnisotropy;
			sampler.anisotropyEnable = VK_TRUE;
		}
		VK_CHECK_RESULT(vkCreateSampler(mVulkanDevice->mLogicalDevice, &sampler, nullptr, &samplers[2]));

		// Create image view
		VkImageViewCreateInfo view = vkTools::imageViewCreateInfo();
		view.image = texture.image;
		view.viewType = VK_IMAGE_VIEW_TYPE_2D;
		view.format = format;
		view.components = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A };
		view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		view.subresourceRange.baseMipLevel = 0;
		view.subresourceRange.baseArrayLayer = 0;
		view.subresourceRange.layerCount = 1;
		view.subresourceRange.levelCount = texture.mipLevels;
		VK_CHECK_RESULT(vkCreateImageView(mVulkanDevice->mLogicalDevice, &view, nullptr, &texture.view));
	}

	// Free all Vulkan resources used a texture object
	void destroyTextureImage(Texture texture)
	{
		vkDestroyImageView(mVulkanDevice->mLogicalDevice, texture.view, nullptr);
		vkDestroyImage(mVulkanDevice->mLogicalDevice, texture.image, nullptr);
		vkFreeMemory(mVulkanDevice->mLogicalDevice, texture.deviceMemory, nullptr);
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

			vkCmdBindDescriptorSets(mDrawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, NULL);
			vkCmdBindPipeline(mDrawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.solid);

			VkDeviceSize offsets[1] = { 0 };
			vkCmdBindVertexBuffers(mDrawCmdBuffers[i], VERTEX_BUFFER_BIND_ID, 1, &meshes.tunnel.vertices.buf, offsets);
			vkCmdBindIndexBuffer(mDrawCmdBuffers[i], meshes.tunnel.indices.buf, 0, VK_INDEX_TYPE_UINT32);

			vkCmdDrawIndexed(mDrawCmdBuffers[i], meshes.tunnel.indexCount, 1, 0, 0, 0);

			vkCmdEndRenderPass(mDrawCmdBuffers[i]);

			VK_CHECK_RESULT(vkEndCommandBuffer(mDrawCmdBuffers[i]));
		}
	}

	void draw()
	{
		VulkanBase::prepareFrame();

		// Command buffer to be sumitted to the queue
		mSubmitInfo.commandBufferCount = 1;
		mSubmitInfo.pCommandBuffers = &mDrawCmdBuffers[gSwapChain.mCurrentBuffer];

		// Submit to queue
		VK_CHECK_RESULT(vkQueueSubmit(mQueue, 1, &mSubmitInfo, VK_NULL_HANDLE));

		VulkanBase::submitFrame();
	}

	void loadAssets()
	{
		loadMesh(getAssetPath() + "models/tunnel_cylinder.dae", &meshes.tunnel, vertexLayout, 1.0f);
		loadTexture(getAssetPath() + "textures/metalplate_nomips_rgba.ktx", VK_FORMAT_R8G8B8A8_UNORM, false);
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
		// Location 1 : Texture coordinates
		vertices.attributeDescriptions[1] =
			vkTools::vertexInputAttributeDescription(
				VERTEX_BUFFER_BIND_ID,
				1,
				VK_FORMAT_R32G32_SFLOAT,
				3 * sizeof(float));
		// Location 1 : Vertex normal
		vertices.attributeDescriptions[2] =
			vkTools::vertexInputAttributeDescription(
				VERTEX_BUFFER_BIND_ID,
				2,
				VK_FORMAT_R32G32B32_SFLOAT,
				5 * sizeof(float));

		vertices.inputState = vkTools::pipelineVertexInputStateCreateInfo();
		vertices.inputState.vertexBindingDescriptionCount = static_cast<uint32_t>(vertices.bindingDescriptions.size());
		vertices.inputState.pVertexBindingDescriptions = vertices.bindingDescriptions.data();
		vertices.inputState.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertices.attributeDescriptions.size());
		vertices.inputState.pVertexAttributeDescriptions = vertices.attributeDescriptions.data();
	}

	void setupDescriptorPool()
	{
		std::vector<VkDescriptorPoolSize> poolSizes =
		{
			vkTools::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1),	// Vertex shader UBO
			vkTools::descriptorPoolSize(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1),		// Sampled image
			vkTools::descriptorPoolSize(VK_DESCRIPTOR_TYPE_SAMPLER, 3),			// 3 samplers (array)
		};

		VkDescriptorPoolCreateInfo descriptorPoolInfo =
			vkTools::descriptorPoolCreateInfo(
				static_cast<uint32_t>(poolSizes.size()),
				poolSizes.data(),
				1);

		VK_CHECK_RESULT(vkCreateDescriptorPool(mVulkanDevice->mLogicalDevice, &descriptorPoolInfo, nullptr, &descriptorPool));
	}

	void setupDescriptorSetLayout()
	{
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings;

		// Binding 0: Vertex shader uniform buffer
		setLayoutBindings.push_back(vkTools::descriptorSetLayoutBinding(
			VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			VK_SHADER_STAGE_VERTEX_BIT,
			0));

		// Binding 1: Sampled image
		setLayoutBindings.push_back(vkTools::descriptorSetLayoutBinding(
			VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
			VK_SHADER_STAGE_FRAGMENT_BIT,
			1));

		// Binding 2: Sampler array (3 descriptors)
		setLayoutBindings.push_back(vkTools::descriptorSetLayoutBinding(
			VK_DESCRIPTOR_TYPE_SAMPLER,
			VK_SHADER_STAGE_FRAGMENT_BIT,
			2,
			3));

		VkDescriptorSetLayoutCreateInfo descriptorLayout =
			vkTools::descriptorSetLayoutCreateInfo(
				setLayoutBindings.data(),
				static_cast<uint32_t>(setLayoutBindings.size()));

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

		std::vector<VkWriteDescriptorSet> writeDescriptorSets;

		// Binding 0: Vertex shader uniform buffer
		writeDescriptorSets.push_back(vkTools::writeDescriptorSet(
			descriptorSet,
			VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			0,
			&uniformBufferVS.descriptor));

		// Binding 1: Sampled image
		VkDescriptorImageInfo texDescriptor = vkTools::descriptorImageInfo(VK_NULL_HANDLE, texture.view, VK_IMAGE_LAYOUT_GENERAL);
		writeDescriptorSets.push_back(vkTools::writeDescriptorSet(
			descriptorSet,
			VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
			1,
			&texDescriptor));

		// Binding 2: Sampler array
		std::vector<VkDescriptorImageInfo> samplerDescriptors;
		for (auto i = 0; i < samplers.size(); i++)
		{
			samplerDescriptors.push_back(vkTools::descriptorImageInfo(samplers[i], VK_NULL_HANDLE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
		}
		VkWriteDescriptorSet samplerDescriptorWrite{};
		samplerDescriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		samplerDescriptorWrite.dstSet = descriptorSet;
		samplerDescriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
		samplerDescriptorWrite.descriptorCount = static_cast<uint32_t>(samplerDescriptors.size());
		samplerDescriptorWrite.pImageInfo = samplerDescriptors.data();
		samplerDescriptorWrite.dstBinding = 2;
		samplerDescriptorWrite.dstArrayElement = 0;
		writeDescriptorSets.push_back(samplerDescriptorWrite);

		vkUpdateDescriptorSets(mVulkanDevice->mLogicalDevice, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);
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
			VK_DYNAMIC_STATE_SCISSOR
		};
		VkPipelineDynamicStateCreateInfo dynamicState =
			vkTools::pipelineDynamicStateCreateInfo(
				dynamicStateEnables.data(),
				static_cast<uint32_t>(dynamicStateEnables.size()),
				0);

		// Load shaders
		std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

		shaderStages[0] = loadShader(getAssetPath() + "shaders/texturemipmapgen/texture.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getAssetPath() + "shaders/texturemipmapgen/texture.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

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
		pipelineCreateInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
		pipelineCreateInfo.pStages = shaderStages.data();

		VK_CHECK_RESULT(vkCreateGraphicsPipelines(mVulkanDevice->mLogicalDevice, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.solid));
	}

	// Prepare and initialize uniform buffer containing shader uniforms
	void prepareUniformBuffers()
	{
		// Vertex shader uniform buffer block
		VK_CHECK_RESULT(mVulkanDevice->createBuffer(
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&uniformBufferVS,
			sizeof(uboVS),
			&uboVS));

		updateUniformBuffers();
	}

	void updateUniformBuffers()
	{
		uboVS.projection = mCamera.mMatrices.perspective;
		uboVS.view = mCamera.mMatrices.view;
		//uboVS.model = glm::rotate(glm::mat4(), glm::radians(timer * 360.0f), glm::vec3(1.0f, 0.0f, 0.0f));
		//uboVS.viewPos = glm::vec4(mCamera.position, 0.0f) * glm::vec4(-1.0f);
		Matrix::createRotationX(MATH_DEG_TO_RAD(timer * 360.0f), &uboVS.model);
		uboVS.viewPos = Vector4(-mCamera.position.x, -mCamera.position.y, -mCamera.position.z, 0.0f);
		VK_CHECK_RESULT(uniformBufferVS.map());
		memcpy(uniformBufferVS.mapped, &uboVS, sizeof(uboVS));
		uniformBufferVS.unmap();
	}

	void prepare()
	{
		VulkanBase::prepare();
		loadAssets();
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
		draw();
		if (!paused)
		{
			updateUniformBuffers();
		}
	}

	virtual void viewChanged()
	{
		updateUniformBuffers();
	}

	void changeLodBias(float delta)
	{
		uboVS.lodBias += delta;
		if (uboVS.lodBias < 0.0f)
		{
			uboVS.lodBias = 0.0f;
		}
		if (uboVS.lodBias > texture.mipLevels)
		{
			uboVS.lodBias = (float)texture.mipLevels;
		}
		updateUniformBuffers();
		updateTextOverlay();
	}

	void toggleSampler()
	{
		uboVS.samplerIndex = (uboVS.samplerIndex < static_cast<uint32_t>(samplers.size()) - 1) ? uboVS.samplerIndex + 1 : 0;
		updateUniformBuffers();
		updateTextOverlay();
	}

	virtual void keyPressed(uint32_t keyCode)
	{
		switch (keyCode)
		{
		case Keyboard::KEY_KPADD:
		case GAMEPAD_BUTTON_R1:
			changeLodBias(0.1f);
			break;
		case Keyboard::KEY_KPSUB:
		case GAMEPAD_BUTTON_L1:
			changeLodBias(-0.1f);
			break;
		case Keyboard::KEY_F:
		case GAMEPAD_BUTTON_A:
			toggleSampler();
			break;
		}
	}

	virtual void getOverlayText(VulkanTextOverlay *textOverlay)
	{
		std::stringstream ss;
		ss << std::setprecision(2) << std::fixed << uboVS.lodBias;
#if defined(__ANDROID__)
		textOverlay->addText("LOD bias: " + ss.str() + " (Buttons L1/R1 to change)", 5.0f, 85.0f, VulkanTextOverlay::alignLeft);
		textOverlay->addText("Sampler: " + samplerNames[uboVS.samplerIndex] + " (\"Button A\" to toggle)", 5.0f, 105.0f, VulkanTextOverlay::alignLeft);
#else
		textOverlay->addText("LOD bias: " + ss.str() + " (numpad +/- to change)", 5.0f, 85.0f, VulkanTextOverlay::alignLeft);
		textOverlay->addText("Sampler: " + samplerNames[uboVS.samplerIndex] + " (\"f\" to toggle)", 5.0f, 105.0f, VulkanTextOverlay::alignLeft);
#endif
	}
};

