#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <vector>
#include <algorithm>

#include "define.h"
#include <gli/gli.hpp>

#include <vulkan/vulkan.h>
#include "VulkanBase.h"
#include "frustum.hpp"

#define ENABLE_VALIDATION false


class VkTerraintessellation : public VulkanBase
{
	// Vertex layout for this example
	std::vector<vkMeshLoader::VertexLayout> vertexLayout =
	{
		vkMeshLoader::VERTEX_LAYOUT_POSITION,
		vkMeshLoader::VERTEX_LAYOUT_NORMAL,
		vkMeshLoader::VERTEX_LAYOUT_UV
	};
private:
	struct {
		vkTools::VulkanTexture heightMap;
		vkTools::VulkanTexture skySphere;
		vkTools::VulkanTexture terrainArray;
	} textures;
public:
	bool wireframe = false;
	bool tessellation = true;

	struct {
		VkPipelineVertexInputStateCreateInfo inputState;
		std::vector<VkVertexInputBindingDescription> bindingDescriptions;
		std::vector<VkVertexInputAttributeDescription> attributeDescriptions;
	} vertices;

	struct {
		vkMeshLoader::MeshBuffer terrain;
		vkMeshLoader::MeshBuffer skysphere;
	} meshes;

	struct {
		vkTools::UniformData terrainTessellation;
		vkTools::UniformData skysphereVertex;
	} uniformData;

	// Shared values for tessellation control and evaluation stages
	struct {
		Matrix projection;
		Matrix modelview;
		Vector4 lightPos = Vector4(-48.0f, -40.0f, 46.0f, 0.0f);
		Vector4 frustumPlanes[6];
		float displacementFactor = 32.0f;
		float tessellationFactor = 0.75f;
		Vector2 viewportDim;
		// Desired size of tessellated quad patch edge
		float tessellatedEdgeSize = 20.0f;
	} uboTess;

	// Skysphere vertex shader stage
	struct {
		Matrix mvp;
	} uboVS;

	struct {
		VkPipeline terrain;
		VkPipeline wireframe;
		VkPipeline skysphere;
	} pipelines;

	struct {
		VkDescriptorSetLayout terrain;
		VkDescriptorSetLayout skysphere;
	} descriptorSetLayouts;

	struct {
		VkPipelineLayout terrain;
		VkPipelineLayout skysphere;
	} pipelineLayouts;

	struct {
		VkDescriptorSet terrain;
		VkDescriptorSet skysphere;
	} descriptorSets;

	// Pipeline statistics
	struct {
		VkBuffer buffer;
		VkDeviceMemory memory;
	} queryResult;
	VkQueryPool queryPool;
	uint64_t pipelineStats[2] = { 0 };

	// View frustum passed to tessellation control shader for culling
	vkTools::Frustum frustum;

	VkTerraintessellation() : VulkanBase(ENABLE_VALIDATION)
	{
		mEnableTextOverlay = true;
		title = "Dynamic terrain tessellation";
		mCamera.type = VkCamera::CameraType::firstperson;
		mCamera.setPerspective(60.0f, (float)width / (float)height, 0.1f, 512.0f);
		mCamera.setRotation(Vector3(-12.0f, 159.0f, 0.0f));
		mCamera.setTranslation(Vector3(18.0f, 22.5f, 57.5f));

		mCamera.movementSpeed = 7.5f;
		// Support for tessellation shaders is optional, so check first
		//if (!deviceFeatures.tessellationShader)
		//{
		//	vkTools::exitFatal("Selected GPU does not support tessellation shaders!", "Feature not supported");
		//}
	}

	~VkTerraintessellation()
	{
		// Clean up used Vulkan resources 
		// Note : Inherited destructor cleans up resources stored in base class
		vkDestroyPipeline(mVulkanDevice->mLogicalDevice, pipelines.terrain, nullptr);
		vkDestroyPipeline(mVulkanDevice->mLogicalDevice, pipelines.wireframe, nullptr);
		vkDestroyPipeline(mVulkanDevice->mLogicalDevice, pipelines.skysphere, nullptr);

		vkDestroyPipelineLayout(mVulkanDevice->mLogicalDevice, pipelineLayouts.skysphere, nullptr);
		vkDestroyPipelineLayout(mVulkanDevice->mLogicalDevice, pipelineLayouts.terrain, nullptr);

		vkDestroyDescriptorSetLayout(mVulkanDevice->mLogicalDevice, descriptorSetLayouts.terrain, nullptr);
		vkDestroyDescriptorSetLayout(mVulkanDevice->mLogicalDevice, descriptorSetLayouts.skysphere, nullptr);

		vkMeshLoader::freeMeshBufferResources(mVulkanDevice->mLogicalDevice, &meshes.terrain);
		vkMeshLoader::freeMeshBufferResources(mVulkanDevice->mLogicalDevice, &meshes.skysphere);

		vkDestroyBuffer(mVulkanDevice->mLogicalDevice, uniformData.terrainTessellation.buffer, nullptr);
		vkFreeMemory(mVulkanDevice->mLogicalDevice, uniformData.terrainTessellation.memory, nullptr);

		vkDestroyBuffer(mVulkanDevice->mLogicalDevice, uniformData.skysphereVertex.buffer, nullptr);
		vkFreeMemory(mVulkanDevice->mLogicalDevice, uniformData.skysphereVertex.memory, nullptr);

		textureLoader->destroyTexture(textures.heightMap);
		textureLoader->destroyTexture(textures.skySphere);
		textureLoader->destroyTexture(textures.terrainArray);

		vkDestroyQueryPool(mVulkanDevice->mLogicalDevice, queryPool, nullptr);

		vkDestroyBuffer(mVulkanDevice->mLogicalDevice, queryResult.buffer, nullptr);
		vkFreeMemory(mVulkanDevice->mLogicalDevice, queryResult.memory, nullptr);
	}

	// Setup pool and buffer for storing pipeline statistics results
	void setupQueryResultBuffer()
	{
		uint32_t bufSize = 2 * sizeof(uint64_t);

		VkMemoryRequirements memReqs;
		VkMemoryAllocateInfo memAlloc = vkTools::memoryAllocateInfo();
		VkBufferCreateInfo bufferCreateInfo =
			vkTools::bufferCreateInfo(
				VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
				bufSize);

		// Results are saved in a host visible buffer for easy access by the application
		VK_CHECK_RESULT(vkCreateBuffer(mVulkanDevice->mLogicalDevice, &bufferCreateInfo, nullptr, &queryResult.buffer));
		vkGetBufferMemoryRequirements(mVulkanDevice->mLogicalDevice, queryResult.buffer, &memReqs);
		memAlloc.allocationSize = memReqs.size;
		memAlloc.memoryTypeIndex = mVulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
		VK_CHECK_RESULT(vkAllocateMemory(mVulkanDevice->mLogicalDevice, &memAlloc, nullptr, &queryResult.memory));
		VK_CHECK_RESULT(vkBindBufferMemory(mVulkanDevice->mLogicalDevice, queryResult.buffer, queryResult.memory, 0));

		// Create query pool
		VkQueryPoolCreateInfo queryPoolInfo = {};
		queryPoolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
		queryPoolInfo.queryType = VK_QUERY_TYPE_PIPELINE_STATISTICS;
		queryPoolInfo.pipelineStatistics =
			VK_QUERY_PIPELINE_STATISTIC_VERTEX_SHADER_INVOCATIONS_BIT |
			VK_QUERY_PIPELINE_STATISTIC_TESSELLATION_EVALUATION_SHADER_INVOCATIONS_BIT;
		queryPoolInfo.queryCount = 2;

		VK_CHECK_RESULT(vkCreateQueryPool(mVulkanDevice->mLogicalDevice, &queryPoolInfo, NULL, &queryPool));
	}

	// Retrieves the results of the pipeline statistics query submitted to the command buffer
	void getQueryResults()
	{
		// We use vkGetQueryResults to copy the results into a host visible buffer
		vkGetQueryPoolResults(
			mVulkanDevice->mLogicalDevice,
			queryPool,
			0,
			1,
			sizeof(pipelineStats),
			pipelineStats,
			sizeof(uint64_t),
			VK_QUERY_RESULT_64_BIT);
	}

	void loadTextures()
	{
		textureLoader->loadTexture(getAssetPath() + "textures/skysphere_bc3.ktx", VK_FORMAT_BC3_UNORM_BLOCK, &textures.skySphere);
		// Height data is stored in a one-channel texture
		textureLoader->loadTexture(getAssetPath() + "textures/terrain_heightmap_r16.ktx", VK_FORMAT_R16_UNORM, &textures.heightMap);
		// Terrain textures are stored in a texture array with layers corresponding to terrain height
		textureLoader->loadTextureArray(getAssetPath() + "textures/terrain_texturearray_bc3.ktx", VK_FORMAT_BC3_UNORM_BLOCK, &textures.terrainArray);

		VkSamplerCreateInfo samplerInfo = vkTools::samplerCreateInfo();

		// Setup a mirroring sampler for the height map
		vkDestroySampler(mVulkanDevice->mLogicalDevice, textures.heightMap.sampler, nullptr);
		samplerInfo.magFilter = VK_FILTER_LINEAR;
		samplerInfo.minFilter = VK_FILTER_LINEAR;
		samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
		samplerInfo.addressModeV = samplerInfo.addressModeU;
		samplerInfo.addressModeW = samplerInfo.addressModeU;
		samplerInfo.compareOp = VK_COMPARE_OP_NEVER;
		samplerInfo.minLod = 0.0f;
		samplerInfo.maxLod = (float)textures.heightMap.mipLevels;
		samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
		VK_CHECK_RESULT(vkCreateSampler(mVulkanDevice->mLogicalDevice, &samplerInfo, nullptr, &textures.heightMap.sampler));
		textures.heightMap.descriptor.sampler = textures.heightMap.sampler;

		// Setup a repeating sampler for the terrain texture layers
		vkDestroySampler(mVulkanDevice->mLogicalDevice, textures.terrainArray.sampler, nullptr);
		samplerInfo = vkTools::samplerCreateInfo();
		samplerInfo.magFilter = VK_FILTER_LINEAR;
		samplerInfo.minFilter = VK_FILTER_LINEAR;
		samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		samplerInfo.addressModeV = samplerInfo.addressModeU;
		samplerInfo.addressModeW = samplerInfo.addressModeU;
		samplerInfo.compareOp = VK_COMPARE_OP_NEVER;
		samplerInfo.minLod = 0.0f;
		samplerInfo.maxLod = (float)textures.terrainArray.mipLevels;
		samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
		if (mVulkanDevice->mFeatures.samplerAnisotropy)
		{
			samplerInfo.maxAnisotropy = 4.0f;
			samplerInfo.anisotropyEnable = VK_TRUE;
		}
		VK_CHECK_RESULT(vkCreateSampler(mVulkanDevice->mLogicalDevice, &samplerInfo, nullptr, &textures.terrainArray.sampler));
		textures.terrainArray.descriptor.sampler = textures.terrainArray.sampler;
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
		clearValues[0].color = { { 0.2f, 0.2f, 0.2f, 0.0f } };
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

			vkCmdResetQueryPool(mDrawCmdBuffers[i], queryPool, 0, 2);

			vkCmdBeginRenderPass(mDrawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

			VkViewport viewport = vkTools::viewport((float)width, (float)height, 0.0f, 1.0f);
			vkCmdSetViewport(mDrawCmdBuffers[i], 0, 1, &viewport);

			VkRect2D scissor = vkTools::rect2D(width, height, 0, 0);
			vkCmdSetScissor(mDrawCmdBuffers[i], 0, 1, &scissor);

			vkCmdSetLineWidth(mDrawCmdBuffers[i], 1.0f);

			VkDeviceSize offsets[1] = { 0 };

			// Skysphere
			vkCmdBindPipeline(mDrawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.skysphere);
			vkCmdBindDescriptorSets(mDrawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayouts.skysphere, 0, 1, &descriptorSets.skysphere, 0, NULL);
			vkCmdBindVertexBuffers(mDrawCmdBuffers[i], VERTEX_BUFFER_BIND_ID, 1, &meshes.skysphere.vertices.buf, offsets);
			vkCmdBindIndexBuffer(mDrawCmdBuffers[i], meshes.skysphere.indices.buf, 0, VK_INDEX_TYPE_UINT32);
			vkCmdDrawIndexed(mDrawCmdBuffers[i], meshes.skysphere.indexCount, 1, 0, 0, 0);

			// Terrrain
			// Begin pipeline statistics query			
			vkCmdBeginQuery(mDrawCmdBuffers[i], queryPool, 0, VK_QUERY_CONTROL_PRECISE_BIT);
			// Render
			vkCmdBindPipeline(mDrawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, wireframe ? pipelines.wireframe : pipelines.terrain);
			vkCmdBindDescriptorSets(mDrawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayouts.terrain, 0, 1, &descriptorSets.terrain, 0, NULL);
			vkCmdBindVertexBuffers(mDrawCmdBuffers[i], VERTEX_BUFFER_BIND_ID, 1, &meshes.terrain.vertices.buf, offsets);
			vkCmdBindIndexBuffer(mDrawCmdBuffers[i], meshes.terrain.indices.buf, 0, VK_INDEX_TYPE_UINT32);
			vkCmdDrawIndexed(mDrawCmdBuffers[i], meshes.terrain.indexCount, 1, 0, 0, 0);
			// End pipeline statistics query
			vkCmdEndQuery(mDrawCmdBuffers[i], queryPool, 0);

			vkCmdEndRenderPass(mDrawCmdBuffers[i]);

			VK_CHECK_RESULT(vkEndCommandBuffer(mDrawCmdBuffers[i]));
		}
	}

	void loadMeshes()
	{
		loadMesh(getAssetPath() + "models/geosphere.obj", &meshes.skysphere, vertexLayout, 1.0f);
	}

	// Encapsulate height map data for easy sampling
	struct HeightMap
	{
	private:
		uint16_t *heightdata;
		uint32_t dim;
		uint32_t scale;
	public:
#if defined(__ANDROID__)
		HeightMap(std::string filename, uint32_t patchsize, AAssetManager* assetManager)
#else
		HeightMap(std::string filename, uint32_t patchsize)
#endif
		{
#if defined(__ANDROID__)
			AAsset* asset = AAssetManager_open(assetManager, filename.c_str(), AASSET_MODE_STREAMING);
			assert(asset);
			size_t size = AAsset_getLength(asset);
			assert(size > 0);
			void *textureData = malloc(size);
			AAsset_read(asset, textureData, size);
			AAsset_close(asset);
			gli::texture2D heightTex(gli::load((const char*)textureData, size));
			free(textureData);
#else
			gli::texture2D heightTex(gli::load(filename));
#endif
			dim = static_cast<uint32_t>(heightTex.dimensions().x);
			heightdata = new uint16_t[dim * dim];
			memcpy(heightdata, heightTex.data(), heightTex.size());
			this->scale = dim / patchsize;
		};

		~HeightMap()
		{
			delete[] heightdata;
		}

		float getHeight(uint32_t x, uint32_t y)
		{
			glm::ivec2 rpos = glm::ivec2(x, y) * glm::ivec2(scale);
			rpos.x = std::max(0, std::min(rpos.x, (int)dim - 1));
			rpos.y = std::max(0, std::min(rpos.y, (int)dim - 1));
			rpos /= glm::ivec2(scale);
			return *(heightdata + (rpos.x + rpos.y * dim) * scale) / 65535.0f;
		}
	};

	// Generate a terrain quad patch for feeding to the tessellation control shader
	void generateTerrain()
	{
		struct Vertex {
			glm::vec3 pos;
			glm::vec3 normal;
			glm::vec2 uv;
		};

#define PATCH_SIZE 64
#define UV_SCALE 1.0f

		Vertex *vertices = new Vertex[PATCH_SIZE * PATCH_SIZE * 4];

		const float wx = 2.0f;
		const float wy = 2.0f;

		for (auto x = 0; x < PATCH_SIZE; x++)
		{
			for (auto y = 0; y < PATCH_SIZE; y++)
			{
				uint32_t index = (x + y * PATCH_SIZE);
				vertices[index].pos[0] = x * wx + wx / 2.0f - (float)PATCH_SIZE * wx / 2.0f;
				vertices[index].pos[1] = 0.0f;
				vertices[index].pos[2] = y * wy + wy / 2.0f - (float)PATCH_SIZE * wy / 2.0f;
				vertices[index].uv = glm::vec2((float)x / PATCH_SIZE, (float)y / PATCH_SIZE) * UV_SCALE;
			}
		}

		// Calculate normals from height map using a sobel filter
#if defined(__ANDROID__)
		HeightMap heightMap(getAssetPath() + "textures/terrain_heightmap_r16.ktx", PATCH_SIZE, androidApp->activity->assetManager);
#else
		HeightMap heightMap(getAssetPath() + "textures/terrain_heightmap_r16.ktx", PATCH_SIZE);
#endif
		for (auto x = 0; x < PATCH_SIZE; x++)
		{
			for (auto y = 0; y < PATCH_SIZE; y++)
			{
				// Get height samples centered around current position
				float heights[3][3];
				for (auto hx = -1; hx <= 1; hx++)
				{
					for (auto hy = -1; hy <= 1; hy++)
					{
						heights[hx + 1][hy + 1] = heightMap.getHeight(x + hx, y + hy);
					}
				}

				// Calcualte the normal
				glm::vec3 normal;
				// Gx sobel filter
				normal.x = heights[0][0] - heights[2][0] + 2.0f * heights[0][1] - 2.0f * heights[2][1] + heights[0][2] - heights[2][2];
				// Gy sobel filter
				normal.z = heights[0][0] + 2.0f * heights[1][0] + heights[2][0] - heights[0][2] - 2.0f * heights[1][2] - heights[2][2];
				// Calculate missing up component of the normal using the filtered x and y axis
				// The first value controls the bump strength
				normal.y = 0.25f * sqrt(1.0f - normal.x * normal.x - normal.z * normal.z);

				vertices[x + y * PATCH_SIZE].normal = glm::normalize(normal * glm::vec3(2.0f, 1.0f, 2.0f));
			}
		}

		// Indices
		const uint32_t w = (PATCH_SIZE - 1);
		uint32_t *indices = new uint32_t[w * w * 4];
		for (auto x = 0; x < w; x++)
		{
			for (auto y = 0; y < w; y++)
			{
				uint32_t index = (x + y * w) * 4;
				indices[index] = (x + y * PATCH_SIZE);
				indices[index + 1] = indices[index] + PATCH_SIZE;
				indices[index + 2] = indices[index + 1] + 1;
				indices[index + 3] = indices[index] + 1;
			}
		}
		meshes.terrain.indexCount = (PATCH_SIZE - 1) * (PATCH_SIZE - 1) * 4;

		uint32_t vertexBufferSize = (PATCH_SIZE * PATCH_SIZE * 4) * sizeof(Vertex);
		uint32_t indexBufferSize = (w * w * 4) * sizeof(uint32_t);

		struct {
			VkBuffer buffer;
			VkDeviceMemory memory;
		} vertexStaging, indexStaging;

		// Create staging buffers
		// Vertex data
		createBuffer(
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
			vertexBufferSize,
			vertices,
			&vertexStaging.buffer,
			&vertexStaging.memory);
		// Index data
		createBuffer(
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
			indexBufferSize,
			indices,
			&indexStaging.buffer,
			&indexStaging.memory);

		createBuffer(
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			vertexBufferSize,
			nullptr,
			&meshes.terrain.vertices.buf,
			&meshes.terrain.vertices.mem);

		createBuffer(
			VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			indexBufferSize,
			nullptr,
			&meshes.terrain.indices.buf,
			&meshes.terrain.indices.mem);

		// Copy from staging buffers
		VkCommandBuffer copyCmd = VulkanBase::createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

		VkBufferCopy copyRegion = {};

		copyRegion.size = vertexBufferSize;
		vkCmdCopyBuffer(
			copyCmd,
			vertexStaging.buffer,
			meshes.terrain.vertices.buf,
			1,
			&copyRegion);

		copyRegion.size = indexBufferSize;
		vkCmdCopyBuffer(
			copyCmd,
			indexStaging.buffer,
			meshes.terrain.indices.buf,
			1,
			&copyRegion);

		VulkanBase::flushCommandBuffer(copyCmd, mQueue, true);

		vkDestroyBuffer(mVulkanDevice->mLogicalDevice, vertexStaging.buffer, nullptr);
		vkFreeMemory(mVulkanDevice->mLogicalDevice, vertexStaging.memory, nullptr);
		vkDestroyBuffer(mVulkanDevice->mLogicalDevice, indexStaging.buffer, nullptr);
		vkFreeMemory(mVulkanDevice->mLogicalDevice, indexStaging.memory, nullptr);

		delete[] vertices;
		delete[] indices;
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
		vertices.inputState.vertexBindingDescriptionCount = static_cast<uint32_t>(vertices.bindingDescriptions.size());
		vertices.inputState.pVertexBindingDescriptions = vertices.bindingDescriptions.data();
		vertices.inputState.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertices.attributeDescriptions.size());
		vertices.inputState.pVertexAttributeDescriptions = vertices.attributeDescriptions.data();
	}

	void setupDescriptorPool()
	{
		std::vector<VkDescriptorPoolSize> poolSizes =
		{
			vkTools::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3),
			vkTools::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3)
		};

		VkDescriptorPoolCreateInfo descriptorPoolInfo =
			vkTools::descriptorPoolCreateInfo(
				static_cast<uint32_t>(poolSizes.size()),
				poolSizes.data(),
				2);

		VK_CHECK_RESULT(vkCreateDescriptorPool(mVulkanDevice->mLogicalDevice, &descriptorPoolInfo, nullptr, &descriptorPool));
	}

	void setupDescriptorSetLayouts()
	{
		VkDescriptorSetLayoutCreateInfo descriptorLayout;
		VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo;
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings;

		// Terrain
		setLayoutBindings =
		{
			// Binding 0 : Shared Tessellation shader ubo
			vkTools::descriptorSetLayoutBinding(
				VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT | VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
				0),
			// Binding 1 : Height map
			vkTools::descriptorSetLayoutBinding(
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT | VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
				1),
			// Binding 3 : Terrain texture array layers
			vkTools::descriptorSetLayoutBinding(
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT,
				2),
		};

		descriptorLayout = vkTools::descriptorSetLayoutCreateInfo(setLayoutBindings.data(), static_cast<uint32_t>(setLayoutBindings.size()));
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(mVulkanDevice->mLogicalDevice, &descriptorLayout, nullptr, &descriptorSetLayouts.terrain));
		pipelineLayoutCreateInfo = vkTools::pipelineLayoutCreateInfo(&descriptorSetLayouts.terrain, 1);
		VK_CHECK_RESULT(vkCreatePipelineLayout(mVulkanDevice->mLogicalDevice, &pipelineLayoutCreateInfo, nullptr, &pipelineLayouts.terrain));

		// Skysphere
		setLayoutBindings =
		{
			// Binding 0 : Vertex shader ubo
			vkTools::descriptorSetLayoutBinding(
				VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				VK_SHADER_STAGE_VERTEX_BIT,
				0),
			// Binding 1 : Color map
			vkTools::descriptorSetLayoutBinding(
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT,
				1),
		};

		descriptorLayout = vkTools::descriptorSetLayoutCreateInfo(setLayoutBindings.data(), static_cast<uint32_t>(setLayoutBindings.size()));
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(mVulkanDevice->mLogicalDevice, &descriptorLayout, nullptr, &descriptorSetLayouts.skysphere));
		pipelineLayoutCreateInfo = vkTools::pipelineLayoutCreateInfo(&descriptorSetLayouts.skysphere, 1);
		VK_CHECK_RESULT(vkCreatePipelineLayout(mVulkanDevice->mLogicalDevice, &pipelineLayoutCreateInfo, nullptr, &pipelineLayouts.skysphere));
	}

	void setupDescriptorSets()
	{
		VkDescriptorSetAllocateInfo allocInfo;
		std::vector<VkWriteDescriptorSet> writeDescriptorSets;

		// Terrain
		allocInfo = vkTools::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayouts.terrain, 1);
		VK_CHECK_RESULT(vkAllocateDescriptorSets(mVulkanDevice->mLogicalDevice, &allocInfo, &descriptorSets.terrain));

		writeDescriptorSets =
		{
			// Binding 0 : Shared tessellation shader ubo
			vkTools::writeDescriptorSet(
				descriptorSets.terrain,
				VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				0,
				&uniformData.terrainTessellation.descriptor),
			// Binding 1 : Displacement map
			vkTools::writeDescriptorSet(
				descriptorSets.terrain,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				1,
				&textures.heightMap.descriptor),
			// Binding 2 : Color map (alpha channel)
			vkTools::writeDescriptorSet(
				descriptorSets.terrain,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				2,
				&textures.terrainArray.descriptor),
		};
		vkUpdateDescriptorSets(mVulkanDevice->mLogicalDevice, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);

		// Skysphere
		allocInfo = vkTools::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayouts.skysphere, 1);
		VK_CHECK_RESULT(vkAllocateDescriptorSets(mVulkanDevice->mLogicalDevice, &allocInfo, &descriptorSets.skysphere));

		writeDescriptorSets =
		{
			// Binding 0 : Vertex shader ubo
			vkTools::writeDescriptorSet(
				descriptorSets.skysphere,
				VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				0,
				&uniformData.skysphereVertex.descriptor),
			// Binding 1 : Fragment shader color map
			vkTools::writeDescriptorSet(
				descriptorSets.skysphere,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				1,
				&textures.skySphere.descriptor),
		};
		vkUpdateDescriptorSets(mVulkanDevice->mLogicalDevice, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);
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
				static_cast<uint32_t>(dynamicStateEnables.size()),
				0);

		// We render the terrain as a grid of quad patches
		VkPipelineTessellationStateCreateInfo tessellationState =
			vkTools::pipelineTessellationStateCreateInfo(4);

		std::array<VkPipelineShaderStageCreateInfo, 4> shaderStages;

		// Terrain tessellation pipeline
		shaderStages[0] = loadShader(getAssetPath() + "shaders/terraintessellation/terrain.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getAssetPath() + "shaders/terraintessellation/terrain.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		shaderStages[2] = loadShader(getAssetPath() + "shaders/terraintessellation/terrain.tesc.spv", VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT);
		shaderStages[3] = loadShader(getAssetPath() + "shaders/terraintessellation/terrain.tese.spv", VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT);

		VkGraphicsPipelineCreateInfo pipelineCreateInfo =
			vkTools::pipelineCreateInfo(
				pipelineLayouts.terrain,
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
		pipelineCreateInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
		pipelineCreateInfo.pStages = shaderStages.data();
		pipelineCreateInfo.renderPass = mRenderPass;

		VK_CHECK_RESULT(vkCreateGraphicsPipelines(mVulkanDevice->mLogicalDevice, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.terrain));

		// Terrain wireframe pipeline
		rasterizationState.polygonMode = VK_POLYGON_MODE_LINE;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(mVulkanDevice->mLogicalDevice, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.wireframe));

		// Skysphere pipeline
		rasterizationState.polygonMode = VK_POLYGON_MODE_FILL;
		// Revert to triangle list topology
		inputAssemblyState.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		// Reset tessellation state
		pipelineCreateInfo.pTessellationState = nullptr;
		// Don't write to depth buffer
		depthStencilState.depthWriteEnable = VK_FALSE;
		pipelineCreateInfo.stageCount = 2;
		pipelineCreateInfo.layout = pipelineLayouts.skysphere;
		shaderStages[0] = loadShader(getAssetPath() + "shaders/terraintessellation/skysphere.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getAssetPath() + "shaders/terraintessellation/skysphere.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(mVulkanDevice->mLogicalDevice, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.skysphere));
	}

	// Prepare and initialize uniform buffer containing shader uniforms
	void prepareUniformBuffers()
	{
		// Shared tessellation shader stages uniform buffer
		createBuffer(
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			sizeof(uboTess),
			nullptr,
			&uniformData.terrainTessellation.buffer,
			&uniformData.terrainTessellation.memory,
			&uniformData.terrainTessellation.descriptor);

		// Skysphere vertex shader uniform buffer
		createBuffer(
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			sizeof(uboVS),
			nullptr,
			&uniformData.skysphereVertex.buffer,
			&uniformData.skysphereVertex.memory,
			&uniformData.skysphereVertex.descriptor);

		updateUniformBuffers();
	}

	void updateUniformBuffers()
	{
		// Tessellation

		uboTess.projection = mCamera.mMatrices.perspective;
		uboTess.modelview = mCamera.mMatrices.view;
		uboTess.lightPos.y = -0.5f - uboTess.displacementFactor; // todo: Not uesed yet
		uboTess.viewportDim = Vector2((float)width, (float)height);

		Matrix matTmp = uboTess.projection * uboTess.modelview;
		frustum.update(matTmp);

		memcpy(uboTess.frustumPlanes, frustum.planes.data(), sizeof(glm::vec4) * 6);

		float savedFactor = uboTess.tessellationFactor;
		if (!tessellation)
		{
			// Setting this to zero sets all tessellation factors to 1.0 in the shader
			uboTess.tessellationFactor = 0.0f;
		}

		uint8_t *pData;
		VK_CHECK_RESULT(vkMapMemory(mVulkanDevice->mLogicalDevice, uniformData.terrainTessellation.memory, 0, sizeof(uboTess), 0, (void **)&pData));
		memcpy(pData, &uboTess, sizeof(uboTess));
		vkUnmapMemory(mVulkanDevice->mLogicalDevice, uniformData.terrainTessellation.memory);

		if (!tessellation)
		{
			uboTess.tessellationFactor = savedFactor;
		}

		// Skysphere vertex shader
		mCamera.mMatrices.view.m[3] = 0;
		mCamera.mMatrices.view.m[7] = 0;
		mCamera.mMatrices.view.m[11] = 0;
		mCamera.mMatrices.view.m[12] = 0;
		mCamera.mMatrices.view.m[13] = 0;
		mCamera.mMatrices.view.m[14] = 0;
		mCamera.mMatrices.view.m[15] = 1;


		uboVS.mvp = mCamera.mMatrices.perspective * mCamera.mMatrices.view;
		//uboVS.mvp = mCamera.mMatrices.perspective * glm::mat4(glm::mat3(mCamera.mMatrices.view));

		VK_CHECK_RESULT(vkMapMemory(mVulkanDevice->mLogicalDevice, uniformData.skysphereVertex.memory, 0, sizeof(uboVS), 0, (void **)&pData));
		memcpy(pData, &uboVS, sizeof(uboVS));
		vkUnmapMemory(mVulkanDevice->mLogicalDevice, uniformData.skysphereVertex.memory);
	}

	void draw()
	{
		VulkanBase::prepareFrame();

		// Command buffer to be sumitted to the queue
		mSubmitInfo.commandBufferCount = 1;
		mSubmitInfo.pCommandBuffers = &mDrawCmdBuffers[gSwapChain.mCurrentBuffer];

		// Submit to queue
		VK_CHECK_RESULT(vkQueueSubmit(mQueue, 1, &mSubmitInfo, VK_NULL_HANDLE));

		// Read query results for displaying in next frame
		getQueryResults();

		VulkanBase::submitFrame();
	}

	void prepare()
	{
		VulkanBase::prepare();
		loadMeshes();
		loadTextures();
		generateTerrain();
		setupQueryResultBuffer();
		setupVertexDescriptions();
		prepareUniformBuffers();
		setupDescriptorSetLayouts();
		preparePipelines();
		setupDescriptorPool();
		setupDescriptorSets();
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

	void changeTessellationFactor(float delta)
	{
		uboTess.tessellationFactor += delta;
		uboTess.tessellationFactor = fmax(0.25f, fmin(uboTess.tessellationFactor, 4.0f));
		updateUniformBuffers();
		updateTextOverlay();
	}

	void toggleWireframe()
	{
		wireframe = !wireframe;
		reBuildCommandBuffers();
		updateUniformBuffers();
	}

	void toggleTessellation()
	{
		tessellation = !tessellation;
		updateUniformBuffers();
	}

	virtual void keyPressed(uint32_t keyCode)
	{
		switch (keyCode)
		{
		case Keyboard::KEY_KPADD:
		case GAMEPAD_BUTTON_R1:
			changeTessellationFactor(0.05f);
			break;
		case Keyboard::KEY_KPSUB:
		case GAMEPAD_BUTTON_L1:
			changeTessellationFactor(-0.05f);
			break;
		case Keyboard::KEY_F:
		case GAMEPAD_BUTTON_A:
			toggleWireframe();
			break;
		case Keyboard::KEY_T:
		case GAMEPAD_BUTTON_X:
			toggleTessellation();
			break;
		}
	}

	virtual void getOverlayText(VulkanTextOverlay *textOverlay)
	{
		std::stringstream ss;
		ss << std::setprecision(2) << std::fixed << uboTess.tessellationFactor;

#if defined(__ANDROID__)
		textOverlay->addText("Tessellation factor: " + ss.str() + " (Buttons L1/R1)", 5.0f, 85.0f, VulkanTextOverlay::alignLeft);
		textOverlay->addText("Press \"Button A\" to toggle wireframe", 5.0f, 100.0f, VulkanTextOverlay::alignLeft);
		textOverlay->addText("Press \"Button X\" to toggle tessellation", 5.0f, 115.0f, VulkanTextOverlay::alignLeft);
#else
		textOverlay->addText("Tessellation factor: " + ss.str() + " (numpad +/-)", 5.0f, 85.0f, VulkanTextOverlay::alignLeft);
		textOverlay->addText("Press \"f\" to toggle wireframe", 5.0f, 100.0f, VulkanTextOverlay::alignLeft);
		textOverlay->addText("Press \"t\" to toggle tessellation", 5.0f, 115.0f, VulkanTextOverlay::alignLeft);
#endif

		textOverlay->addText("pipeline stats:", width - 5.0f, 5.0f, VulkanTextOverlay::alignRight);
		textOverlay->addText("VS:" + std::to_string(pipelineStats[0]), width - 5.0f, 20.0f, VulkanTextOverlay::alignRight);
		textOverlay->addText("TE:" + std::to_string(pipelineStats[1]), width - 5.0f, 35.0f, VulkanTextOverlay::alignRight);
	}
};
