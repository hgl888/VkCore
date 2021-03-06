#include "vulkangear.h"
#include "VkCamera.hpp"

int32_t VulkanGear::newVertex(std::vector<VertexGear> *vBuffer, float x, float y, float z, const Vector3& normal)
{
	VertexGear v(Vector3(x, y, z), normal, color);
	vBuffer->push_back(v);
	return vBuffer->size() - 1;
}

void VulkanGear::newFace(std::vector<uint32_t> *iBuffer, int a, int b, int c)
{
	iBuffer->push_back(a);
	iBuffer->push_back(b);
	iBuffer->push_back(c);
}

VulkanGear::~VulkanGear()
{
	// Clean up vulkan resources
	vkDestroyBuffer(vulkanDevice->mLogicalDevice, uniformData.buffer, nullptr);
	vkFreeMemory(vulkanDevice->mLogicalDevice, uniformData.memory, nullptr);

	vertexBuffer.destroy();
	indexBuffer.destroy();
}

void VulkanGear::generate(GearInfo *gearinfo, VkQueue queue)
{
	this->color = gearinfo->color;
	this->pos = gearinfo->pos;
	this->rotOffset = gearinfo->rotOffset;
	this->rotSpeed = gearinfo->rotSpeed;

	std::vector<VertexGear> vBuffer;
	std::vector<uint32_t> iBuffer;

	int i, j;
	float r0, r1, r2;
	float ta, da;
	float u1, v1, u2, v2, len;
	float cos_ta, cos_ta_1da, cos_ta_2da, cos_ta_3da, cos_ta_4da;
	float sin_ta, sin_ta_1da, sin_ta_2da, sin_ta_3da, sin_ta_4da;
	int32_t ix0, ix1, ix2, ix3, ix4, ix5;

	r0 = gearinfo->innerRadius;
	r1 = gearinfo->outerRadius - gearinfo->toothDepth / 2.0;
	r2 = gearinfo->outerRadius + gearinfo->toothDepth / 2.0;
	da = 2.0 * M_PI / gearinfo->numTeeth / 4.0;

	Vector3 normal;

	for (i = 0; i < gearinfo->numTeeth; i++)
	{
		ta = i * 2.0 * M_PI / gearinfo->numTeeth;

		cos_ta = cos(ta);
		cos_ta_1da = cos(ta + da);
		cos_ta_2da = cos(ta + 2 * da);
		cos_ta_3da = cos(ta + 3 * da);
		cos_ta_4da = cos(ta + 4 * da);
		sin_ta = sin(ta);
		sin_ta_1da = sin(ta + da);
		sin_ta_2da = sin(ta + 2 * da);
		sin_ta_3da = sin(ta + 3 * da);
		sin_ta_4da = sin(ta + 4 * da);

		u1 = r2 * cos_ta_1da - r1 * cos_ta;
		v1 = r2 * sin_ta_1da - r1 * sin_ta;
		len = sqrt(u1 * u1 + v1 * v1);
		u1 /= len;
		v1 /= len;
		u2 = r1 * cos_ta_3da - r2 * cos_ta_2da;
		v2 = r1 * sin_ta_3da - r2 * sin_ta_2da;

		// front face
		normal = Vector3(0.0, 0.0, 1.0);
		ix0 = newVertex(&vBuffer, r0 * cos_ta, r0 * sin_ta, gearinfo->width * 0.5, normal);
		ix1 = newVertex(&vBuffer, r1 * cos_ta, r1 * sin_ta, gearinfo->width * 0.5, normal);
		ix2 = newVertex(&vBuffer, r0 * cos_ta, r0 * sin_ta, gearinfo->width * 0.5, normal);
		ix3 = newVertex(&vBuffer, r1 * cos_ta_3da, r1 * sin_ta_3da, gearinfo->width * 0.5, normal);
		ix4 = newVertex(&vBuffer, r0 * cos_ta_4da, r0 * sin_ta_4da, gearinfo->width * 0.5, normal);
		ix5 = newVertex(&vBuffer, r1 * cos_ta_4da, r1 * sin_ta_4da, gearinfo->width * 0.5, normal);
		newFace(&iBuffer, ix0, ix1, ix2);
		newFace(&iBuffer, ix1, ix3, ix2);
		newFace(&iBuffer, ix2, ix3, ix4);
		newFace(&iBuffer, ix3, ix5, ix4);

		// front sides of teeth
		normal = Vector3(0.0, 0.0, 1.0);
		ix0 = newVertex(&vBuffer, r1 * cos_ta, r1 * sin_ta, gearinfo->width * 0.5, normal);
		ix1 = newVertex(&vBuffer, r2 * cos_ta_1da, r2 * sin_ta_1da, gearinfo->width * 0.5, normal);
		ix2 = newVertex(&vBuffer, r1 * cos_ta_3da, r1 * sin_ta_3da, gearinfo->width * 0.5, normal);
		ix3 = newVertex(&vBuffer, r2 * cos_ta_2da, r2 * sin_ta_2da, gearinfo->width * 0.5, normal);
		newFace(&iBuffer, ix0, ix1, ix2);
		newFace(&iBuffer, ix1, ix3, ix2);

		// back face 
		normal = Vector3(0.0, 0.0, -1.0);
		ix0 = newVertex(&vBuffer, r1 * cos_ta, r1 * sin_ta, -gearinfo->width * 0.5, normal);
		ix1 = newVertex(&vBuffer, r0 * cos_ta, r0 * sin_ta, -gearinfo->width * 0.5, normal);
		ix2 = newVertex(&vBuffer, r1 * cos_ta_3da, r1 * sin_ta_3da, -gearinfo->width * 0.5, normal);
		ix3 = newVertex(&vBuffer, r0 * cos_ta, r0 * sin_ta, -gearinfo->width * 0.5, normal);
		ix4 = newVertex(&vBuffer, r1 * cos_ta_4da, r1 * sin_ta_4da, -gearinfo->width * 0.5, normal);
		ix5 = newVertex(&vBuffer, r0 * cos_ta_4da, r0 * sin_ta_4da, -gearinfo->width * 0.5, normal);
		newFace(&iBuffer, ix0, ix1, ix2);
		newFace(&iBuffer, ix1, ix3, ix2);
		newFace(&iBuffer, ix2, ix3, ix4);
		newFace(&iBuffer, ix3, ix5, ix4);

		// back sides of teeth 
		normal = Vector3(0.0, 0.0, -1.0);
		ix0 = newVertex(&vBuffer, r1 * cos_ta_3da, r1 * sin_ta_3da, -gearinfo->width * 0.5, normal);
		ix1 = newVertex(&vBuffer, r2 * cos_ta_2da, r2 * sin_ta_2da, -gearinfo->width * 0.5, normal);
		ix2 = newVertex(&vBuffer, r1 * cos_ta, r1 * sin_ta, -gearinfo->width * 0.5, normal);
		ix3 = newVertex(&vBuffer, r2 * cos_ta_1da, r2 * sin_ta_1da, -gearinfo->width * 0.5, normal);
		newFace(&iBuffer, ix0, ix1, ix2);
		newFace(&iBuffer, ix1, ix3, ix2);

		// draw outward faces of teeth 
		normal = Vector3(v1, -u1, 0.0);
		ix0 = newVertex(&vBuffer, r1 * cos_ta, r1 * sin_ta, gearinfo->width * 0.5, normal);
		ix1 = newVertex(&vBuffer, r1 * cos_ta, r1 * sin_ta, -gearinfo->width * 0.5, normal);
		ix2 = newVertex(&vBuffer, r2 * cos_ta_1da, r2 * sin_ta_1da, gearinfo->width * 0.5, normal);
		ix3 = newVertex(&vBuffer, r2 * cos_ta_1da, r2 * sin_ta_1da, -gearinfo->width * 0.5, normal);
		newFace(&iBuffer, ix0, ix1, ix2);
		newFace(&iBuffer, ix1, ix3, ix2);

		normal = Vector3(cos_ta, sin_ta, 0.0);
		ix0 = newVertex(&vBuffer, r2 * cos_ta_1da, r2 * sin_ta_1da, gearinfo->width * 0.5, normal);
		ix1 = newVertex(&vBuffer, r2 * cos_ta_1da, r2 * sin_ta_1da, -gearinfo->width * 0.5, normal);
		ix2 = newVertex(&vBuffer, r2 * cos_ta_2da, r2 * sin_ta_2da, gearinfo->width * 0.5, normal);
		ix3 = newVertex(&vBuffer, r2 * cos_ta_2da, r2 * sin_ta_2da, -gearinfo->width * 0.5, normal);
		newFace(&iBuffer, ix0, ix1, ix2);
		newFace(&iBuffer, ix1, ix3, ix2);

		normal = Vector3(v2, -u2, 0.0);
		ix0 = newVertex(&vBuffer, r2 * cos_ta_2da, r2 * sin_ta_2da, gearinfo->width * 0.5, normal);
		ix1 = newVertex(&vBuffer, r2 * cos_ta_2da, r2 * sin_ta_2da, -gearinfo->width * 0.5, normal);
		ix2 = newVertex(&vBuffer, r1 * cos_ta_3da, r1 * sin_ta_3da, gearinfo->width * 0.5, normal);
		ix3 = newVertex(&vBuffer, r1 * cos_ta_3da, r1 * sin_ta_3da, -gearinfo->width * 0.5, normal);
		newFace(&iBuffer, ix0, ix1, ix2);
		newFace(&iBuffer, ix1, ix3, ix2);

		normal = Vector3(cos_ta, sin_ta, 0.0);
		ix0 = newVertex(&vBuffer, r1 * cos_ta_3da, r1 * sin_ta_3da, gearinfo->width * 0.5, normal);
		ix1 = newVertex(&vBuffer, r1 * cos_ta_3da, r1 * sin_ta_3da, -gearinfo->width * 0.5, normal);
		ix2 = newVertex(&vBuffer, r1 * cos_ta_4da, r1 * sin_ta_4da, gearinfo->width * 0.5, normal);
		ix3 = newVertex(&vBuffer, r1 * cos_ta_4da, r1 * sin_ta_4da, -gearinfo->width * 0.5, normal);
		newFace(&iBuffer, ix0, ix1, ix2);
		newFace(&iBuffer, ix1, ix3, ix2);

		// draw inside radius cylinder 
		ix0 = newVertex(&vBuffer, r0 * cos_ta, r0 * sin_ta, -gearinfo->width * 0.5, Vector3(-cos_ta, -sin_ta, 0.0));
		ix1 = newVertex(&vBuffer, r0 * cos_ta, r0 * sin_ta, gearinfo->width * 0.5, Vector3(-cos_ta, -sin_ta, 0.0));
		ix2 = newVertex(&vBuffer, r0 * cos_ta_4da, r0 * sin_ta_4da, -gearinfo->width * 0.5, Vector3(-cos_ta_4da, -sin_ta_4da, 0.0));
		ix3 = newVertex(&vBuffer, r0 * cos_ta_4da, r0 * sin_ta_4da, gearinfo->width * 0.5, Vector3(-cos_ta_4da, -sin_ta_4da, 0.0));
		newFace(&iBuffer, ix0, ix1, ix2);
		newFace(&iBuffer, ix1, ix3, ix2);
	}

	int vertexBufferSize = vBuffer.size() * sizeof(VertexGear);
	int indexBufferSize = iBuffer.size() * sizeof(uint32_t);

	bool useStaging = true;

	if (useStaging)
	{
		vk::Buffer vertexStaging, indexStaging;

		// Create staging buffers
		// Vertex data
		vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
			&vertexStaging,
			vertexBufferSize,
			vBuffer.data());
		// Index data
		vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
			&indexStaging,
			indexBufferSize,
			iBuffer.data());

		// Create device local buffers
		// Vertex buffer
		vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			&vertexBuffer,
			vertexBufferSize);
		// Index buffer
		vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			&indexBuffer,
			indexBufferSize);

		// Copy from staging buffers
		VkCommandBuffer copyCmd = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

		VkBufferCopy copyRegion = {};

		copyRegion.size = vertexBufferSize;
		vkCmdCopyBuffer(
			copyCmd,
			vertexStaging.buffer,
			vertexBuffer.buffer,
			1,
			&copyRegion);

		copyRegion.size = indexBufferSize;
		vkCmdCopyBuffer(
			copyCmd,
			indexStaging.buffer,
			indexBuffer.buffer,
			1,
			&copyRegion);

		vulkanDevice->flushCommandBuffer(copyCmd, queue, true);

		vkDestroyBuffer(vulkanDevice->mLogicalDevice, vertexStaging.buffer, nullptr);
		vkFreeMemory(vulkanDevice->mLogicalDevice, vertexStaging.memory, nullptr);
		vkDestroyBuffer(vulkanDevice->mLogicalDevice, indexStaging.buffer, nullptr);
		vkFreeMemory(vulkanDevice->mLogicalDevice, indexStaging.memory, nullptr);
	}
	else
	{
		// Vertex buffer
		vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
			&vertexBuffer,
			vertexBufferSize,
			vBuffer.data());
		// Index buffer
		vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
			&indexBuffer,
			indexBufferSize,
			iBuffer.data());
	}

	indexCount = iBuffer.size();

	prepareUniformBuffer();
}

void VulkanGear::draw(VkCommandBuffer cmdbuffer, VkPipelineLayout pipelineLayout)
{
	VkDeviceSize offsets[1] = { 0 };
	vkCmdBindDescriptorSets(cmdbuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, NULL);
	vkCmdBindVertexBuffers(cmdbuffer, 0, 1, &vertexBuffer.buffer, offsets);
	vkCmdBindIndexBuffer(cmdbuffer, indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
	vkCmdDrawIndexed(cmdbuffer, indexCount, 1, 0, 0, 1);
}

void VulkanGear::updateUniformBuffer(Matrix perspective, Vector3 rotation, float zoom, float timer)
{
	ubo.projection = perspective;

	Matrix::createLookAt(
		Vector3(0, 0, -zoom),
		Vector3(-1.0, -1.5, 0),
		Vector3(0, 1, 0),
		&ubo.view);
	ubo.view.rotateX(MATH_DEG_TO_RAD(rotation.x));
	ubo.view.rotateY(MATH_DEG_TO_RAD(rotation.y));

	//ubo.view = glm::lookAt(
	//	glm::vec3(0, 0, -zoom),
	//	glm::vec3(-1.0, -1.5, 0),
	//	glm::vec3(0, 1, 0)
	//	);
	//ubo.view = glm::rotate(ubo.view, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
	//ubo.view = glm::rotate(ubo.view, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
	rotation.z = (rotSpeed * timer) + rotOffset;
	ubo.model.setIdentity();
	ubo.model.translate(pos);
	ubo.model.rotateZ(MATH_DEG_TO_RAD(rotation.z));

	//ubo.model = glm::mat4();
	//ubo.model = glm::translate(ubo.model, pos);
	//ubo.model = glm::rotate(ubo.model, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));

	ubo.normal = Matrix(ubo.view * ubo.model);
	ubo.normal.invert();
	ubo.normal.transpose();

	//ubo.normal = glm::inverseTranspose(ubo.view * ubo.model);

	ubo.lightPos = Vector3(0.0f, 0.0f, 2.5f);
	ubo.lightPos.x = sin(MATH_DEG_TO_RAD(timer)) * 8.0f;
	ubo.lightPos.z = cos(MATH_DEG_TO_RAD(timer)) * 8.0f;

	uint8_t *pData;
	VK_CHECK_RESULT(vkMapMemory(vulkanDevice->mLogicalDevice, uniformData.memory, 0, sizeof(ubo), 0, (void **)&pData));
	memcpy(pData, &ubo, sizeof(ubo));
	vkUnmapMemory(vulkanDevice->mLogicalDevice, uniformData.memory);
}

void VulkanGear::setupDescriptorSet(VkDescriptorPool pool, VkDescriptorSetLayout descriptorSetLayout)
{
	VkDescriptorSetAllocateInfo allocInfo =
		vkTools::descriptorSetAllocateInfo(
			pool,
			&descriptorSetLayout,
			1);

	VK_CHECK_RESULT(vkAllocateDescriptorSets(vulkanDevice->mLogicalDevice, &allocInfo, &descriptorSet));

	// Binding 0 : Vertex shader uniform buffer
	VkWriteDescriptorSet writeDescriptorSet =
		vkTools::writeDescriptorSet(
			descriptorSet,
			VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			0,
			&uniformData.descriptor);

	vkUpdateDescriptorSets(vulkanDevice->mLogicalDevice, 1, &writeDescriptorSet, 0, NULL);
}

void VulkanGear::prepareUniformBuffer()
{
	// Vertex shader uniform buffer block
	VkMemoryAllocateInfo allocInfo = vkTools::memoryAllocateInfo();
	VkMemoryRequirements memReqs;

	VkBufferCreateInfo bufferInfo = vkTools::bufferCreateInfo(
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		sizeof(ubo));

	VK_CHECK_RESULT(vkCreateBuffer(vulkanDevice->mLogicalDevice, &bufferInfo, nullptr, &uniformData.buffer));
	vkGetBufferMemoryRequirements(vulkanDevice->mLogicalDevice, uniformData.buffer, &memReqs);
	allocInfo.allocationSize = memReqs.size;
	allocInfo.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	VK_CHECK_RESULT(vkAllocateMemory(vulkanDevice->mLogicalDevice, &allocInfo, nullptr, &uniformData.memory));
	VK_CHECK_RESULT(vkBindBufferMemory(vulkanDevice->mLogicalDevice, uniformData.buffer, uniformData.memory, 0));

	uniformData.descriptor.buffer = uniformData.buffer;
	uniformData.descriptor.offset = 0;
	uniformData.descriptor.range = sizeof(ubo);
	uniformData.allocSize = allocInfo.allocationSize;
}
