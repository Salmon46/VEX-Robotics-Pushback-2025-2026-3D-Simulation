#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <string>
#include <vector>

struct Vertex {
  float position[3];
  float normal[3];
  float color[3];

  static VkVertexInputBindingDescription GetBindingDescription() {
    VkVertexInputBindingDescription binding = {};
    binding.binding = 0;
    binding.stride = sizeof(Vertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    return binding;
  }

  static std::vector<VkVertexInputAttributeDescription>
  GetAttributeDescriptions() {
    std::vector<VkVertexInputAttributeDescription> attrs(3);

    // Position (location = 0)
    attrs[0].binding = 0;
    attrs[0].location = 0;
    attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[0].offset = offsetof(Vertex, position);

    // Normal (location = 1)
    attrs[1].binding = 0;
    attrs[1].location = 1;
    attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[1].offset = offsetof(Vertex, normal);

    // Color (location = 2)
    attrs[2].binding = 0;
    attrs[2].location = 2;
    attrs[2].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[2].offset = offsetof(Vertex, color);

    return attrs;
  }
};

// Push constants: MVP + model matrix (for normals)
struct PushConstants {
  float mvp[16];   // mat4 — model-view-projection
  float model[16]; // mat4 — model matrix (for transforming normals)
};

class Pipeline {
public:
  void Create(VkDevice device, VkRenderPass renderPass, VkFormat depthFormat,
              const std::string &vertPath, const std::string &fragPath);
  void Destroy(VkDevice device);

  void Bind(VkCommandBuffer cmd);
  VkPipelineLayout GetLayout() const { return mLayout; }

private:
  VkPipeline mPipeline = VK_NULL_HANDLE;
  VkPipelineLayout mLayout = VK_NULL_HANDLE;

  VkShaderModule LoadShaderModule(VkDevice device, const std::string &path);
};
