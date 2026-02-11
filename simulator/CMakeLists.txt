cmake_minimum_required(VERSION 3.14...3.31)

project(VexV5Simulator VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Specialized output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Enforce Static Runtime (/MT) Globally
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "" FORCE)
endif()

# --- Dependencies ---
include(FetchContent)

# 1. GLFW (Window + Input)
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

# 2. Vulkan SDK (must be installed on system)
find_package(Vulkan REQUIRED)

# 3. vk-bootstrap (Vulkan setup helper)
FetchContent_Declare(
    vk-bootstrap
    GIT_REPOSITORY https://github.com/charles-lunarg/vk-bootstrap.git
    GIT_TAG        v1.3.296
)
FetchContent_MakeAvailable(vk-bootstrap)

# 4. VMA (Vulkan Memory Allocator)
FetchContent_Declare(
    VulkanMemoryAllocator
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
    GIT_TAG        v3.1.0
)
FetchContent_MakeAvailable(VulkanMemoryAllocator)

# 5. GLM (Math library)
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.1
)
FetchContent_MakeAvailable(glm)

# 6. tinygltf (GLB/glTF model loading) - Header only
FetchContent_Declare(
    tinygltf
    GIT_REPOSITORY https://github.com/syoyo/tinygltf.git
    GIT_TAG        v2.9.4
)
FetchContent_MakeAvailable(tinygltf)
# 7. Dear ImGui (UI overlay) — no CMakeLists.txt, download sources only
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.8
    CONFIGURE_COMMAND ""
    BUILD_COMMAND ""
)
FetchContent_MakeAvailable(imgui)
set(IMGUI_DIR "${imgui_SOURCE_DIR}")
# 8. PhysX 5 (Manual Link)
set(PHYSX_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/external/physx/physx")
set(PHYSX_BIN_DIR "${PHYSX_ROOT}/bin/win.x86_64.vc143.mt/release")

# Check if PhysX is built
if(NOT EXISTS "${PHYSX_BIN_DIR}/PhysX_64.lib")
    message(FATAL_ERROR "PhysX libraries not found in ${PHYSX_BIN_DIR}. Please build PhysX first.")
endif()

# --- Shader Compilation ---
set(SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders")
set(SHADER_OUTPUT_DIR "${CMAKE_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY ${SHADER_OUTPUT_DIR})

# Find glslc from Vulkan SDK
find_program(GLSLC glslc HINTS "$ENV{VULKAN_SDK}/Bin")
if(NOT GLSLC)
    message(FATAL_ERROR "glslc not found! Make sure the Vulkan SDK is installed and in PATH.")
endif()

# Compile shaders
set(SHADER_SOURCES
    ${SHADER_DIR}/basic.vert
    ${SHADER_DIR}/basic.frag
)

set(SHADER_OUTPUTS "")
foreach(SHADER ${SHADER_SOURCES})
    get_filename_component(SHADER_NAME ${SHADER} NAME)
    set(SHADER_OUTPUT "${SHADER_OUTPUT_DIR}/${SHADER_NAME}.spv")
    add_custom_command(
        OUTPUT ${SHADER_OUTPUT}
        COMMAND ${GLSLC} ${SHADER} -o ${SHADER_OUTPUT}
        DEPENDS ${SHADER}
        COMMENT "Compiling shader: ${SHADER_NAME}"
    )
    list(APPEND SHADER_OUTPUTS ${SHADER_OUTPUT})
endforeach()

add_custom_target(shaders ALL DEPENDS ${SHADER_OUTPUTS})

# --- Sources ---
# NOTE: Robot, AssetLoader, PhysicsWorld are temporarily excluded
# during Vulkan migration. They will be re-added in Phase 4.
set(SOURCES
    src/main.cpp
    src/PhysicsWorld.cpp
    src/AssetLoader.cpp
    src/Robot.cpp
    src/renderer/VulkanContext.cpp
    src/renderer/Pipeline.cpp
    src/renderer/Camera.cpp
    src/renderer/Mesh.cpp
    src/renderer/ModelLoader.cpp
    # Dear ImGui core + backends
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp
)

add_executable(simulator ${SOURCES})
add_dependencies(simulator shaders)

# Enforce Static Runtime (/MT) for MSVC to match PhysX
if(MSVC)
    set_property(TARGET simulator PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Include Directories
target_include_directories(simulator PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${PHYSX_ROOT}/include
    ${tinygltf_SOURCE_DIR}
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

# Link Libraries
target_link_libraries(simulator PRIVATE 
    glfw
    Vulkan::Vulkan
    vk-bootstrap::vk-bootstrap
    GPUOpen::VulkanMemoryAllocator
    glm::glm
    "${PHYSX_BIN_DIR}/PhysX_64.lib"
    "${PHYSX_BIN_DIR}/PhysXCommon_64.lib"
    "${PHYSX_BIN_DIR}/PhysXFoundation_64.lib"
    "${PHYSX_BIN_DIR}/PhysXCooking_64.lib"
    "${PHYSX_BIN_DIR}/PhysXExtensions_static_64.lib"
    "${PHYSX_BIN_DIR}/PhysXPvdSDK_static_64.lib"
)

# Copy shaders to output directory
add_custom_command(TARGET simulator POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${SHADER_OUTPUT_DIR}"
    "$<TARGET_FILE_DIR:simulator>/shaders"
    COMMENT "Copying compiled shaders..."
)

# Copy assets to output directory
add_custom_command(TARGET simulator POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/assets"
    "$<TARGET_FILE_DIR:simulator>/assets"
    COMMENT "Copying assets..."
)

# Copy PhysX DLLs to output directory
add_custom_command(TARGET simulator POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${PHYSX_BIN_DIR}/PhysX_64.dll"
    "${PHYSX_BIN_DIR}/PhysXCommon_64.dll"
    "${PHYSX_BIN_DIR}/PhysXFoundation_64.dll"
    "${PHYSX_BIN_DIR}/PhysXCooking_64.dll"
    "${PHYSX_BIN_DIR}/PVDRuntime_64.dll"
    "$<TARGET_FILE_DIR:simulator>"
    COMMENT "Copying PhysX DLLs..."
)

# Compile definitions
target_compile_definitions(simulator PRIVATE
    TINYGLTF_NO_STB_IMAGE_WRITE
    GLM_FORCE_RADIANS
    GLM_FORCE_DEPTH_ZERO_TO_ONE
)
