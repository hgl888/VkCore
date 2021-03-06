#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <vector>
#include "define.h"

#include <vulkan/vulkan.h>
#include "VulkanBase.h"
#include "VkCoreDevice.hpp"
#include "vulkanbuffer.hpp"

#define ENABLE_VALIDATION false

// Vertex layout used in this example
struct SceneVertex {
	Vector3 pos;
	Vector3 normal;
	Vector2 uv;
	Vector3 color;
};

// Scene related structs

// Shader properites for a material
// Will be passed to the shaders using push constant
struct SceneMaterialProperites
{
	Vector4 ambient;
	Vector4 diffuse;
	Vector4 specular;
	float opacity;
};

// Stores info on the materials used in the scene
struct SceneMaterial
{
	std::string name;
	// Material properties
	SceneMaterialProperites properties;
	// The example only uses a diffuse channel
	vkTools::VulkanTexture diffuse;
	// The material's descriptor contains the material descriptors
	VkDescriptorSet descriptorSet;
	// Pointer to the pipeline used by this material
	VkPipeline *pipeline;
};

// Stores per-mesh Vulkan resources
struct SceneMesh
{
	// Index of first index in the scene buffer
	uint32_t indexBase;
	uint32_t indexCount;

	// Pointer to the material used by this mesh
	SceneMaterial *material;
};

// Class for loading the scene and generating all Vulkan resources
class Scene
{
private:
	VkCoreDevice *vulkanDevice;
	VkQueue queue;

	VkDescriptorPool descriptorPool;

	// We will be using separate descriptor sets (and bindings)
	// for material and scene related uniforms
	struct
	{
		VkDescriptorSetLayout material;
		VkDescriptorSetLayout scene;
	} descriptorSetLayouts;

	// We will be using one single index and vertex buffer
	// containing vertices and indices for all meshes in the scene
	// This allows us to keep memory allocations down
	vk::Buffer vertexBuffer;
	vk::Buffer indexBuffer;

	VkDescriptorSet descriptorSetScene;

	vkTools::VulkanTextureLoader *textureLoader;

	const aiScene* aScene;

	// Get materials from the assimp scene and map to our scene structures
	void loadMaterials()
	{
		materials.resize(aScene->mNumMaterials);

		for (size_t i = 0; i < materials.size(); i++)
		{
			materials[i] = {};

			aiString name;
			aScene->mMaterials[i]->Get(AI_MATKEY_NAME, name);

			// Properties
			aiColor4D color;
			aScene->mMaterials[i]->Get(AI_MATKEY_COLOR_AMBIENT, color);
			//materials[i].properties.ambient = glm::make_vec4(&color.r) + glm::vec4(0.1f);
			materials[i].properties.ambient.set(&color.r);
			materials[i].properties.ambient += Vector4(0.1f);
			aScene->mMaterials[i]->Get(AI_MATKEY_COLOR_DIFFUSE, color);
			//materials[i].properties.diffuse = glm::make_vec4(&color.r);
			materials[i].properties.diffuse.set(&color.r);
			aScene->mMaterials[i]->Get(AI_MATKEY_COLOR_SPECULAR, color);
			//materials[i].properties.specular = glm::make_vec4(&color.r);
			materials[i].properties.specular.set(&color.r);
			aScene->mMaterials[i]->Get(AI_MATKEY_OPACITY, materials[i].properties.opacity);

			if ((materials[i].properties.opacity) > 0.0f)
				materials[i].properties.specular = Vector4(0.0f);

			materials[i].name = name.C_Str();
			std::cout << "Material \"" << materials[i].name << "\"" << std::endl;

			// Textures
			aiString texturefile;
			// Diffuse
			aScene->mMaterials[i]->GetTexture(aiTextureType_DIFFUSE, 0, &texturefile);
			if (aScene->mMaterials[i]->GetTextureCount(aiTextureType_DIFFUSE) > 0)
			{
				std::cout << "  Diffuse: \"" << texturefile.C_Str() << "\"" << std::endl;
				std::string fileName = std::string(texturefile.C_Str());
				std::replace(fileName.begin(), fileName.end(), '\\', '/');
				textureLoader->loadTexture(assetPath + fileName, VK_FORMAT_BC3_UNORM_BLOCK, &materials[i].diffuse);
			}
			else
			{
				std::cout << "  Material has no diffuse, using dummy texture!" << std::endl;
				// todo : separate pipeline and layout
				textureLoader->loadTexture(assetPath + "dummy.ktx", VK_FORMAT_BC2_UNORM_BLOCK, &materials[i].diffuse);
			}

			// For scenes with multiple textures per material we would need to check for additional texture types, e.g.:
			// aiTextureType_HEIGHT, aiTextureType_OPACITY, aiTextureType_SPECULAR, etc.

			// Assign pipeline
			materials[i].pipeline = (materials[i].properties.opacity == 0.0f) ? &pipelines.solid : &pipelines.blending;
		}

		// Generate descriptor sets for the materials

		// Descriptor pool
		std::vector<VkDescriptorPoolSize> poolSizes;
		poolSizes.push_back(vkTools::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, static_cast<uint32_t>(materials.size())));
		poolSizes.push_back(vkTools::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, static_cast<uint32_t>(materials.size())));

		VkDescriptorPoolCreateInfo descriptorPoolInfo =
			vkTools::descriptorPoolCreateInfo(
				static_cast<uint32_t>(poolSizes.size()),
				poolSizes.data(),
				static_cast<uint32_t>(materials.size()) + 1);

		VK_CHECK_RESULT(vkCreateDescriptorPool(vulkanDevice->mLogicalDevice, &descriptorPoolInfo, nullptr, &descriptorPool));

		// Descriptor set and pipeline layouts
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings;
		VkDescriptorSetLayoutCreateInfo descriptorLayout;

		// Set 0: Scene matrices
		setLayoutBindings.push_back(vkTools::descriptorSetLayoutBinding(
			VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			VK_SHADER_STAGE_VERTEX_BIT,
			0));
		descriptorLayout = vkTools::descriptorSetLayoutCreateInfo(
			setLayoutBindings.data(),
			static_cast<uint32_t>(setLayoutBindings.size()));
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(vulkanDevice->mLogicalDevice, &descriptorLayout, nullptr, &descriptorSetLayouts.scene));

		// Set 1: Material data
		setLayoutBindings.clear();
		setLayoutBindings.push_back(vkTools::descriptorSetLayoutBinding(
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			VK_SHADER_STAGE_FRAGMENT_BIT,
			0));
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(vulkanDevice->mLogicalDevice, &descriptorLayout, nullptr, &descriptorSetLayouts.material));

		// Setup pipeline layout
		std::array<VkDescriptorSetLayout, 2> setLayouts = { descriptorSetLayouts.scene, descriptorSetLayouts.material };
		VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = vkTools::pipelineLayoutCreateInfo(setLayouts.data(), static_cast<uint32_t>(setLayouts.size()));

		// We will be using a push constant block to pass material properties to the fragment shaders
		VkPushConstantRange pushConstantRange = vkTools::pushConstantRange(
			VK_SHADER_STAGE_FRAGMENT_BIT,
			sizeof(SceneMaterialProperites),
			0);
		pipelineLayoutCreateInfo.pushConstantRangeCount = 1;
		pipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;

		VK_CHECK_RESULT(vkCreatePipelineLayout(vulkanDevice->mLogicalDevice, &pipelineLayoutCreateInfo, nullptr, &pipelineLayout));

		// Material descriptor sets
		for (size_t i = 0; i < materials.size(); i++)
		{
			// Descriptor set
			VkDescriptorSetAllocateInfo allocInfo =
				vkTools::descriptorSetAllocateInfo(
					descriptorPool,
					&descriptorSetLayouts.material,
					1);

			VK_CHECK_RESULT(vkAllocateDescriptorSets(vulkanDevice->mLogicalDevice, &allocInfo, &materials[i].descriptorSet));

			std::vector<VkWriteDescriptorSet> writeDescriptorSets;

			// todo : only use image sampler descriptor set and use one scene ubo for matrices

			// Binding 0: Diffuse texture
			writeDescriptorSets.push_back(vkTools::writeDescriptorSet(
				materials[i].descriptorSet,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				0,
				&materials[i].diffuse.descriptor));

			vkUpdateDescriptorSets(vulkanDevice->mLogicalDevice, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);
		}

		// Scene descriptor set
		VkDescriptorSetAllocateInfo allocInfo =
			vkTools::descriptorSetAllocateInfo(
				descriptorPool,
				&descriptorSetLayouts.scene,
				1);
		VK_CHECK_RESULT(vkAllocateDescriptorSets(vulkanDevice->mLogicalDevice, &allocInfo, &descriptorSetScene));

		std::vector<VkWriteDescriptorSet> writeDescriptorSets;
		// Binding 0 : Vertex shader uniform buffer
		writeDescriptorSets.push_back(vkTools::writeDescriptorSet(
			descriptorSetScene,
			VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			0,
			&uniformBuffer.descriptor));

		vkUpdateDescriptorSets(vulkanDevice->mLogicalDevice, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);
	}

	// Load all meshes from the scene and generate the buffers for rendering them
	void loadMeshes(VkCommandBuffer copyCmd)
	{
		std::vector<SceneVertex> vertices;
		std::vector<uint32_t> indices;
		uint32_t indexBase = 0;

		meshes.resize(aScene->mNumMeshes);
		for (uint32_t i = 0; i < meshes.size(); i++)
		{
			aiMesh *aMesh = aScene->mMeshes[i];

			std::cout << "Mesh \"" << aMesh->mName.C_Str() << "\"" << std::endl;
			std::cout << "	Material: \"" << materials[aMesh->mMaterialIndex].name << "\"" << std::endl;
			std::cout << "	Faces: " << aMesh->mNumFaces << std::endl;

			meshes[i].material = &materials[aMesh->mMaterialIndex];
			meshes[i].indexBase = indexBase;
			meshes[i].indexCount = aMesh->mNumFaces * 3;

			// Vertices
			bool hasUV = aMesh->HasTextureCoords(0);
			bool hasColor = aMesh->HasVertexColors(0);
			bool hasNormals = aMesh->HasNormals();

			for (uint32_t v = 0; v < aMesh->mNumVertices; v++)
			{
				SceneVertex vertex;
				vertex.pos.set(&aMesh->mVertices[v].x);
				vertex.pos.y = -vertex.pos.y;
				if (hasUV) {
					vertex.uv.set(&aMesh->mTextureCoords[0][v].x);
					//vertex.uv = hasUV ? glm::make_vec2(&aMesh->mTextureCoords[0][v].x) : glm::vec2(0.0f);
				}
				if (hasNormals) {
					vertex.normal.set(&aMesh->mNormals[v].x);
					//vertex.normal = hasNormals ? glm::make_vec3(&aMesh->mNormals[v].x) : glm::vec3(0.0f);
				}
				vertex.normal.y = -vertex.normal.y;
				if (hasColor) {
					vertex.color.set(&aMesh->mColors[0][v].r);
					//vertex.color = hasColor ? glm::make_vec3(&aMesh->mColors[0][v].r) : glm::vec3(1.0f);
				}
				else {
					vertex.color.set(1.0f, 1.0f, 1.0f);
				}
				vertices.push_back(vertex);
			}

			// Indices
			for (uint32_t f = 0; f < aMesh->mNumFaces; f++)
			{
				for (uint32_t j = 0; j < 3; j++)
				{
					indices.push_back(aMesh->mFaces[f].mIndices[j]);
				}
			}

			indexBase += aMesh->mNumFaces * 3;
		}

		// Create buffers
		// For better performance we only create one index and vertex buffer to keep number of memory allocations down
		size_t vertexDataSize = vertices.size() * sizeof(SceneVertex);
		size_t indexDataSize = indices.size() * sizeof(uint32_t);

		vk::Buffer vertexStaging, indexStaging;

		// Vertex buffer
		// Staging buffer
		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&vertexStaging,
			static_cast<uint32_t>(vertexDataSize),
			vertices.data()));
		// Target
		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			&vertexBuffer,
			static_cast<uint32_t>(vertexDataSize)));

		// Index buffer
		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&indexStaging,
			static_cast<uint32_t>(indexDataSize),
			indices.data()));
		// Target
		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			&indexBuffer,
			static_cast<uint32_t>(indexDataSize)));

		// Copy
		VkCommandBufferBeginInfo cmdBufInfo = vkTools::commandBufferBeginInfo();
		VK_CHECK_RESULT(vkBeginCommandBuffer(copyCmd, &cmdBufInfo));

		VkBufferCopy copyRegion = {};

		copyRegion.size = vertexDataSize;
		vkCmdCopyBuffer(
			copyCmd,
			vertexStaging.buffer,
			vertexBuffer.buffer,
			1,
			&copyRegion);

		copyRegion.size = indexDataSize;
		vkCmdCopyBuffer(
			copyCmd,
			indexStaging.buffer,
			indexBuffer.buffer,
			1,
			&copyRegion);

		VK_CHECK_RESULT(vkEndCommandBuffer(copyCmd));

		VkSubmitInfo submitInfo = {};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &copyCmd;

		VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));
		VK_CHECK_RESULT(vkQueueWaitIdle(queue));

		//todo: fence
		vertexStaging.destroy();
		indexStaging.destroy();
	}

public:
#if defined(__ANDROID__)
	AAssetManager* assetManager = nullptr;
#endif

	std::string assetPath = "";

	std::vector<SceneMaterial> materials;
	std::vector<SceneMesh> meshes;

	// Shared ubo containing matrices used by all
	// materials and meshes
	vkTools::UniformData uniformBuffer;
	struct {
		Matrix projection;
		Matrix view;
		Matrix model;
		Vector4 lightPos = Vector4(1.25f, 8.35f, 0.0f, 0.0f);
	} uniformData;

	// Scene uses multiple pipelines
	struct {
		VkPipeline solid;
		VkPipeline blending;
		VkPipeline wireframe;
	} pipelines;

	// Shared pipeline layout
	VkPipelineLayout pipelineLayout;

	// For displaying only a single part of the scene
	bool renderSingleScenePart = false;
	uint32_t scenePartIndex = 0;

	// Default constructor
	Scene(VkCoreDevice *vulkanDevice, VkQueue queue, vkTools::VulkanTextureLoader *textureloader)
	{
		this->vulkanDevice = vulkanDevice;
		this->queue = queue;
		this->textureLoader = textureloader;

		// Prepare uniform buffer for global matrices
		VkMemoryRequirements memReqs;
		VkMemoryAllocateInfo memAlloc = vkTools::memoryAllocateInfo();
		VkBufferCreateInfo bufferCreateInfo = vkTools::bufferCreateInfo(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, sizeof(uniformData));
		VK_CHECK_RESULT(vkCreateBuffer(vulkanDevice->mLogicalDevice, &bufferCreateInfo, nullptr, &uniformBuffer.buffer));
		vkGetBufferMemoryRequirements(vulkanDevice->mLogicalDevice, uniformBuffer.buffer, &memReqs);
		memAlloc.allocationSize = memReqs.size;
		memAlloc.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		VK_CHECK_RESULT(vkAllocateMemory(vulkanDevice->mLogicalDevice, &memAlloc, nullptr, &uniformBuffer.memory));
		VK_CHECK_RESULT(vkBindBufferMemory(vulkanDevice->mLogicalDevice, uniformBuffer.buffer, uniformBuffer.memory, 0));
		VK_CHECK_RESULT(vkMapMemory(vulkanDevice->mLogicalDevice, uniformBuffer.memory, 0, sizeof(uniformData), 0, (void **)&uniformBuffer.mapped));
		uniformBuffer.descriptor.offset = 0;
		uniformBuffer.descriptor.buffer = uniformBuffer.buffer;
		uniformBuffer.descriptor.range = sizeof(uniformData);
	}

	// Default destructor
	~Scene()
	{
		vertexBuffer.destroy();
		indexBuffer.destroy();
		for (auto material : materials)
		{
			textureLoader->destroyTexture(material.diffuse);
		}
		vkDestroyPipelineLayout(vulkanDevice->mLogicalDevice, pipelineLayout, nullptr);
		vkDestroyDescriptorSetLayout(vulkanDevice->mLogicalDevice, descriptorSetLayouts.material, nullptr);
		vkDestroyDescriptorSetLayout(vulkanDevice->mLogicalDevice, descriptorSetLayouts.scene, nullptr);
		vkDestroyDescriptorPool(vulkanDevice->mLogicalDevice, descriptorPool, nullptr);
		vkDestroyPipeline(vulkanDevice->mLogicalDevice, pipelines.solid, nullptr);
		vkDestroyPipeline(vulkanDevice->mLogicalDevice, pipelines.blending, nullptr);
		vkDestroyPipeline(vulkanDevice->mLogicalDevice, pipelines.wireframe, nullptr);
		vkTools::destroyUniformData(vulkanDevice->mLogicalDevice, &uniformBuffer);
	}

	void load(std::string filename, VkCommandBuffer copyCmd)
	{
		Assimp::Importer Importer;

		int flags = aiProcess_PreTransformVertices | aiProcess_Triangulate | aiProcess_GenNormals;

#if defined(__ANDROID__)
		AAsset* asset = AAssetManager_open(assetManager, filename.c_str(), AASSET_MODE_STREAMING);
		assert(asset);
		size_t size = AAsset_getLength(asset);
		assert(size > 0);
		void *meshData = malloc(size);
		AAsset_read(asset, meshData, size);
		AAsset_close(asset);
		aScene = Importer.ReadFileFromMemory(meshData, size, flags);
		free(meshData);
#else
		aScene = Importer.ReadFile(filename.c_str(), flags);
#endif
		if (aScene)
		{
			loadMaterials();
			loadMeshes(copyCmd);
		}
		else
		{
			printf("Error parsing '%s': '%s'\n", filename.c_str(), Importer.GetErrorString());
#if defined(__ANDROID__)
			LOGE("Error parsing '%s': '%s'", filename.c_str(), Importer.GetErrorString());
#endif
		}

	}

	// Renders the scene into an active command buffer
	// In a real world application we would do some visibility culling in here
	void render(VkCommandBuffer cmdBuffer, bool wireframe)
	{
		VkDeviceSize offsets[1] = { 0 };

		// Bind scene vertex and index buffers
		vkCmdBindVertexBuffers(cmdBuffer, 0, 1, &vertexBuffer.buffer, offsets);
		vkCmdBindIndexBuffer(cmdBuffer, indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

		for (size_t i = 0; i < meshes.size(); i++)
		{
			if ((renderSingleScenePart) && (i != scenePartIndex))
				continue;

			// todo : per material pipelines
			// vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, *mesh.material->pipeline);

			// We will be using multiple descriptor sets for rendering
			// In GLSL the selection is done via the set and binding keywords
			// VS: layout (set = 0, binding = 0) uniform UBO;
			// FS: layout (set = 1, binding = 0) uniform sampler2D samplerColorMap;

			std::array<VkDescriptorSet, 2> descriptorSets;
			// Set 0: Scene descriptor set containing global matrices
			descriptorSets[0] = descriptorSetScene;
			// Set 1: Per-Material descriptor set containing bound images
			descriptorSets[1] = meshes[i].material->descriptorSet;

			vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, wireframe ? pipelines.wireframe : *meshes[i].material->pipeline);
			vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, static_cast<uint32_t>(descriptorSets.size()), descriptorSets.data(), 0, NULL);

			// Pass material properies via push constants
			vkCmdPushConstants(
				cmdBuffer,
				pipelineLayout,
				VK_SHADER_STAGE_FRAGMENT_BIT,
				0,
				sizeof(SceneMaterialProperites),
				&meshes[i].material->properties);

			// Render from the global scene vertex buffer using the mesh index offset
			vkCmdDrawIndexed(cmdBuffer, meshes[i].indexCount, 1, 0, meshes[i].indexBase, 0);
		}
	}
};

class VkSceneRendering : public VulkanBase
{
public:
	bool wireframe = false;
	bool attachLight = false;

	Scene *scene = nullptr;

	struct {
		VkPipelineVertexInputStateCreateInfo inputState;
		std::vector<VkVertexInputBindingDescription> bindingDescriptions;
		std::vector<VkVertexInputAttributeDescription> attributeDescriptions;
	} vertices;

	VkSceneRendering() : VulkanBase(ENABLE_VALIDATION)
	{
		rotationSpeed = 0.5f;
		mEnableTextOverlay = true;
		mCamera.type = VkCamera::CameraType::firstperson;
		mCamera.movementSpeed = 7.5f;
		mCamera.position = { 15.0f, -13.5f, 0.0f };
		mCamera.setRotation(Vector3(5.0f, 90.0f, 0.0f));
		mCamera.setPerspective(60.0f, (float)width / (float)height, 0.1f, 256.0f);
		title = "Scene rendering";
	}

	~VkSceneRendering()
	{
		delete(scene);
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
		clearValues[0].color = { { 0.25f, 0.25f, 0.25f, 1.0f } };
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

			scene->render(mDrawCmdBuffers[i], wireframe);

			vkCmdEndRenderPass(mDrawCmdBuffers[i]);

			VK_CHECK_RESULT(vkEndCommandBuffer(mDrawCmdBuffers[i]));
		}
	}

	void setupVertexDescriptions()
	{
		// Binding description
		vertices.bindingDescriptions.resize(1);
		vertices.bindingDescriptions[0] =
			vkTools::vertexInputBindingDescription(
				VERTEX_BUFFER_BIND_ID,
				sizeof(SceneVertex),
				VK_VERTEX_INPUT_RATE_VERTEX);

		// Attribute descriptions
		// Describes memory layout and shader positions
		vertices.attributeDescriptions.resize(4);
		// Location 0 : Position
		vertices.attributeDescriptions[0] =
			vkTools::vertexInputAttributeDescription(
				VERTEX_BUFFER_BIND_ID,
				0,
				VK_FORMAT_R32G32B32_SFLOAT,
				0);
		// Location 1 : Normal
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
		// Location 3 : Color
		vertices.attributeDescriptions[3] =
			vkTools::vertexInputAttributeDescription(
				VERTEX_BUFFER_BIND_ID,
				3,
				VK_FORMAT_R32G32B32_SFLOAT,
				sizeof(float) * 8);

		vertices.inputState = vkTools::pipelineVertexInputStateCreateInfo();
		vertices.inputState.vertexBindingDescriptionCount = static_cast<uint32_t>(vertices.bindingDescriptions.size());
		vertices.inputState.pVertexBindingDescriptions = vertices.bindingDescriptions.data();
		vertices.inputState.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertices.attributeDescriptions.size());
		vertices.inputState.pVertexAttributeDescriptions = vertices.attributeDescriptions.data();
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

		std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

		// Solid rendering pipeline
		shaderStages[0] = loadShader(getAssetPath() + "shaders/scenerendering/scene.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getAssetPath() + "shaders/scenerendering/scene.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

		VkGraphicsPipelineCreateInfo pipelineCreateInfo =
			vkTools::pipelineCreateInfo(
				scene->pipelineLayout,
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

		VK_CHECK_RESULT(vkCreateGraphicsPipelines(mVulkanDevice->mLogicalDevice, pipelineCache, 1, &pipelineCreateInfo, nullptr, &scene->pipelines.solid));

		// Alpha blended pipeline
		rasterizationState.cullMode = VK_CULL_MODE_NONE;
		blendAttachmentState.blendEnable = VK_TRUE;
		blendAttachmentState.colorBlendOp = VK_BLEND_OP_ADD;
		blendAttachmentState.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_COLOR;
		blendAttachmentState.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;

		VK_CHECK_RESULT(vkCreateGraphicsPipelines(mVulkanDevice->mLogicalDevice, pipelineCache, 1, &pipelineCreateInfo, nullptr, &scene->pipelines.blending));

		// Wire frame rendering pipeline
		rasterizationState.cullMode = VK_CULL_MODE_BACK_BIT;
		blendAttachmentState.blendEnable = VK_FALSE;
		rasterizationState.polygonMode = VK_POLYGON_MODE_LINE;
		rasterizationState.lineWidth = 1.0f;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(mVulkanDevice->mLogicalDevice, pipelineCache, 1, &pipelineCreateInfo, nullptr, &scene->pipelines.wireframe));
	}

	void updateUniformBuffers()
	{
		if (attachLight)
		{
			scene->uniformData.lightPos = Vector4(-mCamera.position.x, 
				-mCamera.position.y, -mCamera.position.z, 1.0f);
		}

		scene->uniformData.projection = mCamera.mMatrices.perspective;
		scene->uniformData.view = mCamera.mMatrices.view;

		memcpy(scene->uniformBuffer.mapped, &scene->uniformData, sizeof(scene->uniformData));
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

	void loadScene()
	{
		VkCommandBuffer copyCmd = VulkanBase::createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, false);
		scene = new Scene(mVulkanDevice, mQueue, textureLoader);

#if defined(__ANDROID__)
		scene->assetManager = androidApp->activity->assetManager;
#endif
		scene->assetPath = getAssetPath() + "models/sibenik/";
		scene->load(getAssetPath() + "models/sibenik/sibenik.dae", copyCmd);
		vkFreeCommandBuffers(mVulkanDevice->mLogicalDevice, mCmdPool, 1, &copyCmd);
		updateUniformBuffers();
	}

	void prepare()
	{
		VulkanBase::prepare();
		setupVertexDescriptions();
		loadScene();
		preparePipelines();
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

	virtual void keyPressed(uint32_t keyCode)
	{
		switch (keyCode)
		{
		case Keyboard::KEY_SPACE:
		case GAMEPAD_BUTTON_A:
			wireframe = !wireframe;
			reBuildCommandBuffers();
			break;
		case Keyboard::KEY_P:
			scene->renderSingleScenePart = !scene->renderSingleScenePart;
			reBuildCommandBuffers();
			updateTextOverlay();
			break;
		case Keyboard::KEY_KPADD:
			scene->scenePartIndex = (scene->scenePartIndex < static_cast<uint32_t>(scene->meshes.size())) ? scene->scenePartIndex + 1 : 0;
			reBuildCommandBuffers();
			updateTextOverlay();
			break;
		case Keyboard::KEY_KPSUB:
			scene->scenePartIndex = (scene->scenePartIndex > 0) ? scene->scenePartIndex - 1 : static_cast<uint32_t>(scene->meshes.size()) - 1;
			updateTextOverlay();
			reBuildCommandBuffers();
			break;
		case Keyboard::KEY_L:
			attachLight = !attachLight;
			updateUniformBuffers();
			break;
		}
	}

	virtual void getOverlayText(VulkanTextOverlay *textOverlay)
	{
#if defined(__ANDROID__)
		textOverlay->addText("Press \"Button A\" to toggle wireframe", 5.0f, 85.0f, VulkanTextOverlay::alignLeft);
#else
		textOverlay->addText("Press \"space\" to toggle wireframe", 5.0f, 85.0f, VulkanTextOverlay::alignLeft);
		if ((scene) && (scene->renderSingleScenePart))
		{
			textOverlay->addText("Rendering mesh " + std::to_string(scene->scenePartIndex + 1) + " of " + std::to_string(static_cast<uint32_t>(scene->meshes.size())) + "(\"p\" to toggle)", 5.0f, 100.0f, VulkanTextOverlay::alignLeft);
		}
		else
		{
			textOverlay->addText("Rendering whole scene (\"p\" to toggle)", 5.0f, 100.0f, VulkanTextOverlay::alignLeft);
		}
#endif
	}
};

