#include "renderer/Mesh.h"
#include <cstring>
#include <iostream>
#include <stdexcept>

#define VK_CHECK(x)                                                            \
  do {                                                                         \
    VkResult err = x;                                                          \
    if (err) {                                                                 \
      std::cerr << "[Mesh] Vulkan Error: " << err << std::endl;                \
      throw std::runtime_error("Mesh Vulkan error");                           \
    }                                                                          \
  } while (0)

// Helper: create a buffer, upload data via staging, return GPU-only buffer
static void CreateBufferWithStaging(VkDevice device, VmaAllocator allocator,
                                    VkQueue queue, uint32_t queueFamily,
                                    const void *data, VkDeviceSize size,
                                    VkBufferUsageFlags usage,
                                    VkBuffer &outBuffer,
                                    VmaAllocation &outAllocation) {
  // Staging buffer (CPU-visible)
  VkBufferCreateInfo stagingInfo = {};
  stagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  stagingInfo.size = size;
  stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  stagingInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  VmaAllocationCreateInfo stagingAllocInfo = {};
  stagingAllocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

  VkBuffer stagingBuffer;
  VmaAllocation stagingAlloc;
  VK_CHECK(vmaCreateBuffer(allocator, &stagingInfo, &stagingAllocInfo,
                           &stagingBuffer, &stagingAlloc, nullptr));

  // Copy data to staging
  void *mapped;
  vmaMapMemory(allocator, stagingAlloc, &mapped);
  memcpy(mapped, data, size);
  vmaUnmapMemory(allocator, stagingAlloc);

  // GPU buffer
  VkBufferCreateInfo gpuInfo = {};
  gpuInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  gpuInfo.size = size;
  gpuInfo.usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  gpuInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  VmaAllocationCreateInfo gpuAllocInfo = {};
  gpuAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

  VK_CHECK(vmaCreateBuffer(allocator, &gpuInfo, &gpuAllocInfo, &outBuffer,
                           &outAllocation, nullptr));

  // Copy staging → GPU via one-shot command buffer
  VkCommandPoolCreateInfo poolInfo = {};
  poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
  poolInfo.queueFamilyIndex = queueFamily;

  VkCommandPool cmdPool;
  VK_CHECK(vkCreateCommandPool(device, &poolInfo, nullptr, &cmdPool));

  VkCommandBufferAllocateInfo cmdAllocInfo = {};
  cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  cmdAllocInfo.commandPool = cmdPool;
  cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  cmdAllocInfo.commandBufferCount = 1;

  VkCommandBuffer cmd;
  VK_CHECK(vkAllocateCommandBuffers(device, &cmdAllocInfo, &cmd));

  VkCommandBufferBeginInfo beginInfo = {};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(cmd, &beginInfo);

  VkBufferCopy copyRegion = {};
  copyRegion.size = size;
  vkCmdCopyBuffer(cmd, stagingBuffer, outBuffer, 1, &copyRegion);

  vkEndCommandBuffer(cmd);

  VkSubmitInfo submitInfo = {};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &cmd;

  vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
  vkQueueWaitIdle(queue);

  vkDestroyCommandPool(device, cmdPool, nullptr);
  vmaDestroyBuffer(allocator, stagingBuffer, stagingAlloc);
}

Mesh CreateMesh(VkDevice device, VmaAllocator allocator, VkQueue queue,
                uint32_t queueFamily, const std::vector<Vertex> &vertices,
                const std::vector<uint32_t> &indices) {
  Mesh mesh;
  mesh.indexCount = static_cast<uint32_t>(indices.size());

  CreateBufferWithStaging(device, allocator, queue, queueFamily,
                          vertices.data(), vertices.size() * sizeof(Vertex),
                          VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, mesh.vertexBuffer,
                          mesh.vertexAllocation);

  CreateBufferWithStaging(device, allocator, queue, queueFamily, indices.data(),
                          indices.size() * sizeof(uint32_t),
                          VK_BUFFER_USAGE_INDEX_BUFFER_BIT, mesh.indexBuffer,
                          mesh.indexAllocation);

  return mesh;
}

void DestroyMesh(VmaAllocator allocator, Mesh &mesh) {
  if (mesh.vertexBuffer) {
    vmaDestroyBuffer(allocator, mesh.vertexBuffer, mesh.vertexAllocation);
    mesh.vertexBuffer = VK_NULL_HANDLE;
  }
  if (mesh.indexBuffer) {
    vmaDestroyBuffer(allocator, mesh.indexBuffer, mesh.indexAllocation);
    mesh.indexBuffer = VK_NULL_HANDLE;
  }
  mesh.indexCount = 0;
}

void DrawMesh(VkCommandBuffer cmd, const Mesh &mesh) {
  VkDeviceSize offset = 0;
  vkCmdBindVertexBuffers(cmd, 0, 1, &mesh.vertexBuffer, &offset);
  vkCmdBindIndexBuffer(cmd, mesh.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
  vkCmdDrawIndexed(cmd, mesh.indexCount, 1, 0, 0, 0);
}

// --- Colored unit cube with normals ---
Mesh CreateCubeMesh(VkDevice device, VmaAllocator allocator, VkQueue queue,
                    uint32_t queueFamily) {
  const float s = 0.5f;

  // Colors per face
  const float R[] = {0.9f, 0.2f, 0.2f};
  const float G[] = {0.2f, 0.8f, 0.3f};
  const float B[] = {0.2f, 0.4f, 0.9f};
  const float Y[] = {0.9f, 0.9f, 0.2f};
  const float C[] = {0.2f, 0.9f, 0.9f};
  const float M[] = {0.9f, 0.3f, 0.8f};

  // Normals per face
  const float nPX[] = {1, 0, 0};
  const float nNX[] = {-1, 0, 0};
  const float nPY[] = {0, 1, 0};
  const float nNY[] = {0, -1, 0};
  const float nPZ[] = {0, 0, 1};
  const float nNZ[] = {0, 0, -1};

  std::vector<Vertex> verts = {
      // +X face (right)
      {{s, -s, -s}, {nPX[0], nPX[1], nPX[2]}, {R[0], R[1], R[2]}},
      {{s, s, -s}, {nPX[0], nPX[1], nPX[2]}, {R[0], R[1], R[2]}},
      {{s, s, s}, {nPX[0], nPX[1], nPX[2]}, {R[0], R[1], R[2]}},
      {{s, -s, s}, {nPX[0], nPX[1], nPX[2]}, {R[0], R[1], R[2]}},
      // -X face (left)
      {{-s, -s, s}, {nNX[0], nNX[1], nNX[2]}, {G[0], G[1], G[2]}},
      {{-s, s, s}, {nNX[0], nNX[1], nNX[2]}, {G[0], G[1], G[2]}},
      {{-s, s, -s}, {nNX[0], nNX[1], nNX[2]}, {G[0], G[1], G[2]}},
      {{-s, -s, -s}, {nNX[0], nNX[1], nNX[2]}, {G[0], G[1], G[2]}},
      // +Y face (top)
      {{-s, s, -s}, {nPY[0], nPY[1], nPY[2]}, {B[0], B[1], B[2]}},
      {{-s, s, s}, {nPY[0], nPY[1], nPY[2]}, {B[0], B[1], B[2]}},
      {{s, s, s}, {nPY[0], nPY[1], nPY[2]}, {B[0], B[1], B[2]}},
      {{s, s, -s}, {nPY[0], nPY[1], nPY[2]}, {B[0], B[1], B[2]}},
      // -Y face (bottom)
      {{-s, -s, s}, {nNY[0], nNY[1], nNY[2]}, {Y[0], Y[1], Y[2]}},
      {{-s, -s, -s}, {nNY[0], nNY[1], nNY[2]}, {Y[0], Y[1], Y[2]}},
      {{s, -s, -s}, {nNY[0], nNY[1], nNY[2]}, {Y[0], Y[1], Y[2]}},
      {{s, -s, s}, {nNY[0], nNY[1], nNY[2]}, {Y[0], Y[1], Y[2]}},
      // +Z face (front)
      {{-s, -s, s}, {nPZ[0], nPZ[1], nPZ[2]}, {C[0], C[1], C[2]}},
      {{s, -s, s}, {nPZ[0], nPZ[1], nPZ[2]}, {C[0], C[1], C[2]}},
      {{s, s, s}, {nPZ[0], nPZ[1], nPZ[2]}, {C[0], C[1], C[2]}},
      {{-s, s, s}, {nPZ[0], nPZ[1], nPZ[2]}, {C[0], C[1], C[2]}},
      // -Z face (back)
      {{s, -s, -s}, {nNZ[0], nNZ[1], nNZ[2]}, {M[0], M[1], M[2]}},
      {{-s, -s, -s}, {nNZ[0], nNZ[1], nNZ[2]}, {M[0], M[1], M[2]}},
      {{-s, s, -s}, {nNZ[0], nNZ[1], nNZ[2]}, {M[0], M[1], M[2]}},
      {{s, s, -s}, {nNZ[0], nNZ[1], nNZ[2]}, {M[0], M[1], M[2]}},
  };

  std::vector<uint32_t> indices = {
      0,  1,  2,  0,  2,  3,  4,  5,  6,  4,  6,  7,  8,  9,  10, 8,  10, 11,
      12, 13, 14, 12, 14, 15, 16, 17, 18, 16, 18, 19, 20, 21, 22, 20, 22, 23,
  };

  std::cout << "[Mesh] Cube: " << verts.size() << " vertices, "
            << indices.size() << " indices." << std::endl;

  return CreateMesh(device, allocator, queue, queueFamily, verts, indices);
}
