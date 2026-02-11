#include "renderer/VulkanContext.h"
#include <VkBootstrap.h>
#include <array>

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#define VK_CHECK(x)                                                            \
  do {                                                                         \
    VkResult err = x;                                                          \
    if (err) {                                                                 \
      std::cerr << "[Vulkan] Error: " << err << " at " << __FILE__ << ":"      \
                << __LINE__ << std::endl;                                      \
      throw std::runtime_error("Vulkan error");                                \
    }                                                                          \
  } while (0)

VulkanContext::VulkanContext() {}
VulkanContext::~VulkanContext() { Cleanup(); }

void VulkanContext::Initialize(GLFWwindow *window, const std::string &appName) {
  mWindow = window;

  // --- 1. Instance ---
  vkb::InstanceBuilder instBuilder;
  auto inst_ret = instBuilder.set_app_name(appName.c_str())
                      .request_validation_layers(true)
                      .use_default_debug_messenger()
                      .require_api_version(1, 3, 0)
                      .build();
  if (!inst_ret)
    throw std::runtime_error("[VulkanContext] Instance failed: " +
                             inst_ret.error().message());

  vkb::Instance vkbInst = inst_ret.value();
  mInstance = vkbInst.instance;
  mDebugMessenger = vkbInst.debug_messenger;
  std::cout << "[VulkanContext] Instance created." << std::endl;

  // --- 2. Surface ---
  VK_CHECK(glfwCreateWindowSurface(mInstance, window, nullptr, &mSurface));
  std::cout << "[VulkanContext] Surface created." << std::endl;

  // --- 3. Physical Device ---
  vkb::PhysicalDeviceSelector selector{vkbInst};
  auto phys_ret =
      selector.set_minimum_version(1, 3).set_surface(mSurface).select();
  if (!phys_ret)
    throw std::runtime_error("[VulkanContext] No suitable GPU: " +
                             phys_ret.error().message());

  vkb::PhysicalDevice vkbPhysDev = phys_ret.value();
  mPhysicalDevice = vkbPhysDev.physical_device;

  VkPhysicalDeviceProperties props;
  vkGetPhysicalDeviceProperties(mPhysicalDevice, &props);
  std::cout << "[VulkanContext] GPU: " << props.deviceName << std::endl;

  // --- 4. Logical Device ---
  vkb::DeviceBuilder deviceBuilder{vkbPhysDev};
  auto dev_ret = deviceBuilder.build();
  if (!dev_ret)
    throw std::runtime_error("[VulkanContext] Device failed: " +
                             dev_ret.error().message());

  vkb::Device vkbDevice = dev_ret.value();
  mDevice = vkbDevice.device;

  auto gq = vkbDevice.get_queue(vkb::QueueType::graphics);
  if (!gq)
    throw std::runtime_error("[VulkanContext] No graphics queue");
  mGraphicsQueue = gq.value();
  mGraphicsQueueFamily =
      vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

  auto pq = vkbDevice.get_queue(vkb::QueueType::present);
  mPresentQueue = pq ? pq.value() : mGraphicsQueue;
  std::cout << "[VulkanContext] Device created." << std::endl;

  // --- 5. VMA ---
  VmaAllocatorCreateInfo allocInfo = {};
  allocInfo.physicalDevice = mPhysicalDevice;
  allocInfo.device = mDevice;
  allocInfo.instance = mInstance;
  allocInfo.vulkanApiVersion = VK_API_VERSION_1_3;
  VK_CHECK(vmaCreateAllocator(&allocInfo, &mAllocator));
  std::cout << "[VulkanContext] VMA created." << std::endl;

  // --- 6. Swapchain + resources ---
  int w, h;
  glfwGetFramebufferSize(window, &w, &h);
  CreateSwapchain(w, h);
  CreateDepthBuffer();
  CreateRenderPass();
  CreateFramebuffers();
  CreateSyncResources();

  std::cout << "[VulkanContext] Initialized successfully!" << std::endl;
}

void VulkanContext::Cleanup() {
  if (mDevice == VK_NULL_HANDLE)
    return;

  vkDeviceWaitIdle(mDevice);

  CleanupSyncResources();
  CleanupSwapchain();

  if (mRenderPass) {
    vkDestroyRenderPass(mDevice, mRenderPass, nullptr);
    mRenderPass = VK_NULL_HANDLE;
  }
  if (mAllocator) {
    vmaDestroyAllocator(mAllocator);
    mAllocator = VK_NULL_HANDLE;
  }

  vkDestroyDevice(mDevice, nullptr);
  mDevice = VK_NULL_HANDLE;

  if (mSurface) {
    vkDestroySurfaceKHR(mInstance, mSurface, nullptr);
    mSurface = VK_NULL_HANDLE;
  }
  if (mDebugMessenger) {
    auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
        mInstance, "vkDestroyDebugUtilsMessengerEXT");
    if (func)
      func(mInstance, mDebugMessenger, nullptr);
    mDebugMessenger = VK_NULL_HANDLE;
  }
  if (mInstance) {
    vkDestroyInstance(mInstance, nullptr);
    mInstance = VK_NULL_HANDLE;
  }
}

// --- Swapchain ---
void VulkanContext::CreateSwapchain(int width, int height) {
  vkb::SwapchainBuilder swapchainBuilder{mPhysicalDevice, mDevice, mSurface};

  auto swap_ret = swapchainBuilder
                      .set_desired_format({VK_FORMAT_B8G8R8A8_SRGB,
                                           VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
                      .set_desired_present_mode(VK_PRESENT_MODE_MAILBOX_KHR)
                      .add_fallback_present_mode(VK_PRESENT_MODE_FIFO_KHR)
                      .set_desired_extent(width, height)
                      .build();

  if (!swap_ret)
    throw std::runtime_error("[VulkanContext] Swapchain failed: " +
                             swap_ret.error().message());

  vkb::Swapchain vkbSwapchain = swap_ret.value();
  mSwapchain = vkbSwapchain.swapchain;
  mSwapchainImageFormat = vkbSwapchain.image_format;
  mSwapchainExtent = vkbSwapchain.extent;
  mSwapchainImages = vkbSwapchain.get_images().value();
  mSwapchainImageViews = vkbSwapchain.get_image_views().value();

  const char *modeStr = "UNKNOWN";
  switch (vkbSwapchain.present_mode) {
  case VK_PRESENT_MODE_MAILBOX_KHR:
    modeStr = "MAILBOX";
    break;
  case VK_PRESENT_MODE_FIFO_KHR:
    modeStr = "FIFO";
    break;
  case VK_PRESENT_MODE_IMMEDIATE_KHR:
    modeStr = "IMMEDIATE";
    break;
  default:
    break;
  }
  std::cout << "[VulkanContext] Swapchain: " << mSwapchainExtent.width << "x"
            << mSwapchainExtent.height << ", " << mSwapchainImages.size()
            << " images"
            << ", " << modeStr << std::endl;
}

// --- Depth Buffer ---
void VulkanContext::CreateDepthBuffer() {
  VkImageCreateInfo imageInfo = {};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.format = mDepthFormat;
  imageInfo.extent = {mSwapchainExtent.width, mSwapchainExtent.height, 1};
  imageInfo.mipLevels = 1;
  imageInfo.arrayLayers = 1;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

  VmaAllocationCreateInfo vmaAllocInfo = {};
  vmaAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
  vmaAllocInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

  VK_CHECK(vmaCreateImage(mAllocator, &imageInfo, &vmaAllocInfo, &mDepthImage,
                          &mDepthAllocation, nullptr));

  VkImageViewCreateInfo viewInfo = {};
  viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewInfo.image = mDepthImage;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = mDepthFormat;
  viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = 1;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = 1;

  VK_CHECK(vkCreateImageView(mDevice, &viewInfo, nullptr, &mDepthImageView));
  std::cout << "[VulkanContext] Depth buffer created." << std::endl;
}

// --- Render Pass ---
void VulkanContext::CreateRenderPass() {
  // Color attachment
  VkAttachmentDescription colorAttachment = {};
  colorAttachment.format = mSwapchainImageFormat;
  colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
  colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

  // Depth attachment
  VkAttachmentDescription depthAttachment = {};
  depthAttachment.format = mDepthFormat;
  depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
  depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  depthAttachment.finalLayout =
      VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkAttachmentReference colorRef = {};
  colorRef.attachment = 0;
  colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkAttachmentReference depthRef = {};
  depthRef.attachment = 1;
  depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkSubpassDescription subpass = {};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &colorRef;
  subpass.pDepthStencilAttachment = &depthRef;

  // Dependencies for both color and depth
  VkSubpassDependency dependency = {};
  dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
  dependency.dstSubpass = 0;
  dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
  dependency.srcAccessMask = 0;
  dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
  dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                             VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

  std::array<VkAttachmentDescription, 2> attachments = {colorAttachment,
                                                        depthAttachment};

  VkRenderPassCreateInfo renderPassInfo = {};
  renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
  renderPassInfo.pAttachments = attachments.data();
  renderPassInfo.subpassCount = 1;
  renderPassInfo.pSubpasses = &subpass;
  renderPassInfo.dependencyCount = 1;
  renderPassInfo.pDependencies = &dependency;

  VK_CHECK(vkCreateRenderPass(mDevice, &renderPassInfo, nullptr, &mRenderPass));
}

// --- Framebuffers ---
void VulkanContext::CreateFramebuffers() {
  mFramebuffers.resize(mSwapchainImageViews.size());
  for (size_t i = 0; i < mSwapchainImageViews.size(); i++) {
    std::array<VkImageView, 2> attachments = {mSwapchainImageViews[i],
                                              mDepthImageView};

    VkFramebufferCreateInfo fbInfo = {};
    fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbInfo.renderPass = mRenderPass;
    fbInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    fbInfo.pAttachments = attachments.data();
    fbInfo.width = mSwapchainExtent.width;
    fbInfo.height = mSwapchainExtent.height;
    fbInfo.layers = 1;
    VK_CHECK(vkCreateFramebuffer(mDevice, &fbInfo, nullptr, &mFramebuffers[i]));
  }
}

// --- Sync resources ---
void VulkanContext::CreateSyncResources() {
  uint32_t imageCount = static_cast<uint32_t>(mSwapchainImages.size());
  mImageData.resize(imageCount);

  VkSemaphoreCreateInfo semInfo = {};
  semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

  VkFenceCreateInfo fenceInfo = {};
  fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

  for (uint32_t i = 0; i < imageCount; i++) {
    VkCommandPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = mGraphicsQueueFamily;
    VK_CHECK(vkCreateCommandPool(mDevice, &poolInfo, nullptr,
                                 &mImageData[i].commandPool));

    VkCommandBufferAllocateInfo cmdAllocInfo = {};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.commandPool = mImageData[i].commandPool;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandBufferCount = 1;
    VK_CHECK(vkAllocateCommandBuffers(mDevice, &cmdAllocInfo,
                                      &mImageData[i].commandBuffer));

    VK_CHECK(vkCreateSemaphore(mDevice, &semInfo, nullptr,
                               &mImageData[i].renderFinishedSemaphore));
    VK_CHECK(vkCreateFence(mDevice, &fenceInfo, nullptr,
                           &mImageData[i].inFlightFence));
  }

  uint32_t acquireCount = imageCount + 1;
  mAcquireSemaphores.resize(acquireCount);
  for (uint32_t i = 0; i < acquireCount; i++) {
    VK_CHECK(
        vkCreateSemaphore(mDevice, &semInfo, nullptr, &mAcquireSemaphores[i]));
  }
  mAcquireSemaphoreIndex = 0;

  std::cout << "[VulkanContext] Sync: " << imageCount << " image slots, "
            << acquireCount << " acquire semaphores." << std::endl;
}

void VulkanContext::CleanupSyncResources() {
  for (auto &img : mImageData) {
    if (img.inFlightFence)
      vkDestroyFence(mDevice, img.inFlightFence, nullptr);
    if (img.renderFinishedSemaphore)
      vkDestroySemaphore(mDevice, img.renderFinishedSemaphore, nullptr);
    if (img.commandPool)
      vkDestroyCommandPool(mDevice, img.commandPool, nullptr);
  }
  mImageData.clear();

  for (auto sem : mAcquireSemaphores) {
    if (sem)
      vkDestroySemaphore(mDevice, sem, nullptr);
  }
  mAcquireSemaphores.clear();
}

// --- Frame rendering ---
bool VulkanContext::BeginFrame(VkCommandBuffer &outCmd) {
  VkSemaphore acquireSem = mAcquireSemaphores[mAcquireSemaphoreIndex];

  VkResult result =
      vkAcquireNextImageKHR(mDevice, mSwapchain, UINT64_MAX, acquireSem,
                            VK_NULL_HANDLE, &mCurrentImageIndex);

  if (result == VK_ERROR_OUT_OF_DATE_KHR) {
    int w, h;
    glfwGetFramebufferSize(mWindow, &w, &h);
    RecreateSwapchain(w, h);
    return false;
  }

  mAcquireSemaphoreIndex = (mAcquireSemaphoreIndex + 1) %
                           static_cast<uint32_t>(mAcquireSemaphores.size());

  ImageData &img = mImageData[mCurrentImageIndex];
  VK_CHECK(
      vkWaitForFences(mDevice, 1, &img.inFlightFence, VK_TRUE, UINT64_MAX));
  VK_CHECK(vkResetFences(mDevice, 1, &img.inFlightFence));

  VkCommandBuffer cmd = img.commandBuffer;
  VK_CHECK(vkResetCommandBuffer(cmd, 0));

  VkCommandBufferBeginInfo beginInfo = {};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo));

  // Clear both color and depth
  std::array<VkClearValue, 2> clearValues = {};
  clearValues[0].color = {{0.1f, 0.1f, 0.12f, 1.0f}}; // Dark grey background
  clearValues[1].depthStencil = {1.0f, 0};

  VkRenderPassBeginInfo rpBegin = {};
  rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  rpBegin.renderPass = mRenderPass;
  rpBegin.framebuffer = mFramebuffers[mCurrentImageIndex];
  rpBegin.renderArea.offset = {0, 0};
  rpBegin.renderArea.extent = mSwapchainExtent;
  rpBegin.clearValueCount = static_cast<uint32_t>(clearValues.size());
  rpBegin.pClearValues = clearValues.data();

  vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

  VkViewport viewport = {};
  viewport.width = static_cast<float>(mSwapchainExtent.width);
  viewport.height = static_cast<float>(mSwapchainExtent.height);
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;
  vkCmdSetViewport(cmd, 0, 1, &viewport);

  VkRect2D scissor = {};
  scissor.extent = mSwapchainExtent;
  vkCmdSetScissor(cmd, 0, 1, &scissor);

  outCmd = cmd;
  return true;
}

void VulkanContext::EndFrame() {
  ImageData &img = mImageData[mCurrentImageIndex];
  VkCommandBuffer cmd = img.commandBuffer;

  vkCmdEndRenderPass(cmd);
  VK_CHECK(vkEndCommandBuffer(cmd));

  uint32_t usedAcquireIdx =
      (mAcquireSemaphoreIndex +
       static_cast<uint32_t>(mAcquireSemaphores.size()) - 1) %
      static_cast<uint32_t>(mAcquireSemaphores.size());
  VkSemaphore acquireSem = mAcquireSemaphores[usedAcquireIdx];

  VkPipelineStageFlags waitStage =
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

  VkSubmitInfo submitInfo = {};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.waitSemaphoreCount = 1;
  submitInfo.pWaitSemaphores = &acquireSem;
  submitInfo.pWaitDstStageMask = &waitStage;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &cmd;
  submitInfo.signalSemaphoreCount = 1;
  submitInfo.pSignalSemaphores = &img.renderFinishedSemaphore;

  VK_CHECK(vkQueueSubmit(mGraphicsQueue, 1, &submitInfo, img.inFlightFence));

  VkPresentInfoKHR presentInfo = {};
  presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  presentInfo.waitSemaphoreCount = 1;
  presentInfo.pWaitSemaphores = &img.renderFinishedSemaphore;
  presentInfo.swapchainCount = 1;
  presentInfo.pSwapchains = &mSwapchain;
  presentInfo.pImageIndices = &mCurrentImageIndex;

  VkResult result = vkQueuePresentKHR(mPresentQueue, &presentInfo);
  if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
    int w, h;
    glfwGetFramebufferSize(mWindow, &w, &h);
    RecreateSwapchain(w, h);
  }
}

// --- Swapchain recreation ---
void VulkanContext::CleanupSwapchain() {
  // Depth buffer
  if (mDepthImageView) {
    vkDestroyImageView(mDevice, mDepthImageView, nullptr);
    mDepthImageView = VK_NULL_HANDLE;
  }
  if (mDepthImage) {
    vmaDestroyImage(mAllocator, mDepthImage, mDepthAllocation);
    mDepthImage = VK_NULL_HANDLE;
    mDepthAllocation = VK_NULL_HANDLE;
  }

  for (auto fb : mFramebuffers) {
    if (fb)
      vkDestroyFramebuffer(mDevice, fb, nullptr);
  }
  mFramebuffers.clear();

  for (auto iv : mSwapchainImageViews) {
    if (iv)
      vkDestroyImageView(mDevice, iv, nullptr);
  }
  mSwapchainImageViews.clear();

  if (mSwapchain) {
    vkDestroySwapchainKHR(mDevice, mSwapchain, nullptr);
    mSwapchain = VK_NULL_HANDLE;
  }
}

void VulkanContext::RecreateSwapchain(int width, int height) {
  vkDeviceWaitIdle(mDevice);

  // Destroy render pass (it references depth format, needs recreating)
  if (mRenderPass) {
    vkDestroyRenderPass(mDevice, mRenderPass, nullptr);
    mRenderPass = VK_NULL_HANDLE;
  }

  CleanupSyncResources();
  CleanupSwapchain();
  CreateSwapchain(width, height);
  CreateDepthBuffer();
  CreateRenderPass();
  CreateFramebuffers();
  CreateSyncResources();
  std::cout << "[VulkanContext] Swapchain recreated: " << width << "x" << height
            << std::endl;
}
