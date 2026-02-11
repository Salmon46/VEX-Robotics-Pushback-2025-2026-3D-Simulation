#include "renderer/Pipeline.h"
#include <fstream>
#include <iostream>
#include <stdexcept>

VkShaderModule Pipeline::LoadShaderModule(VkDevice device,
                                          const std::string &path) {
  std::ifstream file(path, std::ios::ate | std::ios::binary);
  if (!file.is_open()) {
    throw std::runtime_error("[Pipeline] Cannot open shader: " + path);
  }

  size_t fileSize = static_cast<size_t>(file.tellg());
  std::vector<uint32_t> buffer(fileSize / sizeof(uint32_t));
  file.seekg(0);
  file.read(reinterpret_cast<char *>(buffer.data()), fileSize);
  file.close();

  VkShaderModuleCreateInfo createInfo = {};
  createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  createInfo.codeSize = fileSize;
  createInfo.pCode = buffer.data();

  VkShaderModule module;
  if (vkCreateShaderModule(device, &createInfo, nullptr, &module) !=
      VK_SUCCESS) {
    throw std::runtime_error("[Pipeline] Failed to create shader module: " +
                             path);
  }

  std::cout << "[Pipeline] Loaded shader: " << path << std::endl;
  return module;
}

void Pipeline::Create(VkDevice device, VkRenderPass renderPass,
                      VkFormat depthFormat, const std::string &vertPath,
                      const std::string &fragPath) {
  // --- Shader stages ---
  VkShaderModule vertModule = LoadShaderModule(device, vertPath);
  VkShaderModule fragModule = LoadShaderModule(device, fragPath);

  VkPipelineShaderStageCreateInfo vertStage = {};
  vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
  vertStage.module = vertModule;
  vertStage.pName = "main";

  VkPipelineShaderStageCreateInfo fragStage = {};
  fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  fragStage.module = fragModule;
  fragStage.pName = "main";

  VkPipelineShaderStageCreateInfo stages[] = {vertStage, fragStage};

  // --- Vertex input ---
  auto bindingDesc = Vertex::GetBindingDescription();
  auto attrDescs = Vertex::GetAttributeDescriptions();

  VkPipelineVertexInputStateCreateInfo vertexInput = {};
  vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertexInput.vertexBindingDescriptionCount = 1;
  vertexInput.pVertexBindingDescriptions = &bindingDesc;
  vertexInput.vertexAttributeDescriptionCount =
      static_cast<uint32_t>(attrDescs.size());
  vertexInput.pVertexAttributeDescriptions = attrDescs.data();

  // --- Input assembly ---
  VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
  inputAssembly.sType =
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  inputAssembly.primitiveRestartEnable = VK_FALSE;

  // --- Viewport (dynamic) ---
  VkPipelineViewportStateCreateInfo viewportState = {};
  viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewportState.viewportCount = 1;
  viewportState.scissorCount = 1;

  // --- Rasterizer ---
  VkPipelineRasterizationStateCreateInfo rasterizer = {};
  rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rasterizer.depthClampEnable = VK_FALSE;
  rasterizer.rasterizerDiscardEnable = VK_FALSE;
  rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
  rasterizer.lineWidth = 1.0f;
  rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
  rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rasterizer.depthBiasEnable = VK_FALSE;

  // --- Multisampling ---
  VkPipelineMultisampleStateCreateInfo multisampling = {};
  multisampling.sType =
      VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisampling.sampleShadingEnable = VK_FALSE;
  multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  // --- Depth stencil ---
  VkPipelineDepthStencilStateCreateInfo depthStencil = {};
  depthStencil.sType =
      VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  depthStencil.depthTestEnable = VK_TRUE;
  depthStencil.depthWriteEnable = VK_TRUE;
  depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
  depthStencil.depthBoundsTestEnable = VK_FALSE;
  depthStencil.stencilTestEnable = VK_FALSE;

  // --- Color blending ---
  VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
  colorBlendAttachment.colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  colorBlendAttachment.blendEnable = VK_FALSE;

  VkPipelineColorBlendStateCreateInfo colorBlending = {};
  colorBlending.sType =
      VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  colorBlending.logicOpEnable = VK_FALSE;
  colorBlending.attachmentCount = 1;
  colorBlending.pAttachments = &colorBlendAttachment;

  // --- Dynamic state ---
  VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT,
                                    VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dynamicState = {};
  dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamicState.dynamicStateCount = 2;
  dynamicState.pDynamicStates = dynamicStates;

  // --- Pipeline layout (push constants for MVP) ---
  VkPushConstantRange pushConstant = {};
  pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
  pushConstant.offset = 0;
  pushConstant.size = sizeof(PushConstants);

  VkPipelineLayoutCreateInfo layoutInfo = {};
  layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  layoutInfo.pushConstantRangeCount = 1;
  layoutInfo.pPushConstantRanges = &pushConstant;

  if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &mLayout) !=
      VK_SUCCESS) {
    throw std::runtime_error("[Pipeline] Failed to create pipeline layout");
  }

  // --- Create pipeline ---
  VkGraphicsPipelineCreateInfo pipelineInfo = {};
  pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipelineInfo.stageCount = 2;
  pipelineInfo.pStages = stages;
  pipelineInfo.pVertexInputState = &vertexInput;
  pipelineInfo.pInputAssemblyState = &inputAssembly;
  pipelineInfo.pViewportState = &viewportState;
  pipelineInfo.pRasterizationState = &rasterizer;
  pipelineInfo.pMultisampleState = &multisampling;
  pipelineInfo.pDepthStencilState = &depthStencil;
  pipelineInfo.pColorBlendState = &colorBlending;
  pipelineInfo.pDynamicState = &dynamicState;
  pipelineInfo.layout = mLayout;
  pipelineInfo.renderPass = renderPass;
  pipelineInfo.subpass = 0;

  if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo,
                                nullptr, &mPipeline) != VK_SUCCESS) {
    throw std::runtime_error("[Pipeline] Failed to create graphics pipeline");
  }

  // Cleanup shader modules (no longer needed after pipeline creation)
  vkDestroyShaderModule(device, vertModule, nullptr);
  vkDestroyShaderModule(device, fragModule, nullptr);

  std::cout << "[Pipeline] Graphics pipeline created." << std::endl;
}

void Pipeline::Destroy(VkDevice device) {
  if (mPipeline) {
    vkDestroyPipeline(device, mPipeline, nullptr);
    mPipeline = VK_NULL_HANDLE;
  }
  if (mLayout) {
    vkDestroyPipelineLayout(device, mLayout, nullptr);
    mLayout = VK_NULL_HANDLE;
  }
}

void Pipeline::Bind(VkCommandBuffer cmd) {
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mPipeline);
}
