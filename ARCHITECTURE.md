# System Architecture

This document provides a high-level overview of the `simulator` codebase.

## Core Modules

The project is structured into several key components located in `src/`:

### 1. Main Loop (`main.cpp`)

- Initializes GLFW window and Vulkan context.
- Manages the main application loop (Input -> Physics Update -> Render).
- Handles ImGui overlay rendering.

### 2. Renderer (`src/renderer/`)

- **VulkanContext**: Wraps Vulkan instance, device, swapchain, and command pools.
- **Pipeline**: Manages graphics pipeline state (shaders, vertex input, rasterization).
- **ModelLoader**: Loads GLB/glTF models using `tinygltf` and uploads them to GPU buffers.
- **Camera**: Handles view/projection matrices and user input for camera movement.

### 3. Physics (`src/CollisionFilters.h`, `PhysicsWorld.cpp`, etc.)

- Uses **Nvidia PhysX 5** for rigid body simulation.
- **PhysicsWorld**: Manages the PhysX scene, materials, and simulation step.
- **FilterGroup**: Defines collision layers (Ground, Robot, Box) to control object interactions.

### 4. Game Objects

- **Robot.cpp**: Encapsulates robot state, drive train physics, and intake/outtake logic.
- **GameBlock.h**: Struct representing game elements (cubes) with physics bodies.

## Data Flow

1. **Initialization**:
    - Vulkan and PhysX are initialized.
    - Assets (Robot, Field, Blocks) are loaded from GLB files.
    - Physics bodies are created from these assets.

2. **Update Loop**:
    - **Input**: Key presses update robot motor states and camera transform.
    - **Physics**: `PxScene::simulate()` advances the physical world. Motors apply forces; collisions are resolved.
    - **Sync**: Renderable meshes query their corresponding Physics actors for updated transforms.
    - **Render**: Current state is drawn to the swapchain image.

## Dependencies

- **GLFW**: Windowing and input.
- **Vulkan**: Graphics API.
- **PhysX 5**: Physics engine.
- **glm**: Mathematics (vectors, matrices).
- **tinygltf**: Model loading.
- **Dear ImGui**: UI overlay.
