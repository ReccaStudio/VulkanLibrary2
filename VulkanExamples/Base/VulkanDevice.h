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
		VkPhysicalDevice physicalDevice;
		VkDevice logicalDevice;
		VkPhysicalDeviceProperties properties;
		VkPhysicalDeviceFeatures features;
		VkPhysicalDeviceFeatures m_enabledDeviceFeatures;
		VkPhysicalDeviceMemoryProperties memoryProperties;
		std::vector<VkQueueFamilyProperties> queueFamilyProperties;
		std::vector<std::string> supportedExtensions;
		VkCommandPool commandPool = VK_NULL_HANDLE;
		bool enableDebugMarkers = false;

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

		VkCommandBuffer CreateCommandBuffer(VkCommandBufferLevel	level, VkCommandPool pool, bool begin = false);

		VkCommandBuffer CreateCommandBuffer(VkCommandBufferLevel level, bool begin = false);

		void FlushCommandBuffer(VkCommandBuffer commandBuffer, VkQueue queue, VkCommandPool pool, bool free = true);

		void FlushCommandBuffer(VkCommandBuffer commandBuffer, VkQueue queue, bool free = true);

		bool IsExtensionSupported(std::string extension);

		VkFormat GetSupportedDepthFormat(bool checkSamplingSupport);

	};//VulkanDevice
}