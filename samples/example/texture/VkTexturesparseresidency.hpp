#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <vector>
#include <algorithm>
#include <random>
#include <chrono>

#include "define.h"

#include <vulkan/vulkan.h>
#include "VulkanBase.h"
#include "VkCoreDevice.hpp"
#include "vulkanbuffer.hpp"
#include "vulkanheightmap.hpp"

#define ENABLE_VALIDATION false

// Virtual texture page as a part of the partially resident texture
// Contains memory bindings, offsets and status information
struct VirtualTexturePage
{
	VkOffset3D offset;
	VkExtent3D extent;
	VkSparseImageMemoryBind imageMemoryBind;							// Sparse image memory bind for this page
	VkDeviceSize size;													// Page (memory) size in bytes
	uint32_t mipLevel;													// Mip level that this page belongs to
	uint32_t layer;														// Array layer that this page belongs to
	uint32_t index;

	VirtualTexturePage()
	{
		imageMemoryBind.memory = VK_NULL_HANDLE;						// Page initially not backed up by memory
	}

	// Allocate Vulkan memory for the virtual page
	void allocate(VkDevice device, uint32_t memoryTypeIndex)
	{
		if (imageMemoryBind.memory != VK_NULL_HANDLE)
		{
			//std::cout << "Page " << index << " already allocated" << std::endl;
			return;
		};

		imageMemoryBind = {};

		VkMemoryAllocateInfo allocInfo = vkTools::memoryAllocateInfo();
		allocInfo.allocationSize = size;
		allocInfo.memoryTypeIndex = memoryTypeIndex;
		VK_CHECK_RESULT(vkAllocateMemory(device, &allocInfo, nullptr, &imageMemoryBind.memory));

		VkImageSubresource subResource{};
		subResource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		subResource.mipLevel = mipLevel;
		subResource.arrayLayer = layer;

		// Sparse image memory binding
		imageMemoryBind.subresource = subResource;
		imageMemoryBind.extent = extent;
		imageMemoryBind.offset = offset;
	}

	// Release Vulkan memory allocated for this page
	void release(VkDevice device)
	{
		if (imageMemoryBind.memory != VK_NULL_HANDLE)
		{
			vkFreeMemory(device, imageMemoryBind.memory, nullptr);
			imageMemoryBind.memory = VK_NULL_HANDLE;
			//std::cout << "Page " << index << " released" << std::endl;
		}
	}
};

// Virtual texture object containing all pages 
struct VirtualTexture
{
	VkDevice device;
	VkImage image;														// Texture image handle
	VkBindSparseInfo bindSparseInfo;									// Sparse queue binding information
	std::vector<VirtualTexturePage> pages;								// Contains all virtual pages of the texture
	std::vector<VkSparseImageMemoryBind> sparseImageMemoryBinds;		// Sparse image memory bindings of all memory-backed virtual tables
	std::vector<VkSparseMemoryBind>	opaqueMemoryBinds;					// Sparse �paque memory bindings for the mip tail (if present)
	VkSparseImageMemoryBindInfo imageMemoryBindInfo;					// Sparse image memory bind info 
	VkSparseImageOpaqueMemoryBindInfo opaqueMemoryBindInfo;				// Sparse image opaque memory bind info (mip tail)
	uint32_t mipTailStart;												// First mip level in mip tail

	VirtualTexturePage* addPage(VkOffset3D offset, VkExtent3D extent, const VkDeviceSize size, const uint32_t mipLevel, uint32_t layer)
	{
		VirtualTexturePage newPage;
		newPage.offset = offset;
		newPage.extent = extent;
		newPage.size = size;
		newPage.mipLevel = mipLevel;
		newPage.layer = layer;
		newPage.index = static_cast<uint32_t>(pages.size());
		newPage.imageMemoryBind.offset = offset;
		newPage.imageMemoryBind.extent = extent;
		pages.push_back(newPage);
		return &pages.back();
	}

	// Call before sparse binding to update memory bind list etc.
	void updateSparseBindInfo()
	{
		// Update list of memory-backed sparse image memory binds
		sparseImageMemoryBinds.resize(pages.size());
		uint32_t index = 0;
		for (auto page : pages)
		{
			sparseImageMemoryBinds[index] = page.imageMemoryBind;
			index++;
		}
		// Update sparse bind info
		bindSparseInfo = vkTools::bindSparseInfo();
		// todo: Semaphore for queue submission
		// bindSparseInfo.signalSemaphoreCount = 1;
		// bindSparseInfo.pSignalSemaphores = &bindSparseSemaphore;

		// Image memory binds
		imageMemoryBindInfo.image = image;
		imageMemoryBindInfo.bindCount = static_cast<uint32_t>(sparseImageMemoryBinds.size());
		imageMemoryBindInfo.pBinds = sparseImageMemoryBinds.data();
		bindSparseInfo.imageBindCount = (imageMemoryBindInfo.bindCount > 0) ? 1 : 0;
		bindSparseInfo.pImageBinds = &imageMemoryBindInfo;

		// Opaque image memory binds (mip tail)
		opaqueMemoryBindInfo.image = image;
		opaqueMemoryBindInfo.bindCount = static_cast<uint32_t>(opaqueMemoryBinds.size());
		opaqueMemoryBindInfo.pBinds = opaqueMemoryBinds.data();
		bindSparseInfo.imageOpaqueBindCount = (opaqueMemoryBindInfo.bindCount > 0) ? 1 : 0;
		bindSparseInfo.pImageOpaqueBinds = &opaqueMemoryBindInfo;
	}

	// Release all Vulkan resources
	void destroy()
	{
		for (auto page : pages)
		{
			page.release(device);
		}
		for (auto bind : opaqueMemoryBinds)
		{
			vkFreeMemory(device, bind.memory, nullptr);
		}
	}
};

uint32_t memoryTypeIndex;
int32_t lastFilledMip = 0;

class VkTexturesparseresidency : public VulkanBase
{
	// Vertex layout for this example
	struct Vertex
	{
		float pos[3];
		float normal[3];
		float uv[2];
	};

	std::vector<vkMeshLoader::VertexLayout> vertexLayout =
	{
		vkMeshLoader::VERTEX_LAYOUT_POSITION,
		vkMeshLoader::VERTEX_LAYOUT_NORMAL,
		vkMeshLoader::VERTEX_LAYOUT_UV,
	};

public:
	//todo: comments
	struct SparseTexture : VirtualTexture {
		VkSampler sampler;
		VkImageLayout imageLayout;
		VkImageView view;
		VkDescriptorImageInfo descriptor;
		VkFormat format;
		uint32_t width, height;
		uint32_t mipLevels;
		uint32_t layerCount;
	} texture;

	struct {
		vkTools::VulkanTexture source;
	} textures;

	vkTools::HeightMap *heightMap = nullptr;

	struct {
		VkPipelineVertexInputStateCreateInfo inputState;
		std::vector<VkVertexInputBindingDescription> bindingDescriptions;
		std::vector<VkVertexInputAttributeDescription> attributeDescriptions;
	} vertices;

	uint32_t indexCount;

	vk::Buffer uniformBufferVS;

	struct UboVS {
		Matrix projection;
		Matrix model;
		Vector4 viewPos;
		float lodBias = 0.0f;
	} uboVS;

	struct {
		VkPipeline solid;
	} pipelines;

	VkPipelineLayout pipelineLayout;
	VkDescriptorSet descriptorSet;
	VkDescriptorSetLayout descriptorSetLayout;

	//todo: comment
	VkSemaphore bindSparseSemaphore = VK_NULL_HANDLE;

	// Device features to be enabled for this example 
	static VkPhysicalDeviceFeatures getEnabledFeatures()
	{
		VkPhysicalDeviceFeatures enabledFeatures = {};
		enabledFeatures.shaderResourceResidency = VK_TRUE;
		enabledFeatures.shaderResourceMinLod = VK_TRUE;
		return enabledFeatures;
	}

	VkTexturesparseresidency() : VulkanBase(ENABLE_VALIDATION, getEnabledFeatures)
	{
		mZoom = -1.3f;
		mRotation = { 76.25f, 0.0f, 0.0f };
		title = "Vulkan Example - Sparse texture residency";
		mEnableTextOverlay = true;
		std::cout.imbue(std::locale(""));
		// Check if the GPU supports sparse residency for 2D images
		if (!mVulkanDevice->mFeatures.sparseResidencyImage2D)
		{
			vkTools::exitFatal("Device does not support sparse residency for 2D images!", "Feature not supported");
		}
		mCamera.type = VkCamera::CameraType::firstperson;
		mCamera.movementSpeed = 50.0f;
#ifndef __ANDROID__
		mCamera.rotationSpeed = 0.25f;
#endif
		mCamera.position = { 84.5f, 40.5f, 225.0f };
		mCamera.setRotation(Vector3(-8.5f, -200.0f, 0.0f));
		mCamera.setPerspective(60.0f, (float)width / (float)height, 0.1f, 1024.0f);
	}

	~VkTexturesparseresidency()
	{
		// Clean up used Vulkan resources 
		// Note : Inherited destructor cleans up resources stored in base class

		if (heightMap)
			delete heightMap;

		destroyTextureImage(texture);

		vkDestroySemaphore(mVulkanDevice->mLogicalDevice, bindSparseSemaphore, nullptr);

		vkDestroyPipeline(mVulkanDevice->mLogicalDevice, pipelines.solid, nullptr);

		vkDestroyPipelineLayout(mVulkanDevice->mLogicalDevice, pipelineLayout, nullptr);
		vkDestroyDescriptorSetLayout(mVulkanDevice->mLogicalDevice, descriptorSetLayout, nullptr);

		uniformBufferVS.destroy();
	}

	glm::uvec3 alignedDivision(const VkExtent3D& extent, const VkExtent3D& granularity)
	{
		glm::uvec3 res;
		res.x = extent.width / granularity.width + ((extent.width  % granularity.width) ? 1u : 0u);
		res.y = extent.height / granularity.height + ((extent.height % granularity.height) ? 1u : 0u);
		res.z = extent.depth / granularity.depth + ((extent.depth  % granularity.depth) ? 1u : 0u);
		return res;
	}

	void prepareSparseTexture(uint32_t width, uint32_t height, uint32_t layerCount, VkFormat format)
	{
		texture.device = mVulkanDevice->mLogicalDevice;
		texture.width = width;
		texture.height = height;
		texture.mipLevels = floor(log2(std::max(width, height))) + 1;
		texture.layerCount = layerCount;
		texture.format = format;

		// Get device properites for the requested texture format
		VkFormatProperties formatProperties;
		vkGetPhysicalDeviceFormatProperties(mVulkanDevice->mPhysicalDevice, format, &formatProperties);

		// Get sparse image properties
		std::vector<VkSparseImageFormatProperties> sparseProperties;
		// Sparse properties count for the desired format
		uint32_t sparsePropertiesCount;
		vkGetPhysicalDeviceSparseImageFormatProperties(
			mVulkanDevice->mPhysicalDevice,
			format,
			VK_IMAGE_TYPE_2D,
			VK_SAMPLE_COUNT_1_BIT,
			VK_IMAGE_USAGE_SAMPLED_BIT,
			VK_IMAGE_TILING_OPTIMAL,
			&sparsePropertiesCount,
			nullptr);
		// Check if sparse is supported for this format
		if (sparsePropertiesCount == 0)
		{
			std::cout << "Error: Requested format does not support sparse features!" << std::endl;
			return;
		}

		// Get actual image format properties
		sparseProperties.resize(sparsePropertiesCount);
		vkGetPhysicalDeviceSparseImageFormatProperties(
			mVulkanDevice->mPhysicalDevice,
			format,
			VK_IMAGE_TYPE_2D,
			VK_SAMPLE_COUNT_1_BIT,
			VK_IMAGE_USAGE_SAMPLED_BIT,
			VK_IMAGE_TILING_OPTIMAL,
			&sparsePropertiesCount,
			sparseProperties.data());

		std::cout << "Sparse image format properties: " << sparsePropertiesCount << std::endl;
		for (auto props : sparseProperties)
		{
			std::cout << "\t Image granularity: w = " << props.imageGranularity.width << " h = " << props.imageGranularity.height << " d = " << props.imageGranularity.depth << std::endl;
			std::cout << "\t Aspect mask: " << props.aspectMask << std::endl;
			std::cout << "\t Flags: " << props.flags << std::endl;
		}

		// Create sparse image
		VkImageCreateInfo sparseImageCreateInfo = vkTools::imageCreateInfo();
		sparseImageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
		sparseImageCreateInfo.format = texture.format;
		sparseImageCreateInfo.mipLevels = texture.mipLevels;
		sparseImageCreateInfo.arrayLayers = texture.layerCount;
		sparseImageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		sparseImageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		sparseImageCreateInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
		sparseImageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		sparseImageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		sparseImageCreateInfo.extent = { texture.width, texture.height, 1 };
		sparseImageCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		sparseImageCreateInfo.flags = VK_IMAGE_CREATE_SPARSE_BINDING_BIT | VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT;
		VK_CHECK_RESULT(vkCreateImage(mVulkanDevice->mLogicalDevice, &sparseImageCreateInfo, nullptr, &texture.image));

		// Get memory requirements
		VkMemoryRequirements sparseImageMemoryReqs;
		// Sparse image memory requirement counts
		vkGetImageMemoryRequirements(mVulkanDevice->mLogicalDevice, texture.image, &sparseImageMemoryReqs);

		std::cout << "Image memory requirements:" << std::endl;
		std::cout << "\t Size: " << sparseImageMemoryReqs.size << std::endl;
		std::cout << "\t Alignment: " << sparseImageMemoryReqs.alignment << std::endl;

		// Check requested image size against hardware sparse limit
		if (sparseImageMemoryReqs.size > mVulkanDevice->mProperties.limits.sparseAddressSpaceSize)
		{
			std::cout << "Error: Requested sparse image size exceeds supportes sparse address space size!" << std::endl;
			return;
		};

		// Get sparse memory requirements
		// Count
		uint32_t sparseMemoryReqsCount;
		std::vector<VkSparseImageMemoryRequirements> sparseMemoryReqs(32);
		vkGetImageSparseMemoryRequirements(mVulkanDevice->mLogicalDevice, texture.image, &sparseMemoryReqsCount, sparseMemoryReqs.data());
		if (sparseMemoryReqsCount == 0)
		{
			std::cout << "Error: No memory requirements for the sparse image!" << std::endl;
			return;
		}
		sparseMemoryReqs.resize(sparseMemoryReqsCount);
		// Get actual requirements
		vkGetImageSparseMemoryRequirements(mVulkanDevice->mLogicalDevice, texture.image, &sparseMemoryReqsCount, sparseMemoryReqs.data());

		std::cout << "Sparse image memory requirements: " << sparseMemoryReqsCount << std::endl;
		for (auto reqs : sparseMemoryReqs)
		{
			std::cout << "\t Image granularity: w = " << reqs.formatProperties.imageGranularity.width << " h = " << reqs.formatProperties.imageGranularity.height << " d = " << reqs.formatProperties.imageGranularity.depth << std::endl;
			std::cout << "\t Mip tail first LOD: " << reqs.imageMipTailFirstLod << std::endl;
			std::cout << "\t Mip tail size: " << reqs.imageMipTailSize << std::endl;
			std::cout << "\t Mip tail offset: " << reqs.imageMipTailOffset << std::endl;
			std::cout << "\t Mip tail stride: " << reqs.imageMipTailStride << std::endl;
			//todo:multiple reqs
			texture.mipTailStart = reqs.imageMipTailFirstLod;
		}

		lastFilledMip = texture.mipTailStart - 1;

		// Get sparse image requirements for the color aspect
		VkSparseImageMemoryRequirements sparseMemoryReq;
		bool colorAspectFound = false;
		for (auto reqs : sparseMemoryReqs)
		{
			if (reqs.formatProperties.aspectMask & VK_IMAGE_ASPECT_COLOR_BIT)
			{
				sparseMemoryReq = reqs;
				colorAspectFound = true;
				break;
			}
		}
		if (!colorAspectFound)
		{
			std::cout << "Error: Could not find sparse image memory requirements for color aspect bit!" << std::endl;
			return;
		}

		// todo:
		// Calculate number of required sparse memory bindings by alignment
		assert((sparseImageMemoryReqs.size % sparseImageMemoryReqs.alignment) == 0);
		memoryTypeIndex = mVulkanDevice->getMemoryType(sparseImageMemoryReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

		// Get sparse bindings
		uint32_t sparseBindsCount = static_cast<uint32_t>(sparseImageMemoryReqs.size / sparseImageMemoryReqs.alignment);
		std::vector<VkSparseMemoryBind>	sparseMemoryBinds(sparseBindsCount);

		// Check if the format has a single mip tail for all layers or one mip tail for each layer
		// The mip tail contains all mip levels > sparseMemoryReq.imageMipTailFirstLod
		bool singleMipTail = sparseMemoryReq.formatProperties.flags & VK_SPARSE_IMAGE_FORMAT_SINGLE_MIPTAIL_BIT;

		// Sparse bindings for each mip level of all layers outside of the mip tail
		for (uint32_t layer = 0; layer < texture.layerCount; layer++)
		{
			// sparseMemoryReq.imageMipTailFirstLod is the first mip level that's stored inside the mip tail
			for (uint32_t mipLevel = 0; mipLevel < sparseMemoryReq.imageMipTailFirstLod; mipLevel++)
			{
				VkExtent3D extent;
				extent.width = std::max(sparseImageCreateInfo.extent.width >> mipLevel, 1u);
				extent.height = std::max(sparseImageCreateInfo.extent.height >> mipLevel, 1u);
				extent.depth = std::max(sparseImageCreateInfo.extent.depth >> mipLevel, 1u);

				VkImageSubresource subResource{};
				subResource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				subResource.mipLevel = mipLevel;
				subResource.arrayLayer = layer;

				// Aligned sizes by image granularity
				VkExtent3D imageGranularity = sparseMemoryReq.formatProperties.imageGranularity;
				glm::uvec3 sparseBindCounts = alignedDivision(extent, imageGranularity);
				glm::uvec3 lastBlockExtent;
				lastBlockExtent.x = (extent.width % imageGranularity.width) ? extent.width % imageGranularity.width : imageGranularity.width;
				lastBlockExtent.y = (extent.height % imageGranularity.height) ? extent.height % imageGranularity.height : imageGranularity.height;
				lastBlockExtent.z = (extent.depth % imageGranularity.depth) ? extent.depth % imageGranularity.depth : imageGranularity.depth;

				// Alllocate memory for some blocks
				uint32_t index = 0;
				for (uint32_t z = 0; z < sparseBindCounts.z; z++)
				{
					for (uint32_t y = 0; y < sparseBindCounts.y; y++)
					{
						for (uint32_t x = 0; x < sparseBindCounts.x; x++)
						{
							// Offset 
							VkOffset3D offset;
							offset.x = x * imageGranularity.width;
							offset.y = y * imageGranularity.height;
							offset.z = z * imageGranularity.depth;
							// Size of the page
							VkExtent3D extent;
							extent.width = (x == sparseBindCounts.x - 1) ? lastBlockExtent.x : imageGranularity.width;
							extent.height = (y == sparseBindCounts.y - 1) ? lastBlockExtent.y : imageGranularity.height;
							extent.depth = (z == sparseBindCounts.z - 1) ? lastBlockExtent.z : imageGranularity.depth;

							// Add new virtual page
							VirtualTexturePage *newPage = texture.addPage(offset, extent, sparseImageMemoryReqs.alignment, mipLevel, layer);
							newPage->imageMemoryBind.subresource = subResource;

							if ((x % 2 == 1) || (y % 2 == 1))
							{
								// Allocate memory for this virtual page
								//newPage->allocate(device, memoryTypeIndex);
							}

							index++;
						}
					}
				}
			}

			// Check if format has one mip tail per layer
			if ((!singleMipTail) && (sparseMemoryReq.imageMipTailFirstLod < texture.mipLevels))
			{
				// Allocate memory for the mip tail
				VkMemoryAllocateInfo allocInfo = vkTools::memoryAllocateInfo();
				allocInfo.allocationSize = sparseMemoryReq.imageMipTailSize;
				allocInfo.memoryTypeIndex = memoryTypeIndex;

				VkDeviceMemory deviceMemory;
				VK_CHECK_RESULT(vkAllocateMemory(mVulkanDevice->mLogicalDevice, &allocInfo, nullptr, &deviceMemory));

				// (Opaque) sparse memory binding
				VkSparseMemoryBind sparseMemoryBind{};
				sparseMemoryBind.resourceOffset = sparseMemoryReq.imageMipTailOffset + layer * sparseMemoryReq.imageMipTailStride;
				sparseMemoryBind.size = sparseMemoryReq.imageMipTailSize;
				sparseMemoryBind.memory = deviceMemory;

				texture.opaqueMemoryBinds.push_back(sparseMemoryBind);
			}
		} // end layers and mips

		std::cout << "Texture info:" << std::endl;
		std::cout << "\tDim: " << texture.width << " x " << texture.height << std::endl;
		std::cout << "\tVirtual pages: " << texture.pages.size() << std::endl;

		// Check if format has one mip tail for all layers
		if ((sparseMemoryReq.formatProperties.flags & VK_SPARSE_IMAGE_FORMAT_SINGLE_MIPTAIL_BIT) && (sparseMemoryReq.imageMipTailFirstLod < texture.mipLevels))
		{
			// Allocate memory for the mip tail
			VkMemoryAllocateInfo allocInfo = vkTools::memoryAllocateInfo();
			allocInfo.allocationSize = sparseMemoryReq.imageMipTailSize;
			allocInfo.memoryTypeIndex = memoryTypeIndex;

			VkDeviceMemory deviceMemory;
			VK_CHECK_RESULT(vkAllocateMemory(mVulkanDevice->mLogicalDevice, &allocInfo, nullptr, &deviceMemory));

			// (Opaque) sparse memory binding
			VkSparseMemoryBind sparseMemoryBind{};
			sparseMemoryBind.resourceOffset = sparseMemoryReq.imageMipTailOffset;
			sparseMemoryBind.size = sparseMemoryReq.imageMipTailSize;
			sparseMemoryBind.memory = deviceMemory;

			texture.opaqueMemoryBinds.push_back(sparseMemoryBind);
		}

		// Create signal semaphore for sparse binding
		VkSemaphoreCreateInfo semaphoreCreateInfo = vkTools::semaphoreCreateInfo();
		VK_CHECK_RESULT(vkCreateSemaphore(mVulkanDevice->mLogicalDevice, &semaphoreCreateInfo, nullptr, &bindSparseSemaphore));

		// Prepare bind sparse info for reuse in queue submission
		texture.updateSparseBindInfo();

		// Bind to queue
		// todo: in draw?
		vkQueueBindSparse(mQueue, 1, &texture.bindSparseInfo, VK_NULL_HANDLE);
		//todo: use sparse bind semaphore
		vkQueueWaitIdle(mQueue);

		// Create sampler
		VkSamplerCreateInfo sampler = vkTools::samplerCreateInfo();
		sampler.magFilter = VK_FILTER_LINEAR;
		sampler.minFilter = VK_FILTER_LINEAR;
		sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		sampler.mipLodBias = 0.0f;
		sampler.compareOp = VK_COMPARE_OP_NEVER;
		sampler.minLod = 0.0f;
		sampler.maxLod = static_cast<float>(texture.mipLevels);
		sampler.anisotropyEnable = mVulkanDevice->mFeatures.samplerAnisotropy;
		sampler.maxAnisotropy = mVulkanDevice->mFeatures.samplerAnisotropy ? mVulkanDevice->mProperties.limits.maxSamplerAnisotropy : 1.0f;
		sampler.anisotropyEnable = false;
		sampler.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
		VK_CHECK_RESULT(vkCreateSampler(mVulkanDevice->mLogicalDevice, &sampler, nullptr, &texture.sampler));

		// Create image view
		VkImageViewCreateInfo view = vkTools::imageViewCreateInfo();
		view.image = VK_NULL_HANDLE;
		view.viewType = VK_IMAGE_VIEW_TYPE_2D;
		view.format = format;
		view.components = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A };
		view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		view.subresourceRange.baseMipLevel = 0;
		view.subresourceRange.baseArrayLayer = 0;
		view.subresourceRange.layerCount = 1;
		view.subresourceRange.levelCount = texture.mipLevels;
		view.image = texture.image;
		VK_CHECK_RESULT(vkCreateImageView(mVulkanDevice->mLogicalDevice, &view, nullptr, &texture.view));

		// Fill image descriptor image info that can be used during the descriptor set setup
		texture.descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		texture.descriptor.imageView = texture.view;
		texture.descriptor.sampler = texture.sampler;

		// Fill smallest (non-tail) mip map leve
		fillVirtualTexture(lastFilledMip);
	}

	// Free all Vulkan resources used a texture object
	void destroyTextureImage(SparseTexture texture)
	{
		vkDestroyImageView(mVulkanDevice->mLogicalDevice, texture.view, nullptr);
		vkDestroyImage(mVulkanDevice->mLogicalDevice, texture.image, nullptr);
		vkDestroySampler(mVulkanDevice->mLogicalDevice, texture.sampler, nullptr);
		texture.destroy();
	}

	void buildCommandBuffers()
	{
		VkCommandBufferBeginInfo cmdBufInfo = vkTools::commandBufferBeginInfo();

		VkClearValue clearValues[2];
		clearValues[0].color = { { 0.0f, 0.0f, 0.2f, 1.0f } };
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
			vkCmdBindVertexBuffers(mDrawCmdBuffers[i], VERTEX_BUFFER_BIND_ID, 1, &heightMap->vertexBuffer.buffer, offsets);
			vkCmdBindIndexBuffer(mDrawCmdBuffers[i], heightMap->indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
			vkCmdDrawIndexed(mDrawCmdBuffers[i], heightMap->indexCount, 1, 0, 0, 0);

			vkCmdEndRenderPass(mDrawCmdBuffers[i]);

			VK_CHECK_RESULT(vkEndCommandBuffer(mDrawCmdBuffers[i]));
		}
	}

	void draw()
	{
		VulkanBase::prepareFrame();

		// Sparse bindings
		//		vkQueueBindSparse(queue, 1, &bindSparseInfo, VK_NULL_HANDLE);
		//todo: use sparse bind semaphore
		//		vkQueueWaitIdle(queue);

		// Command buffer to be sumitted to the queue
		mSubmitInfo.commandBufferCount = 1;
		mSubmitInfo.pCommandBuffers = &mDrawCmdBuffers[gSwapChain.mCurrentBuffer];

		// Submit to queue
		VK_CHECK_RESULT(vkQueueSubmit(mQueue, 1, &mSubmitInfo, VK_NULL_HANDLE));

		VulkanBase::submitFrame();
	}

	void loadAssets()
	{
		textureLoader->loadTexture(getAssetPath() + "textures/ground_dry_bc3.ktx", VK_FORMAT_BC3_UNORM_BLOCK, &textures.source, false, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
	}

	// Generate a terrain quad patch for feeding to the tessellation control shader
	void generateTerrain()
	{
		heightMap = new vkTools::HeightMap(mVulkanDevice, mQueue);
#if defined(__ANDROID__)
		heightMap->loadFromFile(getAssetPath() + "textures/terrain_heightmap_r16.ktx", 128, glm::vec3(2.0f, 48.0f, 2.0f), vkTools::HeightMap::topologyTriangles, androidApp->activity->assetManager);
#else
		heightMap->loadFromFile(getAssetPath() + "textures/terrain_heightmap_r16.ktx", 128, glm::vec3(2.0f, 48.0f, 2.0f), vkTools::HeightMap::topologyTriangles);
#endif
	}

	void setupVertexDescriptions()
	{
		// Binding description
		vertices.bindingDescriptions.resize(1);
		vertices.bindingDescriptions[0] =
			vkTools::vertexInputBindingDescription(
				VERTEX_BUFFER_BIND_ID,
				sizeof(Vertex),
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
				offsetof(Vertex, pos));
		// Location 1 : Vertex normal
		vertices.attributeDescriptions[1] =
			vkTools::vertexInputAttributeDescription(
				VERTEX_BUFFER_BIND_ID,
				1,
				VK_FORMAT_R32G32B32_SFLOAT,
				offsetof(Vertex, normal));
		// Location 1 : Texture coordinates
		vertices.attributeDescriptions[2] =
			vkTools::vertexInputAttributeDescription(
				VERTEX_BUFFER_BIND_ID,
				2,
				VK_FORMAT_R32G32_SFLOAT,
				offsetof(Vertex, uv));

		vertices.inputState = vkTools::pipelineVertexInputStateCreateInfo();
		vertices.inputState.vertexBindingDescriptionCount = static_cast<uint32_t>(vertices.bindingDescriptions.size());
		vertices.inputState.pVertexBindingDescriptions = vertices.bindingDescriptions.data();
		vertices.inputState.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertices.attributeDescriptions.size());
		vertices.inputState.pVertexAttributeDescriptions = vertices.attributeDescriptions.data();
	}

	void setupDescriptorPool()
	{
		// Example uses one ubo and one image sampler
		std::vector<VkDescriptorPoolSize> poolSizes =
		{
			vkTools::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1),
			vkTools::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1)
		};

		VkDescriptorPoolCreateInfo descriptorPoolInfo =
			vkTools::descriptorPoolCreateInfo(
				static_cast<uint32_t>(poolSizes.size()),
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
			// Binding 1 : Fragment shader image sampler
			vkTools::descriptorSetLayoutBinding(
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT,
				1)
		};

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

		std::vector<VkWriteDescriptorSet> writeDescriptorSets =
		{
			// Binding 0 : Vertex shader uniform buffer
			vkTools::writeDescriptorSet(
				descriptorSet,
				VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				0,
				&uniformBufferVS.descriptor),
			// Binding 1 : Fragment shader texture sampler
			vkTools::writeDescriptorSet(
				descriptorSet,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				1,
				&texture.descriptor)
		};

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

		shaderStages[0] = loadShader(getAssetPath() + "shaders/texturesparseresidency/sparseresidency.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getAssetPath() + "shaders/texturesparseresidency/sparseresidency.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

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
		// Vertex shader
		//uboVS.projection = glm::perspective(glm::radians(60.0f), (float)width / (float)height, 0.001f, 256.0f);
		//glm::mat4 viewMatrix = glm::translate(glm::mat4(), glm::vec3(0.0f, 0.0f, mZoom));

		//uboVS.model = viewMatrix * glm::translate(glm::mat4(), cameraPos);
		//uboVS.model = glm::rotate(uboVS.model, glm::radians(mRotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
		//uboVS.model = glm::rotate(uboVS.model, glm::radians(mRotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
		//uboVS.model = glm::rotate(uboVS.model, glm::radians(mRotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
		Matrix viewMatrix, matTmp;
		Matrix::createPerspectiveVK(MATH_DEG_TO_RAD(60.0f), (float)width / (float)height, 0.001f, 256.0f, &uboVS.projection);
		viewMatrix.translate(0.0, 0.0f, mZoom);
		matTmp.translate(cameraPos);
		uboVS.model = viewMatrix * matTmp;
		uboVS.model.rotateX(MATH_DEG_TO_RAD(mRotation.x));
		uboVS.model.rotateY(MATH_DEG_TO_RAD(mRotation.y));
		uboVS.model.rotateZ(MATH_DEG_TO_RAD(mRotation.z));

		uboVS.projection = mCamera.mMatrices.perspective;
		uboVS.model = mCamera.mMatrices.view;
		//uboVS.model = glm::mat4();

		uboVS.viewPos = Vector4(0.0f, 0.0f, -mZoom, 0.0f);

		VK_CHECK_RESULT(uniformBufferVS.map());
		memcpy(uniformBufferVS.mapped, &uboVS, sizeof(uboVS));
		uniformBufferVS.unmap();
	}

	void prepare()
	{
		VulkanBase::prepare();
		loadAssets();
		generateTerrain();
		setupVertexDescriptions();
		prepareUniformBuffers();
		// Create a virtual texture with max. possible dimension (does not take up any VRAM yet)
		prepareSparseTexture(8192, 8192, 1, VK_FORMAT_R8G8B8A8_UNORM);
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

	// Clear all pages of the virtual texture
	// todo: just for testing
	void flushVirtualTexture()
	{
		vkDeviceWaitIdle(mVulkanDevice->mLogicalDevice);
		for (auto& page : texture.pages)
		{
			page.release(mVulkanDevice->mLogicalDevice);
		}
		texture.updateSparseBindInfo();
		vkQueueBindSparse(mQueue, 1, &texture.bindSparseInfo, VK_NULL_HANDLE);
		//todo: use sparse bind semaphore
		vkQueueWaitIdle(mQueue);
		lastFilledMip = texture.mipTailStart - 1;
	}

	// Fill a complete mip level
	void fillVirtualTexture(int32_t &mipLevel)
	{
		vkDeviceWaitIdle(mVulkanDevice->mLogicalDevice);
		std::default_random_engine rndEngine(std::random_device{}());
		std::uniform_real_distribution<float> rndDist(0.0f, 1.0f);
		std::vector<VkImageBlit> imageBlits;
		for (auto& page : texture.pages)
		{
			if ((page.mipLevel == mipLevel) && /*(rndDist(rndEngine) < 0.5f) &&*/ (page.imageMemoryBind.memory == VK_NULL_HANDLE))
			{
				// Allocate page memory
				page.allocate(mVulkanDevice->mLogicalDevice, memoryTypeIndex);

				// Current mip level scaling
				uint32_t scale = texture.width / (texture.width >> page.mipLevel);

				for (uint32_t x = 0; x < scale; x++)
				{
					for (uint32_t y = 0; y < scale; y++)
					{
						// Image blit
						VkImageBlit blit{};
						// Source
						blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
						blit.srcSubresource.baseArrayLayer = 0;
						blit.srcSubresource.layerCount = 1;
						blit.srcSubresource.mipLevel = 0;
						blit.srcOffsets[0] = { 0, 0, 0 };
						blit.srcOffsets[1] = { static_cast<int32_t>(textures.source.width), static_cast<int32_t>(textures.source.height), 1 };
						// Dest
						blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
						blit.dstSubresource.baseArrayLayer = 0;
						blit.dstSubresource.layerCount = 1;
						blit.dstSubresource.mipLevel = page.mipLevel;
						blit.dstOffsets[0].x = static_cast<int32_t>(page.offset.x + x * 128 / scale);
						blit.dstOffsets[0].y = static_cast<int32_t>(page.offset.y + y * 128 / scale);
						blit.dstOffsets[0].z = 0;
						blit.dstOffsets[1].x = static_cast<int32_t>(blit.dstOffsets[0].x + page.extent.width / scale);
						blit.dstOffsets[1].y = static_cast<int32_t>(blit.dstOffsets[0].y + page.extent.height / scale);
						blit.dstOffsets[1].z = 1;

						imageBlits.push_back(blit);
					}
				}
			}
		}

		// Update sparse queue binding
		texture.updateSparseBindInfo();
		vkQueueBindSparse(mQueue, 1, &texture.bindSparseInfo, VK_NULL_HANDLE);
		//todo: use sparse bind semaphore
		vkQueueWaitIdle(mQueue);

		// Issue blit commands
		if (imageBlits.size() > 0)
		{
			auto tStart = std::chrono::high_resolution_clock::now();

			VkCommandBuffer copyCmd = mVulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

			vkCmdBlitImage(
				copyCmd,
				textures.source.image,
				VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				texture.image,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				static_cast<uint32_t>(imageBlits.size()),
				imageBlits.data(),
				VK_FILTER_LINEAR
			);

			mVulkanDevice->flushCommandBuffer(copyCmd, mQueue);

			auto tEnd = std::chrono::high_resolution_clock::now();
			auto tDiff = std::chrono::duration<double, std::milli>(tEnd - tStart).count();
			std::cout << "Image blits took " << tDiff << " ms" << std::endl;
		}

		vkQueueWaitIdle(mQueue);

		mipLevel--;
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
			flushVirtualTexture();
			break;
		case Keyboard::KEY_N:
			if (lastFilledMip >= 0)
			{
				fillVirtualTexture(lastFilledMip);
			}
			break;
		}
	}

	virtual void getOverlayText(VulkanTextOverlay *textOverlay)
	{
		uint32_t respages = 0;
		std::for_each(texture.pages.begin(), texture.pages.end(), [&respages](VirtualTexturePage page) { respages += (page.imageMemoryBind.memory != VK_NULL_HANDLE) ? 1 : 0; });
		std::stringstream ss;
		ss << std::setprecision(2) << std::fixed << uboVS.lodBias;
#if defined(__ANDROID__)
		//		textOverlay->addText("LOD bias: " + ss.str() + " (Buttons L1/R1 to change)", 5.0f, 85.0f, VulkanTextOverlay::alignLeft);
#else
		//textOverlay->addText("LOD bias: " + ss.str() + " (numpad +/- to change)", 5.0f, 85.0f, VulkanTextOverlay::alignLeft);
		textOverlay->addText("Resident pages: " + std::to_string(respages) + " / " + std::to_string(texture.pages.size()), 5.0f, 85.0f, VulkanTextOverlay::alignLeft);
		textOverlay->addText("\"n\" to fill next mip level (" + std::to_string(lastFilledMip) + ")", 5.0f, 100.0f, VulkanTextOverlay::alignLeft);
#endif
	}
};

