#pragma once

#include <vulkan/vulkan.h>
#include <gli/gli.hpp>

#include "VkCoreDevice.hpp"

#if defined(__ANDROID__)
#include <android/asset_manager.h>
#endif

namespace vkTools 
{
	struct VulkanTexture
	{
		VkSampler sampler;
		VkImage image;
		VkImageLayout imageLayout;
		VkDeviceMemory deviceMemory;
		VkImageView view;
		uint32_t width;
		uint32_t height;
		uint32_t mipLevels;
		uint32_t layerCount;
		VkDescriptorImageInfo descriptor;
	};

	/**
	* @brief A simple Vulkan texture uploader for getting images into GPU memory
	*/
	class VulkanTextureLoader
	{
	private:
		VkCoreDevice *vulkanDevice;
		VkQueue queue;
		VkCommandBuffer cmdBuffer;
		VkCommandPool cmdPool;
	public:
#if defined(__ANDROID__)
		AAssetManager* assetManager = nullptr;
#endif

		/**
		* Default constructor
		*
		* @param vulkanDevice Pointer to a valid VulkanDevice
		* @param queue Queue for the copy commands when using staging (queue must support transfers)
		* @param cmdPool Commandpool used to get command buffers for copies and layout transitions
		*/
		VulkanTextureLoader(VkCoreDevice *vulkanDevice, VkQueue queue, VkCommandPool cmdPool)
		{
			this->vulkanDevice = vulkanDevice;
			this->queue = queue;
			this->cmdPool = cmdPool;

			// Create command buffer for submitting image barriers
			// and converting tilings
			VkCommandBufferAllocateInfo cmdBufInfo = {};
			cmdBufInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			cmdBufInfo.commandPool = cmdPool;
			cmdBufInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			cmdBufInfo.commandBufferCount = 1;

			VK_CHECK_RESULT(vkAllocateCommandBuffers(vulkanDevice->mLogicalDevice, &cmdBufInfo, &cmdBuffer));
		}

		/**
		* Default destructor
		*
		* @note Does not free texture resources
		*/
		~VulkanTextureLoader()
		{
			vkFreeCommandBuffers(vulkanDevice->mLogicalDevice, cmdPool, 1, &cmdBuffer);
		}

		/**
		* Load a 2D texture including all mip levels
		*
		* @param filename File to load
		* @param format Vulkan format of the image data stored in the file
		* @param texture Pointer to the texture object to load the image into 
		* @param (Optional) forceLinear Force linear tiling (not advised, defaults to false)
		* @param (Optional) imageUsageFlags Usage flags for the texture's image (defaults to VK_IMAGE_USAGE_SAMPLED_BIT)
		*
		* @note Only supports .ktx and .dds
		*/
		void loadTexture(std::string filename, VkFormat format, VulkanTexture *texture, bool forceLinear = false, VkImageUsageFlags imageUsageFlags = VK_IMAGE_USAGE_SAMPLED_BIT)
		{
#if defined(__ANDROID__)
			assert(assetManager != nullptr);

			// Textures are stored inside the apk on Android (compressed)
			// So they need to be loaded via the asset manager
			AAsset* asset = AAssetManager_open(assetManager, filename.c_str(), AASSET_MODE_STREAMING);
			assert(asset);
			size_t size = AAsset_getLength(asset);
			assert(size > 0);

			void *textureData = malloc(size);
			AAsset_read(asset, textureData, size);
			AAsset_close(asset);

			gli::texture2D tex2D(gli::load((const char*)textureData, size));

			free(textureData);
#else
			gli::texture2D tex2D(gli::load(filename.c_str()));
#endif		
			assert(!tex2D.empty());

			texture->width = static_cast<uint32_t>(tex2D[0].dimensions().x);
			texture->height = static_cast<uint32_t>(tex2D[0].dimensions().y);
			texture->mipLevels = static_cast<uint32_t>(tex2D.levels());

			// Get device properites for the requested texture format
			VkFormatProperties formatProperties;
			vkGetPhysicalDeviceFormatProperties(vulkanDevice->mPhysicalDevice, format, &formatProperties);

			// Only use linear tiling if requested (and supported by the device)
			// Support for linear tiling is mostly limited, so prefer to use
			// optimal tiling instead
			// On most implementations linear tiling will only support a very
			// limited amount of formats and features (mip maps, cubemaps, arrays, etc.)
			VkBool32 useStaging = !forceLinear;

			VkMemoryAllocateInfo memAllocInfo = vkTools::initializers::memoryAllocateInfo();
			VkMemoryRequirements memReqs;

			// Use a separate command buffer for texture loading
			VkCommandBufferBeginInfo cmdBufInfo = vkTools::initializers::commandBufferBeginInfo();
			VK_CHECK_RESULT(vkBeginCommandBuffer(cmdBuffer, &cmdBufInfo));

			if (useStaging)
			{
				// Create a host-visible staging buffer that contains the raw image data
				VkBuffer stagingBuffer;
				VkDeviceMemory stagingMemory;

				VkBufferCreateInfo bufferCreateInfo = vkTools::initializers::bufferCreateInfo();
				bufferCreateInfo.size = tex2D.size();
				// This buffer is used as a transfer source for the buffer copy
				bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
				bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

				VK_CHECK_RESULT(vkCreateBuffer(vulkanDevice->mLogicalDevice, &bufferCreateInfo, nullptr, &stagingBuffer));

				// Get memory requirements for the staging buffer (alignment, memory type bits)
				vkGetBufferMemoryRequirements(vulkanDevice->mLogicalDevice, stagingBuffer, &memReqs);

				memAllocInfo.allocationSize = memReqs.size;
				// Get memory type index for a host visible buffer
				memAllocInfo.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

				VK_CHECK_RESULT(vkAllocateMemory(vulkanDevice->mLogicalDevice, &memAllocInfo, nullptr, &stagingMemory));
				VK_CHECK_RESULT(vkBindBufferMemory(vulkanDevice->mLogicalDevice, stagingBuffer, stagingMemory, 0));

				// Copy texture data into staging buffer
				uint8_t *data;
				VK_CHECK_RESULT(vkMapMemory(vulkanDevice->mLogicalDevice, stagingMemory, 0, memReqs.size, 0, (void **)&data));
				memcpy(data, tex2D.data(), tex2D.size());
				vkUnmapMemory(vulkanDevice->mLogicalDevice, stagingMemory);

				// Setup buffer copy regions for each mip level
				std::vector<VkBufferImageCopy> bufferCopyRegions;
				uint32_t offset = 0;

				for (uint32_t i = 0; i < texture->mipLevels; i++)
				{
					VkBufferImageCopy bufferCopyRegion = {};
					bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
					bufferCopyRegion.imageSubresource.mipLevel = i;
					bufferCopyRegion.imageSubresource.baseArrayLayer = 0;
					bufferCopyRegion.imageSubresource.layerCount = 1;
					bufferCopyRegion.imageExtent.width = static_cast<uint32_t>(tex2D[i].dimensions().x);
					bufferCopyRegion.imageExtent.height = static_cast<uint32_t>(tex2D[i].dimensions().y);
					bufferCopyRegion.imageExtent.depth = 1;
					bufferCopyRegion.bufferOffset = offset;

					bufferCopyRegions.push_back(bufferCopyRegion);

					offset += static_cast<uint32_t>(tex2D[i].size());
				}

				// Create optimal tiled target image
				VkImageCreateInfo imageCreateInfo = vkTools::initializers::imageCreateInfo();
				imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
				imageCreateInfo.format = format;
				imageCreateInfo.mipLevels = texture->mipLevels;
				imageCreateInfo.arrayLayers = 1;
				imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
				imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
				imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
				imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				imageCreateInfo.extent = { texture->width, texture->height, 1 };
				imageCreateInfo.usage = imageUsageFlags;
				// Ensure that the TRANSFER_DST bit is set for staging
				if (!(imageCreateInfo.usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT))
				{
					imageCreateInfo.usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
				}
				VK_CHECK_RESULT(vkCreateImage(vulkanDevice->mLogicalDevice, &imageCreateInfo, nullptr, &texture->image));

				vkGetImageMemoryRequirements(vulkanDevice->mLogicalDevice, texture->image, &memReqs);

				memAllocInfo.allocationSize = memReqs.size;

				memAllocInfo.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
				VK_CHECK_RESULT(vkAllocateMemory(vulkanDevice->mLogicalDevice, &memAllocInfo, nullptr, &texture->deviceMemory));
				VK_CHECK_RESULT(vkBindImageMemory(vulkanDevice->mLogicalDevice, texture->image, texture->deviceMemory, 0));

				VkImageSubresourceRange subresourceRange = {};
				subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				subresourceRange.baseMipLevel = 0;
				subresourceRange.levelCount = texture->mipLevels;
				subresourceRange.layerCount = 1;

				// Image barrier for optimal image (target)
				// Optimal image will be used as destination for the copy
				setImageLayout(
					cmdBuffer,
					texture->image,
					VK_IMAGE_ASPECT_COLOR_BIT,
					VK_IMAGE_LAYOUT_UNDEFINED,
					VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					subresourceRange);

				// Copy mip levels from staging buffer
				vkCmdCopyBufferToImage(
					cmdBuffer,
					stagingBuffer,
					texture->image,
					VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					static_cast<uint32_t>(bufferCopyRegions.size()),
					bufferCopyRegions.data()
					);

				// Change texture image layout to shader read after all mip levels have been copied
				texture->imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				setImageLayout(
					cmdBuffer,
					texture->image,
					VK_IMAGE_ASPECT_COLOR_BIT,
					VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					texture->imageLayout,
					subresourceRange);

				// Submit command buffer containing copy and image layout commands
				VK_CHECK_RESULT(vkEndCommandBuffer(cmdBuffer));

				// Create a fence to make sure that the copies have finished before continuing
				VkFence copyFence;
				VkFenceCreateInfo fenceCreateInfo = vkTools::initializers::fenceCreateInfo(VK_FLAGS_NONE);
				VK_CHECK_RESULT(vkCreateFence(vulkanDevice->mLogicalDevice, &fenceCreateInfo, nullptr, &copyFence));

				VkSubmitInfo submitInfo = vkTools::initializers::submitInfo();
				submitInfo.commandBufferCount = 1;
				submitInfo.pCommandBuffers = &cmdBuffer;

				VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, copyFence));

				VK_CHECK_RESULT(vkWaitForFences(vulkanDevice->mLogicalDevice, 1, &copyFence, VK_TRUE, DEFAULT_FENCE_TIMEOUT));

				vkDestroyFence(vulkanDevice->mLogicalDevice, copyFence, nullptr);

				// Clean up staging resources
				vkFreeMemory(vulkanDevice->mLogicalDevice, stagingMemory, nullptr);
				vkDestroyBuffer(vulkanDevice->mLogicalDevice, stagingBuffer, nullptr);
			}
			else
			{
				// Prefer using optimal tiling, as linear tiling 
				// may support only a small set of features 
				// depending on implementation (e.g. no mip maps, only one layer, etc.)

				// Check if this support is supported for linear tiling
				assert(formatProperties.linearTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);

				VkImage mappableImage;
				VkDeviceMemory mappableMemory;

				VkImageCreateInfo imageCreateInfo = vkTools::initializers::imageCreateInfo();
				imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
				imageCreateInfo.format = format;
				imageCreateInfo.extent = { texture->width, texture->height, 1 };
				imageCreateInfo.mipLevels = 1;
				imageCreateInfo.arrayLayers = 1;
				imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
				imageCreateInfo.tiling = VK_IMAGE_TILING_LINEAR;
				imageCreateInfo.usage = imageUsageFlags;
				imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
				imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;

				// Load mip map level 0 to linear tiling image
				VK_CHECK_RESULT(vkCreateImage(vulkanDevice->mLogicalDevice, &imageCreateInfo, nullptr, &mappableImage));

				// Get memory requirements for this image 
				// like size and alignment
				vkGetImageMemoryRequirements(vulkanDevice->mLogicalDevice, mappableImage, &memReqs);
				// Set memory allocation size to required memory size
				memAllocInfo.allocationSize = memReqs.size;

				// Get memory type that can be mapped to host memory
				memAllocInfo.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

				// Allocate host memory
				VK_CHECK_RESULT(vkAllocateMemory(vulkanDevice->mLogicalDevice, &memAllocInfo, nullptr, &mappableMemory));

				// Bind allocated image for use
				VK_CHECK_RESULT(vkBindImageMemory(vulkanDevice->mLogicalDevice, mappableImage, mappableMemory, 0));

				// Get sub resource layout
				// Mip map count, array layer, etc.
				VkImageSubresource subRes = {};
				subRes.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				subRes.mipLevel = 0;

				VkSubresourceLayout subResLayout;
				void *data;

				// Get sub resources layout 
				// Includes row pitch, size offsets, etc.
				vkGetImageSubresourceLayout(vulkanDevice->mLogicalDevice, mappableImage, &subRes, &subResLayout);

				// Map image memory
				VK_CHECK_RESULT(vkMapMemory(vulkanDevice->mLogicalDevice, mappableMemory, 0, memReqs.size, 0, &data));

				// Copy image data into memory
				memcpy(data, tex2D[subRes.mipLevel].data(), tex2D[subRes.mipLevel].size());

				vkUnmapMemory(vulkanDevice->mLogicalDevice, mappableMemory);

				// Linear tiled images don't need to be staged
				// and can be directly used as textures
				texture->image = mappableImage;
				texture->deviceMemory = mappableMemory;
				texture->imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

				// Setup image memory barrier
				setImageLayout(
					cmdBuffer,
					texture->image, 
					VK_IMAGE_ASPECT_COLOR_BIT, 
					VK_IMAGE_LAYOUT_PREINITIALIZED, 
					texture->imageLayout);

				// Submit command buffer containing copy and image layout commands
				VK_CHECK_RESULT(vkEndCommandBuffer(cmdBuffer));

				VkFence nullFence = { VK_NULL_HANDLE };

				VkSubmitInfo submitInfo = vkTools::initializers::submitInfo();
				submitInfo.waitSemaphoreCount = 0;
				submitInfo.commandBufferCount = 1;
				submitInfo.pCommandBuffers = &cmdBuffer;

				VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, nullFence));
				VK_CHECK_RESULT(vkQueueWaitIdle(queue));
			}

			// Create sampler
			VkSamplerCreateInfo sampler = {};
			sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
			sampler.magFilter = VK_FILTER_LINEAR;
			sampler.minFilter = VK_FILTER_LINEAR;
			sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
			sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
			sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
			sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
			sampler.mipLodBias = 0.0f;
			sampler.compareOp = VK_COMPARE_OP_NEVER;
			sampler.minLod = 0.0f;
			// Max level-of-detail should match mip level count
			sampler.maxLod = (useStaging) ? (float)texture->mipLevels : 0.0f;
			// Enable anisotropic filtering
			sampler.maxAnisotropy = 8;
			sampler.anisotropyEnable = VK_TRUE;
			sampler.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
			VK_CHECK_RESULT(vkCreateSampler(vulkanDevice->mLogicalDevice, &sampler, nullptr, &texture->sampler));
			
			// Create image view
			// Textures are not directly accessed by the shaders and
			// are abstracted by image views containing additional
			// information and sub resource ranges
			VkImageViewCreateInfo view = {};
			view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			view.pNext = NULL;
			view.image = VK_NULL_HANDLE;
			view.viewType = VK_IMAGE_VIEW_TYPE_2D;
			view.format = format;
			view.components = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A };
			view.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
			// Linear tiling usually won't support mip maps
			// Only set mip map count if optimal tiling is used
			view.subresourceRange.levelCount = (useStaging) ? texture->mipLevels : 1;
			view.image = texture->image;
			VK_CHECK_RESULT(vkCreateImageView(vulkanDevice->mLogicalDevice, &view, nullptr, &texture->view));

			// Fill descriptor image info that can be used for setting up descriptor sets
			texture->descriptor.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
			texture->descriptor.imageView = texture->view;
			texture->descriptor.sampler = texture->sampler;
		}

		/**
		* Load a cubemap texture including all mip levels from a single file
		*
		* @param filename File to load
		* @param format Vulkan format of the image data stored in the file
		* @param texture Pointer to the texture object to load the image into
		*
		* @note Only supports .ktx and .dds
		*/
		void loadCubemap(std::string filename, VkFormat format, VulkanTexture *texture, VkImageUsageFlags imageUsageFlags = VK_IMAGE_USAGE_SAMPLED_BIT)
		{
#if defined(__ANDROID__)
			assert(assetManager != nullptr);

			// Textures are stored inside the apk on Android (compressed)
			// So they need to be loaded via the asset manager
			AAsset* asset = AAssetManager_open(assetManager, filename.c_str(), AASSET_MODE_STREAMING);
			assert(asset);
			size_t size = AAsset_getLength(asset);
			assert(size > 0);

			void *textureData = malloc(size);
			AAsset_read(asset, textureData, size);
			AAsset_close(asset);

			gli::textureCube texCube(gli::load((const char*)textureData, size));

			free(textureData);
#else
			gli::textureCube texCube(gli::load(filename));
#endif	
			assert(!texCube.empty());

			texture->width = static_cast<uint32_t>(texCube.dimensions().x);
			texture->height = static_cast<uint32_t>(texCube.dimensions().y);
			texture->mipLevels = static_cast<uint32_t>(texCube.levels());

			VkMemoryAllocateInfo memAllocInfo = vkTools::initializers::memoryAllocateInfo();
			VkMemoryRequirements memReqs;

			// Create a host-visible staging buffer that contains the raw image data
			VkBuffer stagingBuffer;
			VkDeviceMemory stagingMemory;

			VkBufferCreateInfo bufferCreateInfo = vkTools::initializers::bufferCreateInfo();
			bufferCreateInfo.size = texCube.size();
			// This buffer is used as a transfer source for the buffer copy
			bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
			bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

			VK_CHECK_RESULT(vkCreateBuffer(vulkanDevice->mLogicalDevice, &bufferCreateInfo, nullptr, &stagingBuffer));

			// Get memory requirements for the staging buffer (alignment, memory type bits)
			vkGetBufferMemoryRequirements(vulkanDevice->mLogicalDevice, stagingBuffer, &memReqs);

			memAllocInfo.allocationSize = memReqs.size;
			// Get memory type index for a host visible buffer
			memAllocInfo.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

			VK_CHECK_RESULT(vkAllocateMemory(vulkanDevice->mLogicalDevice, &memAllocInfo, nullptr, &stagingMemory));
			VK_CHECK_RESULT(vkBindBufferMemory(vulkanDevice->mLogicalDevice, stagingBuffer, stagingMemory, 0));

			// Copy texture data into staging buffer
			uint8_t *data;
			VK_CHECK_RESULT(vkMapMemory(vulkanDevice->mLogicalDevice, stagingMemory, 0, memReqs.size, 0, (void **)&data));
			memcpy(data, texCube.data(), texCube.size());
			vkUnmapMemory(vulkanDevice->mLogicalDevice, stagingMemory);

			// Setup buffer copy regions for each face including all of it's miplevels
			std::vector<VkBufferImageCopy> bufferCopyRegions;
			size_t offset = 0;

			for (uint32_t face = 0; face < 6; face++)
			{
				for (uint32_t level = 0; level < texture->mipLevels; level++)
				{
					VkBufferImageCopy bufferCopyRegion = {};
					bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
					bufferCopyRegion.imageSubresource.mipLevel = level;
					bufferCopyRegion.imageSubresource.baseArrayLayer = face;
					bufferCopyRegion.imageSubresource.layerCount = 1;
					bufferCopyRegion.imageExtent.width = static_cast<uint32_t>(texCube[face][level].dimensions().x);
					bufferCopyRegion.imageExtent.height = static_cast<uint32_t>(texCube[face][level].dimensions().y);
					bufferCopyRegion.imageExtent.depth = 1;
					bufferCopyRegion.bufferOffset = offset;

					bufferCopyRegions.push_back(bufferCopyRegion);

					// Increase offset into staging buffer for next level / face
					offset += texCube[face][level].size();
				}
			}

			// Create optimal tiled target image
			VkImageCreateInfo imageCreateInfo = vkTools::initializers::imageCreateInfo();
			imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
			imageCreateInfo.format = format;
			imageCreateInfo.mipLevels = texture->mipLevels;
			imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
			imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
			imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			imageCreateInfo.extent = { texture->width, texture->height, 1 };
			imageCreateInfo.usage = imageUsageFlags;
			// Ensure that the TRANSFER_DST bit is set for staging
			if (!(imageCreateInfo.usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT))
			{
				imageCreateInfo.usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
			}
			// Cube faces count as array layers in Vulkan
			imageCreateInfo.arrayLayers = 6;
			// This flag is required for cube map images
			imageCreateInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;


			VK_CHECK_RESULT(vkCreateImage(vulkanDevice->mLogicalDevice, &imageCreateInfo, nullptr, &texture->image));

			vkGetImageMemoryRequirements(vulkanDevice->mLogicalDevice, texture->image, &memReqs);

			memAllocInfo.allocationSize = memReqs.size;
			memAllocInfo.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

			VK_CHECK_RESULT(vkAllocateMemory(vulkanDevice->mLogicalDevice, &memAllocInfo, nullptr, &texture->deviceMemory));
			VK_CHECK_RESULT(vkBindImageMemory(vulkanDevice->mLogicalDevice, texture->image, texture->deviceMemory, 0));

			VkCommandBufferBeginInfo cmdBufInfo = vkTools::initializers::commandBufferBeginInfo();
			VK_CHECK_RESULT(vkBeginCommandBuffer(cmdBuffer, &cmdBufInfo));

			// Image barrier for optimal image (target)
			// Set initial layout for all array layers (faces) of the optimal (target) tiled texture
			VkImageSubresourceRange subresourceRange = {};
			subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			subresourceRange.baseMipLevel = 0;
			subresourceRange.levelCount = texture->mipLevels;
			subresourceRange.layerCount = 6;

			vkTools::setImageLayout(
				cmdBuffer,
				texture->image,
				VK_IMAGE_ASPECT_COLOR_BIT,
				VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				subresourceRange);

			// Copy the cube map faces from the staging buffer to the optimal tiled image
			vkCmdCopyBufferToImage(
				cmdBuffer,
				stagingBuffer,
				texture->image,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				static_cast<uint32_t>(bufferCopyRegions.size()),
				bufferCopyRegions.data());

			// Change texture image layout to shader read after all faces have been copied
			texture->imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			vkTools::setImageLayout(
				cmdBuffer,
				texture->image,
				VK_IMAGE_ASPECT_COLOR_BIT,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				texture->imageLayout,
				subresourceRange);

			VK_CHECK_RESULT(vkEndCommandBuffer(cmdBuffer));

			// Create a fence to make sure that the copies have finished before continuing
			VkFence copyFence;
			VkFenceCreateInfo fenceCreateInfo = vkTools::initializers::fenceCreateInfo(VK_FLAGS_NONE);
			VK_CHECK_RESULT(vkCreateFence(vulkanDevice->mLogicalDevice, &fenceCreateInfo, nullptr, &copyFence));

			VkSubmitInfo submitInfo = vkTools::initializers::submitInfo();
			submitInfo.commandBufferCount = 1;
			submitInfo.pCommandBuffers = &cmdBuffer;

			VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, copyFence));

			VK_CHECK_RESULT(vkWaitForFences(vulkanDevice->mLogicalDevice, 1, &copyFence, VK_TRUE, DEFAULT_FENCE_TIMEOUT));

			vkDestroyFence(vulkanDevice->mLogicalDevice, copyFence, nullptr);

			// Create sampler
			VkSamplerCreateInfo sampler = vkTools::initializers::samplerCreateInfo();
			sampler.magFilter = VK_FILTER_LINEAR;
			sampler.minFilter = VK_FILTER_LINEAR;
			sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
			sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			sampler.addressModeV = sampler.addressModeU;
			sampler.addressModeW = sampler.addressModeU;
			sampler.mipLodBias = 0.0f;
			sampler.maxAnisotropy = 8;
			sampler.compareOp = VK_COMPARE_OP_NEVER;
			sampler.minLod = 0.0f;
			sampler.maxLod = (float)texture->mipLevels;
			sampler.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
			VK_CHECK_RESULT(vkCreateSampler(vulkanDevice->mLogicalDevice, &sampler, nullptr, &texture->sampler));

			// Create image view
			VkImageViewCreateInfo view = vkTools::initializers::imageViewCreateInfo();
			view.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
			view.format = format;
			view.components = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A };
			view.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
			view.subresourceRange.layerCount = 6;
			view.subresourceRange.levelCount = texture->mipLevels;
			view.image = texture->image;
			VK_CHECK_RESULT(vkCreateImageView(vulkanDevice->mLogicalDevice, &view, nullptr, &texture->view));

			// Clean up staging resources
			vkFreeMemory(vulkanDevice->mLogicalDevice, stagingMemory, nullptr);
			vkDestroyBuffer(vulkanDevice->mLogicalDevice, stagingBuffer, nullptr);

			// Fill descriptor image info that can be used for setting up descriptor sets
			texture->descriptor.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
			texture->descriptor.imageView = texture->view;
			texture->descriptor.sampler = texture->sampler;
		}

		/**
		* Load a texture array including all mip levels from a single file
		*
		* @param filename File to load
		* @param format Vulkan format of the image data stored in the file
		* @param texture Pointer to the texture object to load the image into
		*
		* @note Only supports .ktx and .dds
		*/
		void loadTextureArray(std::string filename, VkFormat format, VulkanTexture *texture, VkImageUsageFlags imageUsageFlags = VK_IMAGE_USAGE_SAMPLED_BIT)
		{
#if defined(__ANDROID__)
			assert(assetManager != nullptr);

			// Textures are stored inside the apk on Android (compressed)
			// So they need to be loaded via the asset manager
			AAsset* asset = AAssetManager_open(assetManager, filename.c_str(), AASSET_MODE_STREAMING);
			assert(asset);
			size_t size = AAsset_getLength(asset);
			assert(size > 0);

			void *textureData = malloc(size);
			AAsset_read(asset, textureData, size);
			AAsset_close(asset);

			gli::texture2DArray tex2DArray(gli::load((const char*)textureData, size));

			free(textureData);
#else
			gli::texture2DArray tex2DArray(gli::load(filename));
#endif	

			assert(!tex2DArray.empty());

			texture->width = static_cast<uint32_t>(tex2DArray.dimensions().x);
			texture->height = static_cast<uint32_t>(tex2DArray.dimensions().y);
			texture->layerCount = static_cast<uint32_t>(tex2DArray.layers());
			texture->mipLevels = static_cast<uint32_t>(tex2DArray.levels());

			VkMemoryAllocateInfo memAllocInfo = vkTools::initializers::memoryAllocateInfo();
			VkMemoryRequirements memReqs;

			// Create a host-visible staging buffer that contains the raw image data
			VkBuffer stagingBuffer;
			VkDeviceMemory stagingMemory;

			VkBufferCreateInfo bufferCreateInfo = vkTools::initializers::bufferCreateInfo();
			bufferCreateInfo.size = tex2DArray.size();
			// This buffer is used as a transfer source for the buffer copy
			bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
			bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

			VK_CHECK_RESULT(vkCreateBuffer(vulkanDevice->mLogicalDevice, &bufferCreateInfo, nullptr, &stagingBuffer));

			// Get memory requirements for the staging buffer (alignment, memory type bits)
			vkGetBufferMemoryRequirements(vulkanDevice->mLogicalDevice, stagingBuffer, &memReqs);

			memAllocInfo.allocationSize = memReqs.size;
			// Get memory type index for a host visible buffer
			memAllocInfo.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

			VK_CHECK_RESULT(vkAllocateMemory(vulkanDevice->mLogicalDevice, &memAllocInfo, nullptr, &stagingMemory));
			VK_CHECK_RESULT(vkBindBufferMemory(vulkanDevice->mLogicalDevice, stagingBuffer, stagingMemory, 0));

			// Copy texture data into staging buffer
			uint8_t *data;
			VK_CHECK_RESULT(vkMapMemory(vulkanDevice->mLogicalDevice, stagingMemory, 0, memReqs.size, 0, (void **)&data));
			memcpy(data, tex2DArray.data(), static_cast<size_t>(tex2DArray.size()));
			vkUnmapMemory(vulkanDevice->mLogicalDevice, stagingMemory);

			// Setup buffer copy regions for each layer including all of it's miplevels
			std::vector<VkBufferImageCopy> bufferCopyRegions;
			size_t offset = 0;

			for (uint32_t layer = 0; layer < texture->layerCount; layer++)
			{
				for (uint32_t level = 0; level < texture->mipLevels; level++)
				{
					VkBufferImageCopy bufferCopyRegion = {};
					bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
					bufferCopyRegion.imageSubresource.mipLevel = level;
					bufferCopyRegion.imageSubresource.baseArrayLayer = layer;
					bufferCopyRegion.imageSubresource.layerCount = 1;
					bufferCopyRegion.imageExtent.width = static_cast<uint32_t>(tex2DArray[layer][level].dimensions().x);
					bufferCopyRegion.imageExtent.height = static_cast<uint32_t>(tex2DArray[layer][level].dimensions().y);
					bufferCopyRegion.imageExtent.depth = 1;
					bufferCopyRegion.bufferOffset = offset;

					bufferCopyRegions.push_back(bufferCopyRegion);

					// Increase offset into staging buffer for next level / face
					offset += tex2DArray[layer][level].size();
				}
			}

			// Create optimal tiled target image
			VkImageCreateInfo imageCreateInfo = vkTools::initializers::imageCreateInfo();
			imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
			imageCreateInfo.format = format;
			imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
			imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
			imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			imageCreateInfo.extent = { texture->width, texture->height, 1 };
			imageCreateInfo.usage = imageUsageFlags;
			// Ensure that the TRANSFER_DST bit is set for staging
			if (!(imageCreateInfo.usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT))
			{
				imageCreateInfo.usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
			}
			imageCreateInfo.arrayLayers = texture->layerCount;
			imageCreateInfo.mipLevels = texture->mipLevels;

			VK_CHECK_RESULT(vkCreateImage(vulkanDevice->mLogicalDevice, &imageCreateInfo, nullptr, &texture->image));

			vkGetImageMemoryRequirements(vulkanDevice->mLogicalDevice, texture->image, &memReqs);

			memAllocInfo.allocationSize = memReqs.size;
			memAllocInfo.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

			VK_CHECK_RESULT(vkAllocateMemory(vulkanDevice->mLogicalDevice, &memAllocInfo, nullptr, &texture->deviceMemory));
			VK_CHECK_RESULT(vkBindImageMemory(vulkanDevice->mLogicalDevice, texture->image, texture->deviceMemory, 0));

			VkCommandBufferBeginInfo cmdBufInfo = vkTools::initializers::commandBufferBeginInfo();
			VK_CHECK_RESULT(vkBeginCommandBuffer(cmdBuffer, &cmdBufInfo));

			// Image barrier for optimal image (target)
			// Set initial layout for all array layers (faces) of the optimal (target) tiled texture
			VkImageSubresourceRange subresourceRange = {};
			subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			subresourceRange.baseMipLevel = 0;
			subresourceRange.levelCount = texture->mipLevels;
			subresourceRange.layerCount = texture->layerCount;

			vkTools::setImageLayout(
				cmdBuffer,
				texture->image,
				VK_IMAGE_ASPECT_COLOR_BIT,
				VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				subresourceRange);

			// Copy the layers and mip levels from the staging buffer to the optimal tiled image
			vkCmdCopyBufferToImage(
				cmdBuffer,
				stagingBuffer,
				texture->image,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				static_cast<uint32_t>(bufferCopyRegions.size()),
				bufferCopyRegions.data());

			// Change texture image layout to shader read after all faces have been copied
			texture->imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			vkTools::setImageLayout(
				cmdBuffer,
				texture->image,
				VK_IMAGE_ASPECT_COLOR_BIT,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				texture->imageLayout,
				subresourceRange);

			VK_CHECK_RESULT(vkEndCommandBuffer(cmdBuffer));

			// Create a fence to make sure that the copies have finished before continuing
			VkFence copyFence;
			VkFenceCreateInfo fenceCreateInfo = vkTools::initializers::fenceCreateInfo(VK_FLAGS_NONE);
			VK_CHECK_RESULT(vkCreateFence(vulkanDevice->mLogicalDevice, &fenceCreateInfo, nullptr, &copyFence));

			VkSubmitInfo submitInfo = vkTools::initializers::submitInfo();
			submitInfo.commandBufferCount = 1;
			submitInfo.pCommandBuffers = &cmdBuffer;

			VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, copyFence));

			VK_CHECK_RESULT(vkWaitForFences(vulkanDevice->mLogicalDevice, 1, &copyFence, VK_TRUE, DEFAULT_FENCE_TIMEOUT));

			vkDestroyFence(vulkanDevice->mLogicalDevice, copyFence, nullptr);

			// Create sampler
			VkSamplerCreateInfo sampler = vkTools::initializers::samplerCreateInfo();
			sampler.magFilter = VK_FILTER_LINEAR;
			sampler.minFilter = VK_FILTER_LINEAR;
			sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
			sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			sampler.addressModeV = sampler.addressModeU;
			sampler.addressModeW = sampler.addressModeU;
			sampler.mipLodBias = 0.0f;
			sampler.maxAnisotropy = 8;
			sampler.compareOp = VK_COMPARE_OP_NEVER;
			sampler.minLod = 0.0f;
			sampler.maxLod = (float)texture->mipLevels;
			sampler.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
			VK_CHECK_RESULT(vkCreateSampler(vulkanDevice->mLogicalDevice, &sampler, nullptr, &texture->sampler));

			// Create image view
			VkImageViewCreateInfo view = vkTools::initializers::imageViewCreateInfo();
			view.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
			view.format = format;
			view.components = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A };
			view.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
			view.subresourceRange.layerCount = texture->layerCount;
			view.subresourceRange.levelCount = texture->mipLevels;
			view.image = texture->image;
			VK_CHECK_RESULT(vkCreateImageView(vulkanDevice->mLogicalDevice, &view, nullptr, &texture->view));

			// Clean up staging resources
			vkFreeMemory(vulkanDevice->mLogicalDevice, stagingMemory, nullptr);
			vkDestroyBuffer(vulkanDevice->mLogicalDevice, stagingBuffer, nullptr);

			// Fill descriptor image info that can be used for setting up descriptor sets
			texture->descriptor.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
			texture->descriptor.imageView = texture->view;
			texture->descriptor.sampler = texture->sampler;
		}

		/**
		* Creates a 2D texture from a buffer
		*
		* @param buffer Buffer containing texture data to upload
		* @param bufferSize Size of the buffer in machine units
		* @param width Width of the texture to create
		* @param hidth Height of the texture to create
		* @param format Vulkan format of the image data stored in the file
		* @param texture Pointer to the texture object to load the image into
		* @param (Optional) filter Texture filtering for the sampler (defaults to VK_FILTER_LINEAR)
		* @param (Optional) imageUsageFlags Usage flags for the texture's image (defaults to VK_IMAGE_USAGE_SAMPLED_BIT)
		*/
		void createTexture(void* buffer, VkDeviceSize bufferSize, VkFormat format, uint32_t width, uint32_t height, VulkanTexture *texture, VkFilter filter = VK_FILTER_LINEAR, VkImageUsageFlags imageUsageFlags = VK_IMAGE_USAGE_SAMPLED_BIT)
		{
			assert(buffer);

			texture->width = width;
			texture->height = height;
			texture->mipLevels = 1;

			VkMemoryAllocateInfo memAllocInfo = vkTools::initializers::memoryAllocateInfo();
			VkMemoryRequirements memReqs;

			// Use a separate command buffer for texture loading
			VkCommandBufferBeginInfo cmdBufInfo = vkTools::initializers::commandBufferBeginInfo();
			VK_CHECK_RESULT(vkBeginCommandBuffer(cmdBuffer, &cmdBufInfo));

			// Create a host-visible staging buffer that contains the raw image data
			VkBuffer stagingBuffer;
			VkDeviceMemory stagingMemory;

			VkBufferCreateInfo bufferCreateInfo = vkTools::initializers::bufferCreateInfo();
			bufferCreateInfo.size = bufferSize;
			// This buffer is used as a transfer source for the buffer copy
			bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
			bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

			VK_CHECK_RESULT(vkCreateBuffer(vulkanDevice->mLogicalDevice, &bufferCreateInfo, nullptr, &stagingBuffer));

			// Get memory requirements for the staging buffer (alignment, memory type bits)
			vkGetBufferMemoryRequirements(vulkanDevice->mLogicalDevice, stagingBuffer, &memReqs);

			memAllocInfo.allocationSize = memReqs.size;
			// Get memory type index for a host visible buffer
			memAllocInfo.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

			VK_CHECK_RESULT(vkAllocateMemory(vulkanDevice->mLogicalDevice, &memAllocInfo, nullptr, &stagingMemory));
			VK_CHECK_RESULT(vkBindBufferMemory(vulkanDevice->mLogicalDevice, stagingBuffer, stagingMemory, 0));

			// Copy texture data into staging buffer
			uint8_t *data;
			VK_CHECK_RESULT(vkMapMemory(vulkanDevice->mLogicalDevice, stagingMemory, 0, memReqs.size, 0, (void **)&data));
			memcpy(data, buffer, bufferSize);
			vkUnmapMemory(vulkanDevice->mLogicalDevice, stagingMemory);

			VkBufferImageCopy bufferCopyRegion = {};
			bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			bufferCopyRegion.imageSubresource.mipLevel = 0;
			bufferCopyRegion.imageSubresource.baseArrayLayer = 0;
			bufferCopyRegion.imageSubresource.layerCount = 1;
			bufferCopyRegion.imageExtent.width = width;
			bufferCopyRegion.imageExtent.height = height;
			bufferCopyRegion.imageExtent.depth = 1;
			bufferCopyRegion.bufferOffset = 0;

			// Create optimal tiled target image
			VkImageCreateInfo imageCreateInfo = vkTools::initializers::imageCreateInfo();
			imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
			imageCreateInfo.format = format;
			imageCreateInfo.mipLevels = texture->mipLevels;
			imageCreateInfo.arrayLayers = 1;
			imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
			imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
			imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			imageCreateInfo.extent = { texture->width, texture->height, 1 };
			imageCreateInfo.usage = imageUsageFlags;
			// Ensure that the TRANSFER_DST bit is set for staging
			if (!(imageCreateInfo.usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT))
			{
				imageCreateInfo.usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
			}
			VK_CHECK_RESULT(vkCreateImage(vulkanDevice->mLogicalDevice, &imageCreateInfo, nullptr, &texture->image));

			vkGetImageMemoryRequirements(vulkanDevice->mLogicalDevice, texture->image, &memReqs);

			memAllocInfo.allocationSize = memReqs.size;

			memAllocInfo.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			VK_CHECK_RESULT(vkAllocateMemory(vulkanDevice->mLogicalDevice, &memAllocInfo, nullptr, &texture->deviceMemory));
			VK_CHECK_RESULT(vkBindImageMemory(vulkanDevice->mLogicalDevice, texture->image, texture->deviceMemory, 0));

			VkImageSubresourceRange subresourceRange = {};
			subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			subresourceRange.baseMipLevel = 0;
			subresourceRange.levelCount = texture->mipLevels;
			subresourceRange.layerCount = 1;

			// Image barrier for optimal image (target)
			// Optimal image will be used as destination for the copy
			setImageLayout(
				cmdBuffer,
				texture->image,
				VK_IMAGE_ASPECT_COLOR_BIT,
				VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				subresourceRange);

			// Copy mip levels from staging buffer
			vkCmdCopyBufferToImage(
				cmdBuffer,
				stagingBuffer,
				texture->image,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				1,
				&bufferCopyRegion
			);

			// Change texture image layout to shader read after all mip levels have been copied
			texture->imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			setImageLayout(
				cmdBuffer,
				texture->image,
				VK_IMAGE_ASPECT_COLOR_BIT,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				texture->imageLayout,
				subresourceRange);

			// Submit command buffer containing copy and image layout commands
			VK_CHECK_RESULT(vkEndCommandBuffer(cmdBuffer));

			// Create a fence to make sure that the copies have finished before continuing
			VkFence copyFence;
			VkFenceCreateInfo fenceCreateInfo = vkTools::initializers::fenceCreateInfo(VK_FLAGS_NONE);
			VK_CHECK_RESULT(vkCreateFence(vulkanDevice->mLogicalDevice, &fenceCreateInfo, nullptr, &copyFence));

			VkSubmitInfo submitInfo = vkTools::initializers::submitInfo();
			submitInfo.commandBufferCount = 1;
			submitInfo.pCommandBuffers = &cmdBuffer;

			VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, copyFence));

			VK_CHECK_RESULT(vkWaitForFences(vulkanDevice->mLogicalDevice, 1, &copyFence, VK_TRUE, DEFAULT_FENCE_TIMEOUT));

			vkDestroyFence(vulkanDevice->mLogicalDevice, copyFence, nullptr);

			// Clean up staging resources
			vkFreeMemory(vulkanDevice->mLogicalDevice, stagingMemory, nullptr);
			vkDestroyBuffer(vulkanDevice->mLogicalDevice, stagingBuffer, nullptr);

			// Create sampler
			VkSamplerCreateInfo sampler = {};
			sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
			sampler.magFilter = filter;
			sampler.minFilter = filter;
			sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
			sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
			sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
			sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
			sampler.mipLodBias = 0.0f;
			sampler.compareOp = VK_COMPARE_OP_NEVER;
			sampler.minLod = 0.0f;
			sampler.maxLod = 0.0f;
			VK_CHECK_RESULT(vkCreateSampler(vulkanDevice->mLogicalDevice, &sampler, nullptr, &texture->sampler));

			// Create image view
			VkImageViewCreateInfo view = {};
			view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			view.pNext = NULL;
			view.image = VK_NULL_HANDLE;
			view.viewType = VK_IMAGE_VIEW_TYPE_2D;
			view.format = format;
			view.components = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A };
			view.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
			view.subresourceRange.levelCount = 1;
			view.image = texture->image;
			VK_CHECK_RESULT(vkCreateImageView(vulkanDevice->mLogicalDevice, &view, nullptr, &texture->view));

			// Fill descriptor image info that can be used for setting up descriptor sets
			texture->descriptor.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
			texture->descriptor.imageView = texture->view;
			texture->descriptor.sampler = texture->sampler;
		}

		/**
		* Free all Vulkan resources used by a texture object
		*
		* @param texture Texture object whose resources are to be freed
		*/
		void destroyTexture(VulkanTexture texture)
		{
			vkDestroyImageView(vulkanDevice->mLogicalDevice, texture.view, nullptr);
			vkDestroyImage(vulkanDevice->mLogicalDevice, texture.image, nullptr);
			vkDestroySampler(vulkanDevice->mLogicalDevice, texture.sampler, nullptr);
			vkFreeMemory(vulkanDevice->mLogicalDevice, texture.deviceMemory, nullptr);
		}
	};
};
