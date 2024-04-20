/*
 * Vulkan device class
 *
 * Encapsulates a physical Vulkan device and its logical representation
 *
 * Copyright (C) 2016-2023 by Sascha Willems - www.saschawillems.de
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 */

#pragma once

#include "VulkanBuffer.h"
#include "VulkanTools.h"
#include "vulkan/vulkan.h"
#include <algorithm>
#include <assert.h>
#include <exception>

namespace vks
{
	struct VulkanDevice
	{
		/** @brief Physical device representation */
		VkPhysicalDevice physicalDevice;
		/** @brief Logical device representation (application's view of the device) */
		VkDevice logicalDevice;
		/** @brief Properties of the physical device including limits that the application can check against */
		VkPhysicalDeviceProperties properties;
		/** @brief Features of the physical device that an application can use to check if a feature is supported */
		VkPhysicalDeviceFeatures features;
		VkPhysicalDeviceFeatures m_enabledDeviceFeatures;
		VkPhysicalDeviceMemoryProperties memoryProperties;
		/** @brief Queue family properties of the physical device */
		std::vector<VkQueueFamilyProperties> queueFamilyProperties;
		/** @brief List of extensions supported by the device */
		std::vector<std::string> supportedExtensions;
		/** @brief Default command pool for the graphics queue family index */
		VkCommandPool commandPool = VK_NULL_HANDLE;
		/** @brief Contains queue family indices */
		struct
		{
			uint32_t graphicIndex;
			uint32_t computeIndex;
			uint32_t transferIndex;
		}	queueFamilyIndices;

		operator VkDevice() const
		{
			return logicalDevice;
		}

		explicit VulkanDevice(VkPhysicalDevice physicalDevice);

		~VulkanDevice();

		uint32_t GetMemoryType(uint32_t typeBits, VkMemoryPropertyFlags propertyFlags, VkBool32 *memTypeFound = nullptr)const;

		uint32_t GetQueueFamilyIndex(VkQueueFlagBits queueFlags)const;

		VkResult CreateLogicalDevice(VkPhysicalDeviceFeatures enabledDeviceFeatures, std::vector<const char*>enabledExtensions,
			void *pNextChain, bool useSwapChain = true, VkQueueFlags requestedQueueTypes = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT);

		VkResult CreateBuffer(VkBufferUsageFlags usageFlags, VkMemoryPropertyFlags memoryPropertyFlags, VkDeviceSize size, VkBuffer *buffer, VkDeviceMemory *memory, void * data = nullptr);

		VkResult CreateBuffer(VkBufferUsageFlags usageFlags, VkMemoryPropertyFlags memoryPropertyFlags, vks::Buffer* buffer, VkDeviceSize size, void* data = nullptr);

		void CopyBuffer(vks::Buffer * src, vks::Buffer * dst, VkQueue queue, VkBufferCopy * copyRegion = nullptr);

		VkCommandPool CreateCommandPool(uint32_t queueFamilyIndex, VkCommandPoolCreateFlags createFlags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

		VkCommandBuffer CreateCommandBuffer(VkCommandBufferLevel	level, VkCommandPool curCommandPool, bool begin = false);

		VkCommandBuffer CreateCommandBuffer(VkCommandBufferLevel level, bool begin = false);

		void FlushCommandBuffer(VkCommandBuffer commandBuffer, VkQueue queue, VkCommandPool pool, bool free = true);

		void FlushCommandBuffer(VkCommandBuffer commandBuffer, VkQueue queue, bool free = true);

		bool IsExtensionSupported(std::string extension);

		VkFormat GetSupportedDepthFormat(bool checkSamplingSupport);

	};//VulkanDevice
}