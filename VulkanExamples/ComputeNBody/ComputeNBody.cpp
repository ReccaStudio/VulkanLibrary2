/*
* Vulkan Example - Compute shader N-body simulation using two passes and shared compute shader memory
*
* This sample shows how to combine compute and graphics for doing N-body particle simulaton
* It calculates the particle system movement using two separate compute passes: calculating particle positions and integrating particles
* For that a shader storage buffer is used which is then used as a vertex buffer for drawing the particle system with a graphics pipeline
* To optimize performance, the compute shaders use shared memory
*
* Copyright (C) 2016-2023 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include "VulkanExampleBase.h"

#define VERTEX_BUFFER_BIND_ID 0
#define ENABLE_VALIDATION true

#if defined(__ANDROID__)
	// Lower particle count on Android for performance reasons
#define PARTICLES_PER_ATTRACTOR 3*1024
#else
#define PARTICLES_PER_ATTRACTOR 4*1024
#endif

class VulkanExample:public VulkanExampleBase
{
public:
	uint32_t numParticles;

	struct
	{
		vks::Texture2D particle;
		vks::Texture2D gradient;
	} textures;

	struct
	{
		VkPipelineVertexInputStateCreateInfo inputState;
		std::vector<VkVertexInputBindingDescription> bindingDescriptions;
		std::vector<VkVertexInputAttributeDescription> attributeDescriptions;
	} vertices;

	// Resources for the graphics part of the example
	struct
	{
		uint32_t queueFamilyIndex; // Used to check if compute and graphics queue families differ and require additional barriers
		vks::Buffer uniformBuffer; // Contains scene matrices
		VkDescriptorSetLayout descriptorSetLayout; // Particle systerm rendering shader binding layout
		VkDescriptorSet descriptorSet; // Particle system rendering shader bindings
		VkPipelineLayout pipelineLayout; //Layout of the graphics pipeline
		VkPipeline pipeline; //Particle rendering pipeline
		VkSemaphore semaphore; // Executing dependency between compute & graphic submission

		struct
		{
			glm::mat4 projection;
			glm::mat4 view;
			glm::vec2 screenDim;
		} ubo;
	} graphics;

	// Resources for the compute part of the example
	struct
	{
		uint32_t queueFamilyIndex; // Used to check if compute and graphics queue families differ and require addtional barriers
		vks::Buffer storageBuffer;  // (Shader) storage buffer object containing the particles
		vks::Buffer uniformBuffer; // Uniform buffer object containing
		VkQueue queue; //Separate queue for compute commands (queue family may differ from the one used for graphics)
		VkCommandPool commandPool; // Use a separate command pool (queue family may differ from the one used for graphics)
		VkCommandBuffer commandBuffer; // Command buffer storing the dispatch commands and barriers
		VkSemaphore semaphore; // execution dependency between compute & graphics submission
		VkDescriptorSetLayout descriptorSetLayout;// Compute shader binding layout
		VkDescriptorSet descriptorSet; // Compute shader bindings
		VkPipelineLayout pipelineLayout; // Layout of the compute pipeline
		VkPipeline pipelineCalculate; // Compute pipeline for N-body velocity calculation (1st pass)
		VkPipeline pipelineIntegrate; // compute pipeline for eular integration (2nd pass)

		//VkPipeline blur;
		//VkPipelineLayout pipelineLayoutBlur;
		//VkDescriptorSetLayout descriptorSetLayoutBlur;
		//VkDescriptorSet descriptorSetBlur;

		struct computeUBO // Compute shader uniform block object
		{
			float deltaT;  // Frame delta time
			int32_t particleCount;
		} ubo;
	} compute;

	// SSBO particle declaration
	struct Particle
	{
		glm::vec4 pos; // xyz = position, w = mass
		glm::vec4 vel; //xyz = velocity,w = gradient texture position
	};

	VulkanExample() : VulkanExampleBase()
	{
		windowTitle = "Compute shader N-body system";
		camera.cameraType = Camera::CameraType::lookat;
		camera.setPerspective(60.0f, (float)width / (float)height, 0.1f, 512.0f);
		camera.setRotation(glm::vec3(-26.0f, 75.0f, 0.0f));
		camera.setTranslation(glm::vec3(0.0f, 0.0f, -14.0f));
		camera.movementSpeed = 2.5f;
	}

	~VulkanExample()
	{
		// Graphics
		graphics.uniformBuffer.destroy();
		vkDestroyPipeline(device, graphics.pipeline, nullptr);
		vkDestroyPipelineLayout(device, graphics.pipelineLayout, nullptr);
		vkDestroyDescriptorSetLayout(device, graphics.descriptorSetLayout, nullptr);
		vkDestroySemaphore(device, graphics.semaphore, nullptr);

		// Compute
		compute.storageBuffer.destroy();
		compute.uniformBuffer.destroy();
		vkDestroyCommandPool(device, compute.commandPool, nullptr);
		vkDestroySemaphore(device,compute.semaphore,nullptr);
		vkDestroyDescriptorSetLayout(device, compute.descriptorSetLayout, nullptr);
		vkDestroyPipelineLayout(device, compute.pipelineLayout,nullptr);
		vkDestroyPipeline(device, compute.pipelineCalculate, nullptr);
		vkDestroyPipeline(device, compute.pipelineIntegrate, nullptr);

		textures.gradient.destroy();
		textures.particle.destroy();
	}

	void loadAssets()
	{
		textures.particle.loadFromFile(getAssetPath() + "textures/particle01_rgba.ktx", VK_FORMAT_R8G8B8A8_UNORM, vulkanDevice, queue);
		textures.gradient.loadFromFile(getAssetPath() + "textures/particle_gradient_rgba.ktx", VK_FORMAT_R8G8B8A8_UNORM, vulkanDevice, queue);
	}

	void setupDescriptorPool()
	{
		std::vector<VkDescriptorPoolSize> poolSizes =
		{
			vks::initializers::GenDescriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,2),
			vks::initializers::GenDescriptorPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1),
			vks::initializers::GenDescriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,2)
		};

		VkDescriptorPoolCreateInfo descriptorPoolInfo = vks::initializers::GenDescriptorPoolCreateInfo(static_cast<uint32_t>(poolSizes.size()), poolSizes.data(), 2);

		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));
	}

	// Setup and fill the compute shader storage buffers containing the particles
	void prepareStorageBuffers()
	{
#if 0
		std::vector<glm::vec3> attractors = {
			glm::vec3(2.5f, 1.5f, 0.0f),
			glm::vec3(-2.5f, -1.5f, 0.0f),
		};
#else
		std::vector<glm::vec3> attractors = {
			glm::vec3(5.0f, 0.0f, 0.0f),
			glm::vec3(-5.0f, 0.0f, 0.0f),
			glm::vec3(0.0f, 0.0f, 5.0f),
			glm::vec3(0.0f, 0.0f, -5.0f),
			glm::vec3(0.0f, 4.0f, 0.0f),
			glm::vec3(0.0f, -8.0f, 0.0f),
		};
#endif

#define ATTRACTORS_SIZE static_cast<uint32_t>(attractors.size())

		numParticles = static_cast<uint32_t>(attractors.size())*PARTICLES_PER_ATTRACTOR;

		// Initial particle positions
		std::vector<Particle> particleBuffer(numParticles);

		std::default_random_engine randEngine(benchmark.active ? 0 : (unsigned)time(nullptr));
		std::normal_distribution<float> randomDist(0.0f, 1.0f);

		for (uint32_t i = 0; i < ATTRACTORS_SIZE; i++)
		{
			for (uint32_t j = 0; j < PARTICLES_PER_ATTRACTOR; j++)
			{
				Particle &particle = particleBuffer[i*PARTICLES_PER_ATTRACTOR + j];

				// First particle in group as heavy center of gravity
				if (j==0)
				{
					particle.pos = glm::vec4(attractors[i] * 1.5f, 90000.0f);
					particle.vel = glm::vec4(glm::vec4(0.0f));
				}
				else
				{
					// Position
					glm::vec3 position(attractors[i] + glm::vec3(randomDist(randEngine), randomDist(randEngine), randomDist(randEngine)) * 0.75f);
					float len = glm::length(glm::normalize(position - attractors[i]));
					position.y *= 2.0f - (len * len);

					// Velocity
					glm::vec3 angular = glm::vec3(0.5f, 1.5f, 0.5f) * (((i % 2) == 0) ? 1.0f : -1.0f);
					glm::vec3 velocity = glm::cross((position - attractors[i]), angular) + glm::vec3(randomDist(randEngine), randomDist(randEngine), randomDist(randEngine) * 0.025f);

					float mass = (randomDist(randEngine) * 0.5f + 0.5f) * 75.0f;
					particle.pos = glm::vec4(position, mass);
					particle.vel = glm::vec4(velocity, 0.0f);
				}//if_else_j

				// Color gradient offset
				particle.vel.w = (float)i*1.0f / ATTRACTORS_SIZE;
			}//for_j
		}//for_i

		compute.ubo.particleCount = numParticles;

		VkDeviceSize storageBufferSize = particleBuffer.size() * sizeof(Particle);

		// Staging
		// SSBO won't be changed on the host after upload so copy to device local memory

		vks::Buffer stagingBuffer;

		vulkanDevice->CreateBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&stagingBuffer, storageBufferSize, particleBuffer.data());

		// The SSBO will be used as a storage buffer for the compute pipeline and as a vertex buffer in the graphics pipeline
		vulkanDevice->CreateBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &compute.storageBuffer, storageBufferSize);

		// Copy from staging buffer to storage buffer
		VkCommandBuffer copyCmd = vulkanDevice->CreateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
		VkBufferCopy copyRegion = {};
		copyRegion.size = storageBufferSize;
		vkCmdCopyBuffer(copyCmd, stagingBuffer.buffer, compute.storageBuffer.buffer, 1, &copyRegion);
		// Execute a transfer barrier to the compute queue,if necessary
		if (graphics.queueFamilyIndex != compute.queueFamilyIndex)
		{
			VkBufferMemoryBarrier buffer_barrier =
			{
				VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,nullptr,VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT,0,
				graphics.queueFamilyIndex,compute.queueFamilyIndex,compute.storageBuffer.buffer,0,compute.storageBuffer.size
			};

			vkCmdPipelineBarrier(copyCmd, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0,
				0, nullptr, 1, &buffer_barrier, 0, nullptr);
		}
		vulkanDevice->FlushCommandBuffer(copyCmd, queue, true);
		stagingBuffer.destroy();

		// Binding description
		vertices.bindingDescriptions.resize(1);
		vertices.bindingDescriptions[0] = vks::initializers::GenVertexInputBindingDescription(VERTEX_BUFFER_BIND_ID, sizeof(Particle), VK_VERTEX_INPUT_RATE_VERTEX);

		// Attribute descriptions
		// Describes memory layout and shader positions
		vertices.attributeDescriptions.resize(2);
		// Location 0: Position
		vertices.attributeDescriptions[0] = vks::initializers::GenVertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Particle, pos));
		// Location 1: Velocity (used for gradient lookup)
		vertices.attributeDescriptions[1] = vks::initializers::GenVertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Particle, vel));

		// Assign to vertex buffer
		vertices.inputState = vks::initializers::GenPipelineVertexInputStateCreateInfo();
		vertices.inputState.vertexBindingDescriptionCount = static_cast<uint32_t>(vertices.bindingDescriptions.size());
		vertices.inputState.pVertexBindingDescriptions = vertices.bindingDescriptions.data();
		vertices.inputState.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertices.attributeDescriptions.size());
		vertices.inputState.pVertexAttributeDescriptions = vertices.attributeDescriptions.data();
	}

	void updateComputeUniformBuffers()
	{
		compute.ubo.deltaT = paused ? 0.0f : frameTimer * 0.05f;
		memcpy(compute.uniformBuffer.mappedData, &compute.ubo, sizeof(compute.ubo));
	}

	void updateGraphicsUniformBuffers()
	{
		graphics.ubo.projection = camera.matrices.perspective;
		graphics.ubo.view = camera.matrices.view;
		graphics.ubo.screenDim = glm::vec2((float)width, (float)height);

		memcpy(graphics.uniformBuffer.mappedData, &graphics.ubo, sizeof(graphics.ubo));
	}

	// Prepare and initialize uniform buffer containing shader uniforms
	void prepareUniformBuffers()
	{
		// Compute shader uniform buffer block
		vulkanDevice->CreateBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &compute.uniformBuffer, sizeof(compute.ubo));
		// Map for host access
		VK_CHECK_RESULT(compute.uniformBuffer.map());

		// Vertex shader uniform buffer block
		vulkanDevice->CreateBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &graphics.uniformBuffer, sizeof(graphics.ubo));
		// Map for host access
		VK_CHECK_RESULT(graphics.uniformBuffer.map());

		updateComputeUniformBuffers();
		updateGraphicsUniformBuffers();
	}

	void setupDescriptorSetLayoutAndPipelineLayout()
	{
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings;
		setLayoutBindings =
		{
			vks::initializers::GenDescriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,VK_SHADER_STAGE_FRAGMENT_BIT,0),
			vks::initializers::GenDescriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,VK_SHADER_STAGE_FRAGMENT_BIT,1),
			vks::initializers::GenDescriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,VK_SHADER_STAGE_VERTEX_BIT,2),
		};

		VkDescriptorSetLayoutCreateInfo descriptorLayoutCI = vks::initializers::GenDescriptorSetLayoutCreateInfo(setLayoutBindings.data(), static_cast<uint32_t>(setLayoutBindings.size()));
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayoutCI, nullptr, &graphics.descriptorSetLayout));

		VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = vks::initializers::GenPipelineLayoutCreateInfo(&graphics.descriptorSetLayout, 1);
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &graphics.pipelineLayout));
	}

	void preparePipelines()
	{
		VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCI = vks::initializers::GenPipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_POINT_LIST, 0, VK_FALSE);

		VkPipelineRasterizationStateCreateInfo rasterizationStateCI = vks::initializers::GenPipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE, 0);

		VkPipelineColorBlendAttachmentState blendAttachmentState = vks::initializers::GenPipelineColorBlendAttachmentState(0xf, VK_FALSE);
		VkPipelineColorBlendStateCreateInfo colorBlendStateCI = vks::initializers::GenPipelineColorBlendStateCreateInfo(1, &blendAttachmentState);

		VkPipelineDepthStencilStateCreateInfo depthStencilStateCI = vks::initializers::GenPipelineDepthStencilStateCreateInfo(VK_FALSE, VK_FALSE, VK_COMPARE_OP_ALWAYS);

		VkPipelineViewportStateCreateInfo viewportStateCI = vks::initializers::GenPipelineViewportStateCreateInfo(1, 1, 0);

		VkPipelineMultisampleStateCreateInfo  multisampleStateCI= vks::initializers::GenPipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT, 0);

		std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dynamicStateCI = vks::initializers::GenPipelineDynamicStateCreateInfo(dynamicStateEnables.data(), static_cast<uint32_t>(dynamicStateEnables.size()), 0);

		// Rendering pipeline
		// Load shaders
		std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

		shaderStages[0] = loadShader(getShadersPath() + "computenbody/particle.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "computenbody/particle.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

		VkGraphicsPipelineCreateInfo pipelineCreateInfo = vks::initializers::GenPipelineCreateInfo(graphics.pipelineLayout, renderPass, 0);
		pipelineCreateInfo.pVertexInputState = &vertices.inputState;
		pipelineCreateInfo.pInputAssemblyState = &inputAssemblyStateCI;
		pipelineCreateInfo.pRasterizationState = &rasterizationStateCI;
		pipelineCreateInfo.pColorBlendState = &colorBlendStateCI;
		pipelineCreateInfo.pDepthStencilState = &depthStencilStateCI;
		pipelineCreateInfo.pViewportState = &viewportStateCI;
		pipelineCreateInfo.pMultisampleState = &multisampleStateCI;
		pipelineCreateInfo.pDynamicState = &dynamicStateCI;
		pipelineCreateInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
		pipelineCreateInfo.pStages = shaderStages.data();
		pipelineCreateInfo.renderPass = renderPass;

		// Additive blending
		blendAttachmentState.colorWriteMask = 0xF;
		blendAttachmentState.blendEnable = VK_TRUE;
		blendAttachmentState.colorBlendOp = VK_BLEND_OP_ADD;
		blendAttachmentState.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
		blendAttachmentState.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
		blendAttachmentState.alphaBlendOp = VK_BLEND_OP_ADD;
		blendAttachmentState.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		blendAttachmentState.dstAlphaBlendFactor = VK_BLEND_FACTOR_DST_ALPHA;

		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &graphics.pipeline));
	}

	void setupDescriptorSetAndUpdate()
	{
		VkDescriptorSetAllocateInfo descriptorSetAllocInfo = vks::initializers::GenDescriptorSetAllocateInfo(descriptorPool, &graphics.descriptorSetLayout, 1);
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &descriptorSetAllocInfo, &graphics.descriptorSet));

		std::vector<VkWriteDescriptorSet> writeDescriptorSets;
		writeDescriptorSets =
		{
			vks::initializers::GenWriteDescriptorSet(graphics.descriptorSet,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,0,&textures.particle.descriptorImageInfo),
			vks::initializers::GenWriteDescriptorSet(graphics.descriptorSet,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,&textures.gradient.descriptorImageInfo),
			vks::initializers::GenWriteDescriptorSet(graphics.descriptorSet,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,2,&graphics.uniformBuffer.descriptorBufferInfo),
		};

		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
	}

	void prepareGraphics()
	{
		prepareStorageBuffers();
		prepareUniformBuffers();
		setupDescriptorSetLayoutAndPipelineLayout();
		preparePipelines();
		setupDescriptorSetAndUpdate();

		// Semaphores for compute & graphics sync
		VkSemaphoreCreateInfo semaphoreCreateInfo = vks::initializers::GenSemaphoreCreateInfo();
		VK_CHECK_RESULT(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &graphics.semaphore));
	}

	void buildComputeCommandBuffer()
	{
		VkCommandBufferBeginInfo cmdBufferInfo = vks::initializers::GenCommandBufferBeginInfo();

		VK_CHECK_RESULT(vkBeginCommandBuffer(compute.commandBuffer, &cmdBufferInfo));

		// Acquire barrier
		if (graphics.queueFamilyIndex != compute.queueFamilyIndex)
		{
			VkBufferMemoryBarrier toCpmputeBufferBarrier =
			{
				VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,nullptr,0,VK_ACCESS_SHADER_WRITE_BIT,
				graphics.queueFamilyIndex,compute.queueFamilyIndex,compute.storageBuffer.buffer,0,compute.storageBuffer.size
			};

			vkCmdPipelineBarrier(compute.commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
				0, nullptr, 1, &toCpmputeBufferBarrier, 0, nullptr);
		}

		// First pass: Calculate particle movement
		// ------------------------------------------------
		vkCmdBindPipeline(compute.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.pipelineCalculate);
		vkCmdBindDescriptorSets(compute.commandBuffer,VK_PIPELINE_BIND_POINT_COMPUTE, compute.pipelineLayout, 0, 1, &compute.descriptorSet, 0, 0);
		vkCmdDispatch(compute.commandBuffer, numParticles / 256, 1, 1);

		// Add memory barrier to ensure that the computer shader has finished writing to the buffer
		VkBufferMemoryBarrier secondComputePassBufferBarrier = vks::initializers::GenBufferMemoryBarrier();
		secondComputePassBufferBarrier.buffer = compute.storageBuffer.buffer;
		secondComputePassBufferBarrier.size = compute.storageBuffer.descriptorBufferInfo.range;
		secondComputePassBufferBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		secondComputePassBufferBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		// Transfer owernship if compute and graphics queue family indices differ
		secondComputePassBufferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		secondComputePassBufferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

		vkCmdPipelineBarrier(compute.commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_FLAGS_NONE, 0, nullptr, 1, &secondComputePassBufferBarrier, 0, nullptr);

		// Second pass: Integrate particles
		// ------------------------------------------
		vkCmdBindPipeline(compute.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.pipelineIntegrate);
		vkCmdDispatch(compute.commandBuffer, numParticles / 256, 1, 1);

		// Release barrier
		if (graphics.queueFamilyIndex != compute.queueFamilyIndex)
		{
			VkBufferMemoryBarrier computeToGraphicBufferBarrier =
			{
				VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,nullptr,VK_ACCESS_SHADER_WRITE_BIT,0,
				compute.queueFamilyIndex,graphics.queueFamilyIndex,compute.storageBuffer.buffer,0,compute.storageBuffer.size
			};

			vkCmdPipelineBarrier(compute.commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
				0, 0, nullptr, 1, &computeToGraphicBufferBarrier, 0, nullptr);
		}

		vkEndCommandBuffer(compute.commandBuffer);
	}

	void prepareCompute()
	{
		// Create a compute capable device queue
		// The VulkanDevice::createLogicalDevice functions finds a compute capable queue and prefers queue families that only support compute
		// Depending on the implementation this may result in different queue family indices for graphics and computes,
		// requiring proper synchronization (see the memory barriers in buildComputeCommandBuffer)
		vkGetDeviceQueue(device, compute.queueFamilyIndex, 0, &compute.queue);

		// Create compute pipeline
		// Compute pipelines are created separate from graphics pipelines even if they use the same queue (family index)
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings =
		{
			//Binding 0 : Particle position storage buffer
			vks::initializers::GenDescriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,VK_SHADER_STAGE_COMPUTE_BIT,0),
			// Bindging 1:Uniform buffer
			vks::initializers::GenDescriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,VK_SHADER_STAGE_COMPUTE_BIT,1),
		};

		VkDescriptorSetLayoutCreateInfo descriptorLayoutCI = vks::initializers::GenDescriptorSetLayoutCreateInfo(setLayoutBindings);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayoutCI, nullptr, &compute.descriptorSetLayout));

		VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = vks::initializers::GenPipelineLayoutCreateInfo(&compute.descriptorSetLayout, 1);
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &compute.pipelineLayout));

		VkDescriptorSetAllocateInfo descriptorSetAllocInfo = vks::initializers::GenDescriptorSetAllocateInfo(descriptorPool, &compute.descriptorSetLayout, 1);
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &descriptorSetAllocInfo, &compute.descriptorSet));
		std::vector<VkWriteDescriptorSet> computeWriteDescriptorSets =
		{
			// Binding 0: Particle position storage buffer
			vks::initializers::GenWriteDescriptorSet(compute.descriptorSet,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,0,&compute.storageBuffer.descriptorBufferInfo),
			// Binding 1: Uniform buffer
			vks::initializers::GenWriteDescriptorSet(compute.descriptorSet,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,&compute.uniformBuffer.descriptorBufferInfo),
		};
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(computeWriteDescriptorSets.size()), computeWriteDescriptorSets.data(), 0, nullptr);

		// Create pipeline
		VkComputePipelineCreateInfo computePipelineCreateInfo = vks::initializers::GenComputePipelineCreateInfo(compute.pipelineLayout, 0);

		// 1st pass
		computePipelineCreateInfo.stage = loadShader(getShadersPath() + "computenbody/particle_calculate.comp.spv", VK_SHADER_STAGE_COMPUTE_BIT);

		// Set shader parameters via specialization constatnts
		struct SpecializationData
		{
			uint32_t sharedDataSize;
			float gravity;
			float power;
			float soften;
		} specializationData;

		std::vector<VkSpecializationMapEntry> specializationMapEntries;
		specializationMapEntries.push_back(vks::initializers::GenSpecializationMapEntry(0, offsetof(SpecializationData, sharedDataSize), sizeof(uint32_t)));
		specializationMapEntries.push_back(vks::initializers::GenSpecializationMapEntry(1, offsetof(SpecializationData, gravity), sizeof(float)));
		specializationMapEntries.push_back(vks::initializers::GenSpecializationMapEntry(2, offsetof(SpecializationData, power), sizeof(float)));
		specializationMapEntries.push_back(vks::initializers::GenSpecializationMapEntry(3, offsetof(SpecializationData, soften), sizeof(float)));

		specializationData.sharedDataSize = std::min((uint32_t)1024,
			(uint32_t)(vulkanDevice->properties.limits.maxComputeSharedMemorySize / sizeof(glm::vec4)) );
		specializationData.gravity = 0.002f;
		specializationData.power = 0.75f;
		specializationData.soften = 0.05f;

		VkSpecializationInfo specialzationInfo = vks::initializers::GenSpecializationInfo(specializationMapEntries, sizeof(specializationData), &specializationData);
		computePipelineCreateInfo.stage.pSpecializationInfo = &specialzationInfo;

		VK_CHECK_RESULT(vkCreateComputePipelines(device, pipelineCache, 1, &computePipelineCreateInfo, nullptr, &compute.pipelineCalculate));


		// 2nd pass
		computePipelineCreateInfo.stage = loadShader(getShadersPath() + "computenbody/particle_integrate.comp.spv", VK_SHADER_STAGE_COMPUTE_BIT);
		VK_CHECK_RESULT(vkCreateComputePipelines(device, pipelineCache, 1, &computePipelineCreateInfo, nullptr, &compute.pipelineIntegrate));


		// Separate command pool as queue family for compute may be different than graphics
		VkCommandPoolCreateInfo cmdPoolInfo = {};
		cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		cmdPoolInfo.queueFamilyIndex = compute.queueFamilyIndex;
		cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		VK_CHECK_RESULT(vkCreateCommandPool(device, &cmdPoolInfo, nullptr, &compute.commandPool));

		// Create a command buffer for compute operations
		compute.commandBuffer = vulkanDevice->CreateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, compute.commandPool);

		// Semaphore for compute & graphics sync
		VkSemaphoreCreateInfo semaphoreCreateInfo = vks::initializers::GenSemaphoreCreateInfo();
		VK_CHECK_RESULT(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &compute.semaphore));

		// Signal the semaphore
		VkSubmitInfo submitInfo = vks::initializers::GenSubmitInfo();
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = &compute.semaphore;
		VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));
		VK_CHECK_RESULT(vkQueueWaitIdle(queue));

		// Build a single command buffer containing the compute dispatch commands
		buildComputeCommandBuffer();

		if (graphics.queueFamilyIndex != compute.queueFamilyIndex)
		{
			// Create a transient command buffer for setting up the initial buffer transfer state
			VkCommandBuffer transferCmd = vulkanDevice->CreateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, compute.commandPool, true);

			//VkBufferMemoryBarrier acquireBufferBarrier =
			//{
			//	VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,nullptr,0,VK_ACCESS_SHADER_WRITE_BIT,
			//	graphics.queueFamilyIndex,compute.queueFamilyIndex,compute.storageBuffer.buffer,0,compute.storageBuffer.size
			//};
			//vkCmdPipelineBarrier(transferCmd, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
			//	0, nullptr, 1, &acquireBufferBarrier, 0, nullptr);

			//VkBufferMemoryBarrier releaseBufferBarrier =
			//{
			//	VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,nullptr,VK_ACCESS_SHADER_WRITE_BIT,0,
			//	compute.queueFamilyIndex,graphics.queueFamilyIndex,compute.storageBuffer.buffer,0,compute.storageBuffer.size
			//};
			//vkCmdPipelineBarrier(transferCmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0,
			//	0, nullptr, 1, &releaseBufferBarrier, 0, nullptr);

			vulkanDevice->FlushCommandBuffer(transferCmd, compute.queue, compute.commandPool);
		}//if
	}

	void buildCommandBuffersForPreRenderPrmitives()
	{
		VkCommandBufferBeginInfo cmdBufBeginInfo = vks::initializers::GenCommandBufferBeginInfo();

		VkClearValue clearValues[2];
		clearValues[0].color = { {0.0f,0.0f,0.0f,1.0f} };
		clearValues[1].depthStencil = { 1.0f,0 };

		VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::GenRenderPassBeginInfo();
		renderPassBeginInfo.renderPass = renderPass;
		renderPassBeginInfo.renderArea.offset.x = 0;
		renderPassBeginInfo.renderArea.offset.y = 0;
		renderPassBeginInfo.renderArea.extent.width = width;
		renderPassBeginInfo.renderArea.extent.height = height;
		renderPassBeginInfo.clearValueCount = 2;
		renderPassBeginInfo.pClearValues = clearValues;

		for (int32_t i = 0; i < drawCmdBuffers.size(); i++)
		{
			// Set target frame buffer
			renderPassBeginInfo.framebuffer = frameBuffers[i];

			VK_CHECK_RESULT(vkBeginCommandBuffer(drawCmdBuffers[i], &cmdBufBeginInfo));

			// Acquire barrier
			if (graphics.queueFamilyIndex!=compute.queueFamilyIndex)
			{
				VkBufferMemoryBarrier bufferBarrier =
				{
					VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,nullptr,0,VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT,
					compute.queueFamilyIndex,graphics.queueFamilyIndex,compute.storageBuffer.buffer,0,compute.storageBuffer.size
				};
				vkCmdPipelineBarrier(drawCmdBuffers[i], VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0,
					0, nullptr, 1, &bufferBarrier, 0, nullptr);
			}//if

			// Draw the particle system using the update vertex buffer
			vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

			VkViewport viewport = vks::initializers::GenViewport((float)width, (float)height, 0.0f, 1.0f);
			vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);

			VkRect2D scissor = vks::initializers::GenRect2D(width, height, 0, 0);
			vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);

			vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, graphics.pipeline);
			vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, graphics.pipelineLayout, 0, 1, &graphics.descriptorSet, 0, nullptr);

			VkDeviceSize offsets[1] = { 0 };
			vkCmdBindVertexBuffers(drawCmdBuffers[i], VERTEX_BUFFER_BIND_ID, 1, &compute.storageBuffer.buffer, offsets);
			vkCmdDraw(drawCmdBuffers[i], numParticles, 1, 0, 0);

			drawUI(drawCmdBuffers[i]);

			vkCmdEndRenderPass(drawCmdBuffers[i]);

			// Release barrier
			if (graphics.queueFamilyIndex!=compute.queueFamilyIndex)
			{
				VkBufferMemoryBarrier bufferBarrier =
				{
					VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,nullptr,VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT,0,
					graphics.queueFamilyIndex,compute.queueFamilyIndex,compute.storageBuffer.buffer,0,compute.storageBuffer.size
				};

				vkCmdPipelineBarrier(drawCmdBuffers[i], VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0,
					0, nullptr, 1, &bufferBarrier, 0, nullptr);
			}//if

			VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers[i]));
		}//for
	}

	void prepareForRendering() override
	{
		VulkanExampleBase::prepareForRendering();

		// We will be using the queue family indices to check if graphics and compute queue families differ
		// If that's the case,we need additional barriers for aquiring and releasing resources
		graphics.queueFamilyIndex = vulkanDevice->queueFamilyIndices.graphicIndex;
		compute.queueFamilyIndex = vulkanDevice->queueFamilyIndices.computeIndex;
		loadAssets();
		setupDescriptorPool();
		prepareGraphics();
		prepareCompute();
		buildCommandBuffersForPreRenderPrmitives();
		prepared = true;
	}

	void draw()
	{
		VulkanExampleBase::prepareFrame();

		VkPipelineStageFlags graphicsWaitStageMasks[] = { VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
		VkSemaphore graphicsWaitSemaphores[] = { compute.semaphore,semaphores.presentComplete };
		VkSemaphore graphicsSignalSemaphores[] = { graphics.semaphore, semaphores.renderComplete };

		// Submit graphics commands
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &drawCmdBuffers[currentCmdBufferIndex];
		submitInfo.waitSemaphoreCount = 2;
		submitInfo.pWaitSemaphores = graphicsWaitSemaphores;
		submitInfo.pWaitDstStageMask = graphicsWaitStageMasks;
		submitInfo.signalSemaphoreCount = 2;
		submitInfo.pSignalSemaphores = graphicsSignalSemaphores;
		VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));

		VulkanExampleBase::submitFrame();

		//Wait for rendering finished
		VkPipelineStageFlags waitStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;

		// Submit compute commands
		VkSubmitInfo computeSubmitInfo = vks::initializers::GenSubmitInfo();
		computeSubmitInfo.commandBufferCount = 1;
		computeSubmitInfo.pCommandBuffers = &compute.commandBuffer;
		computeSubmitInfo.waitSemaphoreCount = 1;
		computeSubmitInfo.pWaitSemaphores = &graphics.semaphore;
		computeSubmitInfo.pWaitDstStageMask = &waitStageMask;
		computeSubmitInfo.signalSemaphoreCount = 1;
		computeSubmitInfo.pSignalSemaphores = &compute.semaphore;
		VK_CHECK_RESULT(vkQueueSubmit(compute.queue, 1, &computeSubmitInfo, VK_NULL_HANDLE));
	}

	virtual void render() override
	{
		if (!prepared)
		{
			return;
		}
		draw();

		//更新下一帧逻辑数据
		updateComputeUniformBuffers();
		if (camera.updated)
		{
			updateGraphicsUniformBuffers();
		}
	}

private:

};

VULKAN_EXAMPLE_MAIN()