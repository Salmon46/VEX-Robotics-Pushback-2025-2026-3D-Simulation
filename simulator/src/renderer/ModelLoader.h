#pragma once

#include "renderer/Mesh.h"
#include <string>
#include <vector>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vk_mem_alloc.h>

// Load a GLB file and return a list of Mesh objects (one per primitive)
std::vector<Mesh> LoadModel(VkDevice device, VmaAllocator allocator,
                            VkQueue queue, uint32_t queueFamily,
                            const std::string &path);

// Destroy all meshes in a model
void DestroyModel(VmaAllocator allocator, std::vector<Mesh> &meshes);

// Draw all meshes in a model
void DrawModel(VkCommandBuffer cmd, const std::vector<Mesh> &meshes);
