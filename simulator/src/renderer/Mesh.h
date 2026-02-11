#pragma once

#define GLFW_INCLUDE_VULKAN
#include "renderer/Pipeline.h" // For Vertex struct
#include <GLFW/glfw3.h>
#include <vector>
#include <vk_mem_alloc.h>


struct Mesh {
  VkBuffer vertexBuffer = VK_NULL_HANDLE;
  VmaAllocation vertexAllocation = VK_NULL_HANDLE;
  VkBuffer indexBuffer = VK_NULL_HANDLE;
  VmaAllocation indexAllocation = VK_NULL_HANDLE;
  uint32_t indexCount = 0;
};

// Upload mesh data to GPU via staging buffer
Mesh CreateMesh(VkDevice device, VmaAllocator allocator, VkQueue queue,
                uint32_t queueFamily, const std::vector<Vertex> &vertices,
                const std::vector<uint32_t> &indices);

void DestroyMesh(VmaAllocator allocator, Mesh &mesh);

// Bind and draw a mesh
void DrawMesh(VkCommandBuffer cmd, const Mesh &mesh);

// Create a colored unit cube centered at origin
Mesh CreateCubeMesh(VkDevice device, VmaAllocator allocator, VkQueue queue,
                    uint32_t queueFamily);
