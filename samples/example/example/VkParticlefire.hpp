#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <vector>
#include "define.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include <vulkan/vulkan.h>
#include "VulkanBase.h"

#define ENABLE_VALIDATION false
#define PARTICLE_COUNT 512
#define PARTICLE_SIZE 10.0f

#define FLAME_RADIUS 8.0f

#define PARTICLE_TYPE_FLAME 0
#define PARTICLE_TYPE_SMOKE 1

struct Particle {
	glm::vec4 pos;
	glm::vec4 color;
	float alpha;
	float size;
	float rotation;
	uint32_t type;
	// Attributes not used in shader
	glm::vec4 vel;
	float rotationSpeed;
};


class VkParticlefire : public VulkanBase
{
	// Vertex layout for this example
	std::vector<vkMeshLoader::VertexLayout> vertexLayout =
	{
		vkMeshLoader::VERTEX_LAYOUT_POSITION,
		vkMeshLoader::VERTEX_LAYOUT_UV,
		vkMeshLoader::VERTEX_LAYOUT_NORMAL,
		vkMeshLoader::VERTEX_LAYOUT_TANGENT,
		vkMeshLoader::VERTEX_LAYOUT_BITANGENT
	};
public:
	struct {
		struct {
			vkTools::VulkanTexture smoke;
			vkTools::VulkanTexture fire;
			// We use a custom sampler to change some sampler
			// attributes required for rotation the uv coordinates
			// inside the shader for alpha blended textures
			VkSampler sampler;
		} particles;
		struct {
			vkTools::VulkanTexture colorMap;
			vkTools::VulkanTexture normalMap;
		} floor;
	} textures;

	struct {
		vkMeshLoader::Mesh environment;
	} meshes;

	glm::vec3 emitterPos = glm::vec3(0.0f, -FLAME_RADIUS + 2.0f, 0.0f);
	glm::vec3 minVel = glm::vec3(-3.0f, 0.5f, -3.0f);
	glm::vec3 maxVel = glm::vec3(3.0f, 7.0f, 3.0f);

	struct {
		VkBuffer buffer;
		VkDeviceMemory memory;
		// Store the mapped address of the particle data for reuse
		void *mappedMemory;
		// Size of the particle buffer in bytes
		size_t size;
		VkPipelineVertexInputStateCreateInfo inputState;
		std::vector<VkVertexInputBindingDescription> bindingDescriptions;
		std::vector<VkVertexInputAttributeDescription> attributeDescriptions;
	} particles;

	struct {
		vkTools::UniformData fire;
		vkTools::UniformData environment;
	} uniformData;

	struct {
		glm::mat4 projection;
		glm::mat4 model;
		glm::vec2 viewportDim;
		float pointSize = PARTICLE_SIZE;
	} uboVS;

	struct {
		glm::mat4 projection;
		glm::mat4 model;
		glm::mat4 normal;
		glm::vec4 lightPos = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
		glm::vec4 cameraPos;
	} uboEnv;

	struct {
		VkPipeline particles;
		VkPipeline environment;
	} pipelines;

	VkPipelineLayout pipelineLayout;
	VkDescriptorSet descriptorSet;
	VkDescriptorSetLayout descriptorSetLayout;

	std::vector<Particle> particleBuffer;

	VkParticlefire() : VulkanBase(ENABLE_VALIDATION)
	{
		mZoom = -75.0f;
		mRotation = { -15.0f, 45.0f, 0.0f };
		mEnableTextOverlay = true;
		title = "Vulkan Example - Particle system";
		zoomSpeed *= 1.5f;
		timerSpeed *= 8.0f;
		srand(time(NULL));
	}

	~VkParticlefire()
	{
		// Clean up used Vulkan resources 
		// Note : Inherited destructor cleans up resources stored in base class

		textureLoader->destroyTexture(textures.particles.smoke);
		textureLoader->destroyTexture(textures.particles.fire);
		textureLoader->destroyTexture(textures.floor.colorMap);
		textureLoader->destroyTexture(textures.floor.normalMap);

		vkDestroyPipeline(mVulkanDevice->mLogicalDevice, pipelines.particles, nullptr);
		vkDestroyPipeline(mVulkanDevice->mLogicalDevice, pipelines.environment, nullptr);

		vkDestroyPipelineLayout(mVulkanDevice->mLogicalDevice, pipelineLayout, nullptr);
		vkDestroyDescriptorSetLayout(mVulkanDevice->mLogicalDevice, descriptorSetLayout, nullptr);

		vkUnmapMemory(mVulkanDevice->mLogicalDevice, particles.memory);
		vkDestroyBuffer(mVulkanDevice->mLogicalDevice, particles.buffer, nullptr);
		vkFreeMemory(mVulkanDevice->mLogicalDevice, particles.memory, nullptr);

		vkDestroyBuffer(mVulkanDevice->mLogicalDevice, uniformData.fire.buffer, nullptr);
		vkFreeMemory(mVulkanDevice->mLogicalDevice, uniformData.fire.memory, nullptr);

		vkMeshLoader::freeMeshBufferResources(mVulkanDevice->mLogicalDevice, &meshes.environment.mMeshBuffers);

		vkDestroySampler(mVulkanDevice->mLogicalDevice, textures.particles.sampler, nullptr);
	}

	void buildCommandBuffers()
	{
		VkCommandBufferBeginInfo cmdBufInfo = vkTools::commandBufferBeginInfo();

		VkClearValue clearValues[2];
		clearValues[0].color = defaultClearColor;
		clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
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

			// Environment
			meshes.environment.drawIndexed(mDrawCmdBuffers[i]);

			// Particle system
			vkCmdBindDescriptorSets(mDrawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, NULL);
			vkCmdBindPipeline(mDrawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.particles);
			VkDeviceSize offsets[1] = { 0 };
			vkCmdBindVertexBuffers(mDrawCmdBuffers[i], VERTEX_BUFFER_BIND_ID, 1, &particles.buffer, offsets);
			vkCmdDraw(mDrawCmdBuffers[i], PARTICLE_COUNT, 1, 0, 0);

			vkCmdEndRenderPass(mDrawCmdBuffers[i]);

			VK_CHECK_RESULT(vkEndCommandBuffer(mDrawCmdBuffers[i]));
		}
	}

	float rnd(float range)
	{
		return range * (rand() / double(RAND_MAX));
	}

	void initParticle(Particle *particle, glm::vec3 emitterPos)
	{
		particle->vel = glm::vec4(0.0f, minVel.y + rnd(maxVel.y - minVel.y), 0.0f, 0.0f);
		particle->alpha = rnd(0.75f);
		particle->size = 1.0f + rnd(0.5f);
		particle->color = glm::vec4(1.0f);
		particle->type = PARTICLE_TYPE_FLAME;
		particle->rotation = rnd(2.0f * M_PI);
		particle->rotationSpeed = rnd(2.0f) - rnd(2.0f);

		// Get random sphere point
		float theta = rnd(2 * M_PI);
		float phi = rnd(M_PI) - M_PI / 2;
		float r = rnd(FLAME_RADIUS);

		particle->pos.x = r * cos(theta) * cos(phi);
		particle->pos.y = r * sin(phi);
		particle->pos.z = r * sin(theta) * cos(phi);

		particle->pos += glm::vec4(emitterPos, 0.0f);
	}

	void transitionParticle(Particle *particle)
	{
		switch (particle->type)
		{
		case PARTICLE_TYPE_FLAME:
			// Flame particles have a chance of turning into smoke
			if (rnd(1.0f) < 0.05f)
			{
				particle->alpha = 0.0f;
				particle->color = glm::vec4(0.25f + rnd(0.25f));
				particle->pos.x *= 0.5f;
				particle->pos.z *= 0.5f;
				particle->vel = glm::vec4(rnd(1.0f) - rnd(1.0f), (minVel.y * 2) + rnd(maxVel.y - minVel.y), rnd(1.0f) - rnd(1.0f), 0.0f);
				particle->size = 1.0f + rnd(0.5f);
				particle->rotationSpeed = rnd(1.0f) - rnd(1.0f);
				particle->type = PARTICLE_TYPE_SMOKE;
			}
			else
			{
				initParticle(particle, emitterPos);
			}
			break;
		case PARTICLE_TYPE_SMOKE:
			// Respawn at end of life
			initParticle(particle, emitterPos);
			break;
		}
	}

	void prepareParticles()
	{
		particleBuffer.resize(PARTICLE_COUNT);
		for (auto& particle : particleBuffer)
		{
			initParticle(&particle, emitterPos);
			particle.alpha = 1.0f - (abs(particle.pos.y) / (FLAME_RADIUS * 2.0f));
		}

		particles.size = particleBuffer.size() * sizeof(Particle);

		createBuffer(
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
			particles.size,
			particleBuffer.data(),
			&particles.buffer,
			&particles.memory);

		// Map the memory and store the pointer for reuse
		VK_CHECK_RESULT(vkMapMemory(mVulkanDevice->mLogicalDevice, particles.memory, 0, particles.size, 0, &particles.mappedMemory));
	}

	void updateParticles()
	{
		float particleTimer = frameTimer * 0.45f;
		for (auto& particle : particleBuffer)
		{
			switch (particle.type)
			{
			case PARTICLE_TYPE_FLAME:
				particle.pos.y -= particle.vel.y * particleTimer * 3.5f;
				particle.alpha += particleTimer * 2.5f;
				particle.size -= particleTimer * 0.5f;
				break;
			case PARTICLE_TYPE_SMOKE:
				particle.pos -= particle.vel * frameTimer * 1.0f;
				particle.alpha += particleTimer * 1.25f;
				particle.size += particleTimer * 0.125f;
				particle.color -= particleTimer * 0.05f;
				break;
			}
			particle.rotation += particleTimer * particle.rotationSpeed;
			// Transition particle state
			if (particle.alpha > 2.0f)
			{
				transitionParticle(&particle);
			}
		}
		size_t size = particleBuffer.size() * sizeof(Particle);
		memcpy(particles.mappedMemory, particleBuffer.data(), size);
	}

	void loadTextures()
	{
		// Particles
		textureLoader->loadTexture(
			getAssetPath() + "textures/particle_smoke.ktx",
			VK_FORMAT_BC3_UNORM_BLOCK,
			&textures.particles.smoke);
		textureLoader->loadTexture(
			getAssetPath() + "textures/particle_fire.ktx",
			VK_FORMAT_BC3_UNORM_BLOCK,
			&textures.particles.fire);

		// Floor
		textureLoader->loadTexture(
			getAssetPath() + "textures/fireplace_colormap_bc3.ktx",
			VK_FORMAT_BC3_UNORM_BLOCK,
			&textures.floor.colorMap);
		textureLoader->loadTexture(
			getAssetPath() + "textures/fireplace_normalmap_bc3.ktx",
			VK_FORMAT_BC3_UNORM_BLOCK,
			&textures.floor.normalMap);

		// Create a custom sampler to be used with the particle textures
		// Create sampler
		VkSamplerCreateInfo samplerCreateInfo = vkTools::samplerCreateInfo();
		samplerCreateInfo.magFilter = VK_FILTER_LINEAR;
		samplerCreateInfo.minFilter = VK_FILTER_LINEAR;
		samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		// Different address mode
		samplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
		samplerCreateInfo.addressModeV = samplerCreateInfo.addressModeU;
		samplerCreateInfo.addressModeW = samplerCreateInfo.addressModeU;
		samplerCreateInfo.mipLodBias = 0.0f;
		samplerCreateInfo.compareOp = VK_COMPARE_OP_NEVER;
		samplerCreateInfo.minLod = 0.0f;
		// Both particle textures have the same number of mip maps
		samplerCreateInfo.maxLod = textures.particles.fire.mipLevels;
		// Enable anisotropic filtering
		samplerCreateInfo.maxAnisotropy = 8;
		samplerCreateInfo.anisotropyEnable = VK_TRUE;
		// Use a different border color (than the normal texture loader) for additive blending
		samplerCreateInfo.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
		VK_CHECK_RESULT(vkCreateSampler(mVulkanDevice->mLogicalDevice, &samplerCreateInfo, nullptr, &textures.particles.sampler));
	}

	void loadMeshes()
	{
		loadMesh(getAssetPath() + "models/fireplace.obj", &meshes.environment.mMeshBuffers, vertexLayout, 10.0f);
		meshes.environment.setupVertexInputState(vertexLayout);
	}

	void setupVertexDescriptions()
	{
		// Binding description
		particles.bindingDescriptions.resize(1);
		particles.bindingDescriptions[0] =
			vkTools::vertexInputBindingDescription(
				VERTEX_BUFFER_BIND_ID,
				sizeof(Particle),
				VK_VERTEX_INPUT_RATE_VERTEX);

		// Attribute descriptions
		// Describes memory layout and shader positions
		// Location 0 : Position
		particles.attributeDescriptions.push_back(
			vkTools::vertexInputAttributeDescription(
				VERTEX_BUFFER_BIND_ID,
				0,
				VK_FORMAT_R32G32B32A32_SFLOAT,
				0));
		// Location 1 : Color
		particles.attributeDescriptions.push_back(
			vkTools::vertexInputAttributeDescription(
				VERTEX_BUFFER_BIND_ID,
				1,
				VK_FORMAT_R32G32B32A32_SFLOAT,
				sizeof(float) * 4));
		// Location 2 : Alpha
		particles.attributeDescriptions.push_back(
			vkTools::vertexInputAttributeDescription(
				VERTEX_BUFFER_BIND_ID,
				2,
				VK_FORMAT_R32_SFLOAT,
				sizeof(float) * 8));
		// Location 3 : Size
		particles.attributeDescriptions.push_back(
			vkTools::vertexInputAttributeDescription(
				VERTEX_BUFFER_BIND_ID,
				3,
				VK_FORMAT_R32_SFLOAT,
				sizeof(float) * 9));
		// Location 4 : Rotation
		particles.attributeDescriptions.push_back(
			vkTools::vertexInputAttributeDescription(
				VERTEX_BUFFER_BIND_ID,
				4,
				VK_FORMAT_R32_SFLOAT,
				sizeof(float) * 10));
		// Location 5 : Type
		particles.attributeDescriptions.push_back(
			vkTools::vertexInputAttributeDescription(
				VERTEX_BUFFER_BIND_ID,
				5,
				VK_FORMAT_R32_SINT,
				sizeof(float) * 11));

		particles.inputState = vkTools::pipelineVertexInputStateCreateInfo();
		particles.inputState.vertexBindingDescriptionCount = particles.bindingDescriptions.size();
		particles.inputState.pVertexBindingDescriptions = particles.bindingDescriptions.data();
		particles.inputState.vertexAttributeDescriptionCount = particles.attributeDescriptions.size();
		particles.inputState.pVertexAttributeDescriptions = particles.attributeDescriptions.data();
	}

	void setupDescriptorPool()
	{
		// Example uses one ubo and one image sampler
		std::vector<VkDescriptorPoolSize> poolSizes =
		{
			vkTools::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2),
			vkTools::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4)
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
			// Binding 1 : Fragment shader image sampler
			vkTools::descriptorSetLayoutBinding(
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT,
				1),
			// Binding 1 : Fragment shader image sampler
			vkTools::descriptorSetLayoutBinding(
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT,
				2)
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

	void setupDescriptorSets()
	{
		VkDescriptorSetAllocateInfo allocInfo =
			vkTools::descriptorSetAllocateInfo(
				descriptorPool,
				&descriptorSetLayout,
				1);

		VK_CHECK_RESULT(vkAllocateDescriptorSets(mVulkanDevice->mLogicalDevice, &allocInfo, &descriptorSet));

		// Image descriptor for the color map texture
		VkDescriptorImageInfo texDescriptorSmoke =
			vkTools::descriptorImageInfo(
				textures.particles.sampler,
				textures.particles.smoke.view,
				VK_IMAGE_LAYOUT_GENERAL);
		VkDescriptorImageInfo texDescriptorFire =
			vkTools::descriptorImageInfo(
				textures.particles.sampler,
				textures.particles.fire.view,
				VK_IMAGE_LAYOUT_GENERAL);

		std::vector<VkWriteDescriptorSet> writeDescriptorSets =
		{
			// Binding 0 : Vertex shader uniform buffer
			vkTools::writeDescriptorSet(
				descriptorSet,
				VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				0,
				&uniformData.fire.descriptor),
			// Binding 1 : Smoke texture
			vkTools::writeDescriptorSet(
				descriptorSet,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				1,
				&texDescriptorSmoke),
			// Binding 1 : Fire texture array
			vkTools::writeDescriptorSet(
				descriptorSet,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				2,
				&texDescriptorFire)
		};

		vkUpdateDescriptorSets(mVulkanDevice->mLogicalDevice, writeDescriptorSets.size(), writeDescriptorSets.data(), 0, NULL);

		// Environment
		VK_CHECK_RESULT(vkAllocateDescriptorSets(mVulkanDevice->mLogicalDevice, &allocInfo, &meshes.environment.descriptorSet));

		VkDescriptorImageInfo texDescriptorColorMap =
			vkTools::descriptorImageInfo(
				textures.floor.colorMap.sampler,
				textures.floor.colorMap.view,
				VK_IMAGE_LAYOUT_GENERAL);
		VkDescriptorImageInfo texDescriptorNormalMap =
			vkTools::descriptorImageInfo(
				textures.floor.normalMap.sampler,
				textures.floor.normalMap.view,
				VK_IMAGE_LAYOUT_GENERAL);

		writeDescriptorSets.clear();

		// Binding 0 : Vertex shader uniform buffer
		writeDescriptorSets.push_back(
			vkTools::writeDescriptorSet(
				meshes.environment.descriptorSet,
				VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				0,
				&uniformData.environment.descriptor));
		// Binding 1 : Color map
		writeDescriptorSets.push_back(
			vkTools::writeDescriptorSet(
				meshes.environment.descriptorSet,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				1,
				&texDescriptorColorMap));
		// Binding 2 : Normal map
		writeDescriptorSets.push_back(
			vkTools::writeDescriptorSet(
				meshes.environment.descriptorSet,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				2,
				&texDescriptorNormalMap));

		vkUpdateDescriptorSets(mVulkanDevice->mLogicalDevice, writeDescriptorSets.size(), writeDescriptorSets.data(), 0, NULL);
	}

	void preparePipelines()
	{
		VkPipelineInputAssemblyStateCreateInfo inputAssemblyState =
			vkTools::pipelineInputAssemblyStateCreateInfo(
				VK_PRIMITIVE_TOPOLOGY_POINT_LIST,
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

		// Load shaders
		std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

		shaderStages[0] = loadShader(getAssetPath() + "shaders/particlefire/particle.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getAssetPath() + "shaders/particlefire/particle.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

		VkGraphicsPipelineCreateInfo pipelineCreateInfo =
			vkTools::pipelineCreateInfo(
				pipelineLayout,
				mRenderPass,
				0);

		pipelineCreateInfo.pVertexInputState = &particles.inputState;
		pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
		pipelineCreateInfo.pRasterizationState = &rasterizationState;
		pipelineCreateInfo.pColorBlendState = &colorBlendState;
		pipelineCreateInfo.pMultisampleState = &multisampleState;
		pipelineCreateInfo.pViewportState = &viewportState;
		pipelineCreateInfo.pDepthStencilState = &depthStencilState;
		pipelineCreateInfo.pDynamicState = &dynamicState;
		pipelineCreateInfo.stageCount = shaderStages.size();
		pipelineCreateInfo.pStages = shaderStages.data();

		depthStencilState.depthWriteEnable = VK_FALSE;

		// Premulitplied alpha
		blendAttachmentState.blendEnable = VK_TRUE;
		blendAttachmentState.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
		blendAttachmentState.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		blendAttachmentState.colorBlendOp = VK_BLEND_OP_ADD;
		blendAttachmentState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		blendAttachmentState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
		blendAttachmentState.alphaBlendOp = VK_BLEND_OP_ADD;
		blendAttachmentState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

		VK_CHECK_RESULT(vkCreateGraphicsPipelines(mVulkanDevice->mLogicalDevice, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.particles));

		// Environment rendering pipeline (normal mapped)
		shaderStages[0] = loadShader(getAssetPath() + "shaders/particlefire/normalmap.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getAssetPath() + "shaders/particlefire/normalmap.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		pipelineCreateInfo.pVertexInputState = &meshes.environment.vertexInputState;
		blendAttachmentState.blendEnable = VK_FALSE;
		depthStencilState.depthWriteEnable = VK_TRUE;
		inputAssemblyState.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(mVulkanDevice->mLogicalDevice, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.environment));

		meshes.environment.pipeline = pipelines.environment;
		meshes.environment.pipelineLayout = pipelineLayout;
	}

	// Prepare and initialize uniform buffer containing shader uniforms
	void prepareUniformBuffers()
	{
		// Vertex shader uniform buffer block
		createBuffer(
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			sizeof(uboVS),
			&uboVS,
			&uniformData.fire.buffer,
			&uniformData.fire.memory,
			&uniformData.fire.descriptor);

		// Vertex shader uniform buffer block
		createBuffer(
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			sizeof(uboEnv),
			&uboEnv,
			&uniformData.environment.buffer,
			&uniformData.environment.memory,
			&uniformData.environment.descriptor);

		updateUniformBuffers();
	}

	void updateUniformBufferLight()
	{
		// Environment
		uboEnv.lightPos.x = sin(timer * 2 * M_PI) * 1.5f;
		uboEnv.lightPos.y = 0.0f;
		uboEnv.lightPos.z = cos(timer * 2 * M_PI) * 1.5f;
		uint8_t *pData;
		VK_CHECK_RESULT(vkMapMemory(mVulkanDevice->mLogicalDevice, uniformData.environment.memory, 0, sizeof(uboEnv), 0, (void **)&pData));
		memcpy(pData, &uboEnv, sizeof(uboEnv));
		vkUnmapMemory(mVulkanDevice->mLogicalDevice, uniformData.environment.memory);
	}

	void updateUniformBuffers()
	{
		// Vertex shader
		glm::mat4 viewMatrix = glm::mat4();
		uboVS.projection = glm::perspective(glm::radians(60.0f), (float)width / (float)height, 0.001f, 256.0f);
		viewMatrix = glm::translate(viewMatrix, glm::vec3(0.0f, 0.0f, mZoom));

		uboVS.model = glm::mat4();
		uboVS.model = viewMatrix * glm::translate(uboVS.model, glm::vec3(0.0f, 15.0f, 0.0f));
		uboVS.model = glm::rotate(uboVS.model, glm::radians(mRotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
		uboVS.model = glm::rotate(uboVS.model, glm::radians(mRotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
		uboVS.model = glm::rotate(uboVS.model, glm::radians(mRotation.z), glm::vec3(0.0f, 0.0f, 1.0f));

		uboVS.viewportDim = glm::vec2((float)width, (float)height);

		uint8_t *pData;
		VK_CHECK_RESULT(vkMapMemory(mVulkanDevice->mLogicalDevice, uniformData.fire.memory, 0, sizeof(uboVS), 0, (void **)&pData));
		memcpy(pData, &uboVS, sizeof(uboVS));
		vkUnmapMemory(mVulkanDevice->mLogicalDevice, uniformData.fire.memory);

		// Environment
		uboEnv.projection = uboVS.projection;
		uboEnv.model = uboVS.model;
		uboEnv.normal = glm::inverseTranspose(uboEnv.model);
		uboEnv.cameraPos = glm::vec4(0.0, 0.0, mZoom, 0.0);
		VK_CHECK_RESULT(vkMapMemory(mVulkanDevice->mLogicalDevice, uniformData.environment.memory, 0, sizeof(uboEnv), 0, (void **)&pData));
		memcpy(pData, &uboEnv, sizeof(uboEnv));
		vkUnmapMemory(mVulkanDevice->mLogicalDevice, uniformData.environment.memory);
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

	void prepare()
	{
		VulkanBase::prepare();
		loadTextures();
		prepareParticles();
		setupVertexDescriptions();
		prepareUniformBuffers();
		setupDescriptorSetLayout();
		loadMeshes();
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
		if (!paused)
		{
			updateUniformBufferLight();
			updateParticles();
		}
	}

	virtual void viewChanged()
	{
		updateUniformBuffers();
	}
};
