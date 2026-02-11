// Block Spawning & Intake — Robot drives, spawns blocks, picks up and ejects
#include "AssetLoader.h"
#include "GameBlock.h"
#include "PhysicsWorld.h"
#include "Robot.h"
#include "SimulationFilter.h"
#include "renderer/Camera.h"
#include "renderer/Mesh.h"
#include "renderer/ModelLoader.h"
#include "renderer/Pipeline.h"
#include "renderer/VulkanContext.h"

#include <GLFW/glfw3.h>
#include <cstring>
#include <functional>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <list>
#include <tiny_gltf.h>

// Dear ImGui
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

// --- Globals for callbacks ---
static bool framebufferResized = false;

void framebufferResizeCallback(GLFWwindow *window, int width, int height) {
  framebufferResized = true;
}

// --- Helper: spawn a block ---
GameBlock SpawnBlock(PxPhysics *physics, PxScene *scene, PxMaterial *material,
                     BlockColor color, PxVec3 position) {
  GameBlock block;
  block.color = color;
  block.held = false;

  // Block is a sphere ~14cm diameter (Big enought to actually collide)
  float radius = 0.07f;
  block.body = physics->createRigidDynamic(PxTransform(position));
  PxShape *shape = physics->createShape(PxSphereGeometry(radius), *material);
  block.body->attachShape(*shape);
  shape->release();
  PxRigidBodyExt::updateMassAndInertia(*block.body, 1.0f);

  // Friction: blocks slow down on the field
  block.body->setLinearDamping(2.0f);
  block.body->setAngularDamping(1.0f);

  // Blocks collide with ground, chassis, wheels, obstacles, other blocks
  SetActorFilter(block.body, FilterGroup::eBLOCK,
                 FilterGroup::eGROUND | FilterGroup::eCHASSIS |
                     FilterGroup::eWHEEL | FilterGroup::eOBSTACLE |
                     FilterGroup::eBLOCK);

  scene->addActor(*block.body);

  std::cout << "[Block] Spawned " << (color == BlockColor::RED ? "RED" : "BLUE")
            << " block at (" << position.x << ", " << position.y << ", "
            << position.z << ")" << std::endl;
  return block;
}

// --- Helper: draw with transform ---
template <typename F>
static void DrawWithPushConstants(VkCommandBuffer cmd, VkPipelineLayout layout,
                                  const glm::mat4 &vp,
                                  const glm::mat4 &modelMat, F drawFn) {
  glm::mat4 mvp = vp * modelMat;
  PushConstants pc;
  memcpy(pc.mvp, glm::value_ptr(mvp), sizeof(pc.mvp));
  memcpy(pc.model, glm::value_ptr(modelMat), sizeof(pc.model));
  vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc),
                     &pc);
  drawFn(cmd);
}

int main() {
  // --- GLFW Init ---
  if (!glfwInit()) {
    std::cerr << "Failed to initialize GLFW!" << std::endl;
    return -1;
  }

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

  const int screenWidth = 1280;
  const int screenHeight = 720;

  GLFWwindow *window =
      glfwCreateWindow(screenWidth, screenHeight,
                       "VEX V5 Robot Simulator (Vulkan)", nullptr, nullptr);
  if (!window) {
    std::cerr << "Failed to create GLFW window!" << std::endl;
    glfwTerminate();
    return -1;
  }

  glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);

  // --- Vulkan Init ---
  VulkanContext vulkan;
  try {
    vulkan.Initialize(window);
  } catch (const std::exception &e) {
    std::cerr << "Vulkan init failed: " << e.what() << std::endl;
    glfwDestroyWindow(window);
    glfwTerminate();
    return -1;
  }

  // --- Pipeline ---
  Pipeline pipeline;
  try {
    pipeline.Create(vulkan.GetDevice(), vulkan.GetRenderPass(),
                    vulkan.GetDepthFormat(), "shaders/basic.vert.spv",
                    "shaders/basic.frag.spv");
  } catch (const std::exception &e) {
    std::cerr << "Pipeline failed: " << e.what() << std::endl;
    vulkan.Cleanup();
    glfwDestroyWindow(window);
    glfwTerminate();
    return -1;
  }

  // --- ImGui Descriptor Pool ---
  VkDescriptorPool imguiPool = VK_NULL_HANDLE;
  {
    VkDescriptorPoolSize poolSizes[] = {
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1},
    };
    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = poolSizes;
    vkCreateDescriptorPool(vulkan.GetDevice(), &poolInfo, nullptr, &imguiPool);
  }

  // --- ImGui Init ---
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  ImGui::StyleColorsDark();

  // Make the style more compact and modern
  ImGuiStyle &style = ImGui::GetStyle();
  style.WindowRounding = 8.0f;
  style.FrameRounding = 4.0f;
  style.GrabRounding = 4.0f;
  style.Alpha = 0.92f;

  ImGui_ImplGlfw_InitForVulkan(window, true);

  ImGui_ImplVulkan_InitInfo initInfo = {};
  initInfo.Instance = vulkan.GetInstance();
  initInfo.PhysicalDevice = vulkan.GetPhysicalDevice();
  initInfo.Device = vulkan.GetDevice();
  initInfo.QueueFamily = vulkan.GetGraphicsQueueFamily();
  initInfo.Queue = vulkan.GetGraphicsQueue();
  initInfo.DescriptorPool = imguiPool;
  initInfo.MinImageCount = 2;
  initInfo.ImageCount = 2;
  initInfo.RenderPass = vulkan.GetRenderPass();
  ImGui_ImplVulkan_Init(&initInfo);

  // Upload ImGui font textures
  ImGui_ImplVulkan_CreateFontsTexture();

  bool showInfoPanel = true;
  bool hWasPressed = false;

  // --- Camera ---
  Camera camera;
  camera.Init(3.0f, -90.0f, 30.0f);

  // --- Load GLB models for rendering ---
  std::vector<Mesh> robotMeshes, fieldMeshes, redBlockMeshes, blueBlockMeshes;
  try {
    robotMeshes = LoadModel(
        vulkan.GetDevice(), vulkan.GetAllocator(), vulkan.GetGraphicsQueue(),
        vulkan.GetGraphicsQueueFamily(), "assets/example_robot.glb");
  } catch (const std::exception &e) {
    std::cerr << "Robot model load failed: " << e.what() << std::endl;
  }

  try {
    fieldMeshes = LoadModel(
        vulkan.GetDevice(), vulkan.GetAllocator(), vulkan.GetGraphicsQueue(),
        vulkan.GetGraphicsQueueFamily(), "assets/field.glb");
  } catch (const std::exception &e) {
    std::cerr << "Field model load failed: " << e.what() << std::endl;
  }

  try {
    redBlockMeshes = LoadModel(
        vulkan.GetDevice(), vulkan.GetAllocator(), vulkan.GetGraphicsQueue(),
        vulkan.GetGraphicsQueueFamily(), "assets/red_block.glb");
  } catch (const std::exception &e) {
    std::cerr << "Red block model load failed: " << e.what() << std::endl;
  }

  try {
    blueBlockMeshes = LoadModel(
        vulkan.GetDevice(), vulkan.GetAllocator(), vulkan.GetGraphicsQueue(),
        vulkan.GetGraphicsQueueFamily(), "assets/blue_block.glb");
  } catch (const std::exception &e) {
    std::cerr << "Blue block model load failed: " << e.what() << std::endl;
  }

  // --- Load GLB models for physics collision ---
  tinygltf::Model fieldGltfModel;
  {
    tinygltf::TinyGLTF loader;
    std::string err, warn;
    if (!loader.LoadBinaryFromFile(&fieldGltfModel, &err, &warn,
                                   "assets/field.glb")) {
      std::cerr << "Failed to load field GLB for physics: " << err << std::endl;
    }
  }

  // --- PhysX Init ---
  PhysicsWorld physics;
  physics.Initialize();

  // Create field collision body (static)
  PxRigidStatic *fieldBody = nullptr;
  if (!fieldGltfModel.meshes.empty()) {
    fieldBody = AssetLoader::CreateStaticBody(
        physics.GetPhysics(), physics.GetScene(), fieldGltfModel,
        physics.GetDefaultMaterial(), PxTransform(PxIdentity), PxVec3(1.0f));

    if (fieldBody) {
      SetActorFilter(fieldBody, FilterGroup::eGROUND,
                     FilterGroup::eCHASSIS | FilterGroup::eWHEEL |
                         FilterGroup::eOBSTACLE | FilterGroup::eBLOCK);
    }
  }

  // Create robot
  Robot robot;
  robot.Initialize(physics.GetPhysics(), physics.GetScene(),
                   physics.GetDefaultMaterial(), PxVec3(0.0f, 0.5f, 0.0f));

  // --- Block storage (std::list for stable pointers) ---
  std::list<GameBlock> blocks;

  std::cout << "=== VEX Robot Simulator ===" << std::endl;
  std::cout << "A/Z: right fwd/rev | D/C: left fwd/rev | R/B: spawn blocks"
            << std::endl;
  std::cout << "F: intake | G: outtake | ESC: exit" << std::endl;
  std::cout << "Arrow keys: pan camera | Right-click: orbit | +/-: zoom"
            << std::endl;

  double lastTime = glfwGetTime();
  const float physicsTimestep = 1.0f / 60.0f;
  float physicsAccumulator = 0.0f;

  // Key debounce state
  bool rWasPressed = false, bWasPressed = false;
  bool fWasPressed = false, gWasPressed = false;
  int spawnCounter = 0;

  // --- Main Loop ---
  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();

    // Delta time
    double currentTime = glfwGetTime();
    float dt = static_cast<float>(currentTime - lastTime);
    lastTime = currentTime;
    if (dt > 0.1f)
      dt = 0.1f;

    // ESC to close
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
      glfwSetWindowShouldClose(window, GLFW_TRUE);
    }

    // Handle resize
    if (framebufferResized) {
      framebufferResized = false;
      int w, h;
      glfwGetFramebufferSize(window, &w, &h);
      if (w > 0 && h > 0) {
        vulkan.RecreateSwapchain(w, h);
        pipeline.Destroy(vulkan.GetDevice());
        pipeline.Create(vulkan.GetDevice(), vulkan.GetRenderPass(),
                        vulkan.GetDepthFormat(), "shaders/basic.vert.spv",
                        "shaders/basic.frag.spv");
      }
      continue;
    }

    // --- Robot Input ---
    // A = right forward, D = left forward, Z = right backward, C = left
    // backward
    float leftInput = 0.0f, rightInput = 0.0f;
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
      rightInput += 1.0f; // Right wheels forward
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
      leftInput += 1.0f; // Left wheels forward
    if (glfwGetKey(window, GLFW_KEY_Z) == GLFW_PRESS)
      rightInput -= 1.0f; // Right wheels backward
    if (glfwGetKey(window, GLFW_KEY_C) == GLFW_PRESS)
      leftInput -= 1.0f; // Left wheels backward
    robot.SetDriveInput(leftInput, rightInput);

    // --- Spawn Blocks (R = red, B = blue) ---
    {
      bool rPressed = glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS;
      if (rPressed && !rWasPressed) {
        // Spawn above the robot's position
        PxVec3 spawnPos = robot.GetFrontPosition();
        spawnPos.y += 0.3f;
        blocks.push_back(SpawnBlock(physics.GetPhysics(), physics.GetScene(),
                                    physics.GetDefaultMaterial(),
                                    BlockColor::RED, spawnPos));
      }
      rWasPressed = rPressed;

      bool bPressed = glfwGetKey(window, GLFW_KEY_B) == GLFW_PRESS;
      if (bPressed && !bWasPressed) {
        PxVec3 spawnPos = robot.GetFrontPosition();
        spawnPos.y += 0.3f;
        blocks.push_back(SpawnBlock(physics.GetPhysics(), physics.GetScene(),
                                    physics.GetDefaultMaterial(),
                                    BlockColor::BLUE, spawnPos));
      }
      bWasPressed = bPressed;
    }

    // --- Intake (F) ---
    {
      bool fPressed = glfwGetKey(window, GLFW_KEY_F) == GLFW_PRESS;
      if (fPressed && !fWasPressed && !robot.IsIntakeFull()) {
        // Try to intake the nearest block
        float bestDist = 999.0f;
        GameBlock *bestBlock = nullptr;
        PxVec3 frontPos = robot.GetFrontPosition();

        for (auto &block : blocks) {
          if (block.held || !block.body)
            continue;
          float dist = (frontPos - block.body->getGlobalPose().p).magnitude();
          if (dist < bestDist) {
            bestDist = dist;
            bestBlock = &block;
          }
        }

        if (bestBlock) {
          robot.TryIntake(*bestBlock, physics.GetPhysics());
        }
      }
      fWasPressed = fPressed;
    }

    // --- Outtake (G) ---
    {
      bool gPressed = glfwGetKey(window, GLFW_KEY_G) == GLFW_PRESS;
      if (gPressed && !gWasPressed) {
        robot.Outtake();
      }
      gWasPressed = gPressed;
    }

    // --- Physics Update (fixed timestep) ---
    physicsAccumulator += dt;
    while (physicsAccumulator >= physicsTimestep) {
      robot.Update(physicsTimestep);
      physics.Update(physicsTimestep);
      physicsAccumulator -= physicsTimestep;
    }

    // --- Camera Input ---
    camera.ProcessInput(window, dt);

    // --- Render Frame ---
    VkCommandBuffer cmd;
    if (vulkan.BeginFrame(cmd)) {
      pipeline.Bind(cmd);

      VkExtent2D extent = vulkan.GetSwapchainExtent();
      float aspect =
          static_cast<float>(extent.width) / static_cast<float>(extent.height);
      glm::mat4 vp = camera.GetViewProjection(aspect);
      VkPipelineLayout layout = pipeline.GetLayout();

      // Draw field
      if (!fieldMeshes.empty()) {
        DrawWithPushConstants(
            cmd, layout, vp, glm::mat4(1.0f),
            [&](VkCommandBuffer c) { DrawModel(c, fieldMeshes); });
      }

      // Draw robot
      if (!robotMeshes.empty()) {
        glm::mat4 robotModel = robot.GetTransformMatrix(0.01f);
        DrawWithPushConstants(
            cmd, layout, vp, robotModel,
            [&](VkCommandBuffer c) { DrawModel(c, robotMeshes); });
      }

      // Draw blocks
      for (const auto &block : blocks) {
        if (!block.body)
          continue;

        PxTransform pose = block.body->getGlobalPose();
        glm::quat q(pose.q.w, pose.q.x, pose.q.y, pose.q.z);
        glm::mat4 blockModel =
            glm::translate(glm::mat4(1.0f),
                           glm::vec3(pose.p.x, pose.p.y, pose.p.z)) *
            glm::mat4_cast(q);

        const auto &meshes =
            (block.color == BlockColor::RED) ? redBlockMeshes : blueBlockMeshes;

        if (!meshes.empty()) {
          DrawWithPushConstants(
              cmd, layout, vp, blockModel,
              [&](VkCommandBuffer c) { DrawModel(c, meshes); });
        }
      }

      // --- ImGui Rendering ---
      // Wait for GPU to finish previous frames before ImGui potentially
      // resizes its vertex/index buffers (prevents vkDestroyBuffer errors)
      vkDeviceWaitIdle(vulkan.GetDevice());
      ImGui_ImplVulkan_NewFrame();
      ImGui_ImplGlfw_NewFrame();
      ImGui::NewFrame();

      // H key toggles info panel
      bool hPressed = glfwGetKey(window, GLFW_KEY_H) == GLFW_PRESS;
      if (hPressed && !hWasPressed)
        showInfoPanel = !showInfoPanel;
      hWasPressed = hPressed;

      if (showInfoPanel) {
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 10, 10),
                                ImGuiCond_Always, ImVec2(1.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(320, 0), ImGuiCond_Always);
        ImGui::Begin("Simulator Info", &showInfoPanel,
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_AlwaysAutoResize);

        ImGui::SeparatorText("Controls");
        if (ImGui::BeginTable("controls", 2,
                              ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_BordersInnerV)) {
          ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed,
                                  80.0f);
          ImGui::TableSetupColumn("Action");
          ImGui::TableHeadersRow();

          auto row = [](const char *key, const char *action) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(key);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(action);
          };

          row("A / Z", "Right wheels fwd / rev");
          row("D / C", "Left wheels fwd / rev");
          row("A + D", "Drive forward");
          row("Z + C", "Drive backward");
          row("R", "Spawn red block");
          row("B", "Spawn blue block");
          row("F", "Intake block");
          row("G", "Outtake block");
          row("Arrows", "Pan camera");
          row("RMB drag", "Orbit camera");
          row("+  /  -", "Zoom in / out");
          row("H", "Toggle this panel");
          row("ESC", "Quit");

          ImGui::EndTable();
        }

        ImGui::SeparatorText("Status");
        ImGui::Text("Blocks on field: %d", static_cast<int>(blocks.size()));
        ImGui::Text("Blocks held: %d / %d", robot.GetHeldCount(), 8);
        ImGui::Text("FPS: %.0f", io.Framerate);

        ImGui::End();
      }

      ImGui::Render();
      ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

      vulkan.EndFrame();
    }
  }

  // --- Cleanup ---
  vkDeviceWaitIdle(vulkan.GetDevice());
  ImGui_ImplVulkan_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  vkDestroyDescriptorPool(vulkan.GetDevice(), imguiPool, nullptr);
  DestroyModel(vulkan.GetAllocator(), robotMeshes);
  DestroyModel(vulkan.GetAllocator(), fieldMeshes);
  DestroyModel(vulkan.GetAllocator(), redBlockMeshes);
  DestroyModel(vulkan.GetAllocator(), blueBlockMeshes);
  pipeline.Destroy(vulkan.GetDevice());
  vulkan.Cleanup();

  physics.Cleanup();

  glfwDestroyWindow(window);
  glfwTerminate();

  std::cout << "Simulator shut down cleanly." << std::endl;
  return 0;
}
