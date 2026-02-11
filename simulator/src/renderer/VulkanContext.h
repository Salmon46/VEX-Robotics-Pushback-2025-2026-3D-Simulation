#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <vk_mem_alloc.h>

class VulkanContext {
public:
  VulkanContext();
  ~VulkanContext();

  void Initialize(GLFWwindow *window,
                  const std::string &appName = "VEX V5 Simulator");
  void Cleanup();

  bool BeginFrame(VkCommandBuffer &outCmd);
  void EndFrame();

  void RecreateSwapchain(int width, int height);

  // Accessors
  VkDevice GetDevice() const { return mDevice; }
  VkPhysicalDevice GetPhysicalDevice() const { return mPhysicalDevice; }
  VkInstance GetInstance() const { return mInstance; }
  VmaAllocator GetAllocator() const { return mAllocator; }
  VkRenderPass GetRenderPass() const { return mRenderPass; }
  VkExtent2D GetSwapchainExtent() const { return mSwapchainExtent; }
  VkFormat GetSwapchainFormat() const { return mSwapchainImageFormat; }
  VkFormat GetDepthFormat() const { return mDepthFormat; }
  VkQueue GetGraphicsQueue() const { return mGraphicsQueue; }
  uint32_t GetGraphicsQueueFamily() const { return mGraphicsQueueFamily; }

private:
  // Core Vulkan
  VkInstance mInstance = VK_NULL_HANDLE;
  VkDebugUtilsMessengerEXT mDebugMessenger = VK_NULL_HANDLE;
  VkSurfaceKHR mSurface = VK_NULL_HANDLE;
  VkPhysicalDevice mPhysicalDevice = VK_NULL_HANDLE;
  VkDevice mDevice = VK_NULL_HANDLE;

  VkQueue mGraphicsQueue = VK_NULL_HANDLE;
  uint32_t mGraphicsQueueFamily = 0;
  VkQueue mPresentQueue = VK_NULL_HANDLE;

  VmaAllocator mAllocator = VK_NULL_HANDLE;

  // Swapchain
  VkSwapchainKHR mSwapchain = VK_NULL_HANDLE;
  VkFormat mSwapchainImageFormat;
  VkExtent2D mSwapchainExtent;
  std::vector<VkImage> mSwapchainImages;
  std::vector<VkImageView> mSwapchainImageViews;
  std::vector<VkFramebuffer> mFramebuffers;

  VkRenderPass mRenderPass = VK_NULL_HANDLE;

  // Depth buffer
  VkFormat mDepthFormat = VK_FORMAT_D32_SFLOAT;
  VkImage mDepthImage = VK_NULL_HANDLE;
  VmaAllocation mDepthAllocation = VK_NULL_HANDLE;
  VkImageView mDepthImageView = VK_NULL_HANDLE;

  // Per-swapchain-image resources
  struct ImageData {
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkSemaphore renderFinishedSemaphore = VK_NULL_HANDLE;
    VkFence inFlightFence = VK_NULL_HANDLE;
  };
  std::vector<ImageData> mImageData;

  // Acquire semaphore ring (size = imageCount + 1)
  std::vector<VkSemaphore> mAcquireSemaphores;
  uint32_t mAcquireSemaphoreIndex = 0;

  uint32_t mCurrentImageIndex = 0;

  GLFWwindow *mWindow = nullptr;

  void CreateSwapchain(int width, int height);
  void CreateDepthBuffer();
  void CreateRenderPass();
  void CreateFramebuffers();
  void CreateSyncResources();
  void CleanupSyncResources();
  void CleanupSwapchain();
};
