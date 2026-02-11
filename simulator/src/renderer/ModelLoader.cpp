#include "renderer/ModelLoader.h"
#include "renderer/Pipeline.h" // For Vertex

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <tiny_gltf.h>

#include <cstring>
#include <iostream>
#include <stdexcept>

static void ExtractPrimitive(const tinygltf::Model &model,
                             const tinygltf::Primitive &prim,
                             std::vector<Vertex> &outVertices,
                             std::vector<uint32_t> &outIndices) {
  // --- Positions (required) ---
  if (prim.attributes.find("POSITION") == prim.attributes.end()) {
    std::cerr << "[ModelLoader] Primitive missing POSITION attribute, skipping."
              << std::endl;
    return;
  }

  const tinygltf::Accessor &posAccessor =
      model.accessors[prim.attributes.at("POSITION")];
  const tinygltf::BufferView &posView =
      model.bufferViews[posAccessor.bufferView];
  const tinygltf::Buffer &posBuffer = model.buffers[posView.buffer];
  const float *posData = reinterpret_cast<const float *>(
      &posBuffer.data[posView.byteOffset + posAccessor.byteOffset]);
  size_t posStride =
      posView.byteStride ? posView.byteStride / sizeof(float) : 3;

  size_t vertexCount = posAccessor.count;

  // --- Normals (optional) ---
  const float *normalData = nullptr;
  size_t normalStride = 3;
  if (prim.attributes.find("NORMAL") != prim.attributes.end()) {
    const tinygltf::Accessor &normAccessor =
        model.accessors[prim.attributes.at("NORMAL")];
    const tinygltf::BufferView &normView =
        model.bufferViews[normAccessor.bufferView];
    const tinygltf::Buffer &normBuffer = model.buffers[normView.buffer];
    normalData = reinterpret_cast<const float *>(
        &normBuffer.data[normView.byteOffset + normAccessor.byteOffset]);
    normalStride =
        normView.byteStride ? normView.byteStride / sizeof(float) : 3;
  }

  // --- Colors (optional, COLOR_0) ---
  const float *colorData = nullptr;
  size_t colorStride = 3;
  int colorComponents = 3;
  const uint8_t *colorDataU8 = nullptr;
  bool colorIsU8 = false;

  if (prim.attributes.find("COLOR_0") != prim.attributes.end()) {
    const tinygltf::Accessor &colAccessor =
        model.accessors[prim.attributes.at("COLOR_0")];
    const tinygltf::BufferView &colView =
        model.bufferViews[colAccessor.bufferView];
    const tinygltf::Buffer &colBuffer = model.buffers[colView.buffer];

    colorComponents = (colAccessor.type == TINYGLTF_TYPE_VEC4) ? 4 : 3;

    if (colAccessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT) {
      colorData = reinterpret_cast<const float *>(
          &colBuffer.data[colView.byteOffset + colAccessor.byteOffset]);
      colorStride = colView.byteStride ? colView.byteStride / sizeof(float)
                                       : colorComponents;
    } else if (colAccessor.componentType ==
               TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
      colorDataU8 =
          &colBuffer.data[colView.byteOffset + colAccessor.byteOffset];
      colorIsU8 = true;
      colorStride = colView.byteStride ? colView.byteStride : colorComponents;
    }
  }

  // --- Build vertices ---
  outVertices.reserve(outVertices.size() + vertexCount);
  for (size_t i = 0; i < vertexCount; i++) {
    Vertex v = {};

    v.position[0] = posData[i * posStride + 0];
    v.position[1] = posData[i * posStride + 1];
    v.position[2] = posData[i * posStride + 2];

    if (normalData) {
      v.normal[0] = normalData[i * normalStride + 0];
      v.normal[1] = normalData[i * normalStride + 1];
      v.normal[2] = normalData[i * normalStride + 2];
    } else {
      v.normal[0] = 0.0f;
      v.normal[1] = 1.0f;
      v.normal[2] = 0.0f;
    }

    if (colorData) {
      v.color[0] = colorData[i * colorStride + 0];
      v.color[1] = colorData[i * colorStride + 1];
      v.color[2] = colorData[i * colorStride + 2];
    } else if (colorIsU8 && colorDataU8) {
      v.color[0] = colorDataU8[i * colorStride + 0] / 255.0f;
      v.color[1] = colorDataU8[i * colorStride + 1] / 255.0f;
      v.color[2] = colorDataU8[i * colorStride + 2] / 255.0f;
    } else {
      // Fallback: use material baseColorFactor if available
      float matR = 0.7f, matG = 0.7f, matB = 0.7f;
      if (prim.material >= 0 &&
          prim.material < static_cast<int>(model.materials.size())) {
        const auto &mat = model.materials[prim.material];
        const auto &bcf = mat.pbrMetallicRoughness.baseColorFactor;
        if (bcf.size() >= 3) {
          matR = static_cast<float>(bcf[0]);
          matG = static_cast<float>(bcf[1]);
          matB = static_cast<float>(bcf[2]);
        }
      }
      v.color[0] = matR;
      v.color[1] = matG;
      v.color[2] = matB;
    }

    outVertices.push_back(v);
  }

  // --- Indices ---
  if (prim.indices >= 0) {
    const tinygltf::Accessor &idxAccessor = model.accessors[prim.indices];
    const tinygltf::BufferView &idxView =
        model.bufferViews[idxAccessor.bufferView];
    const tinygltf::Buffer &idxBuffer = model.buffers[idxView.buffer];
    const uint8_t *idxData =
        &idxBuffer.data[idxView.byteOffset + idxAccessor.byteOffset];

    size_t baseVertex = outVertices.size() - vertexCount;
    outIndices.reserve(outIndices.size() + idxAccessor.count);

    for (size_t i = 0; i < idxAccessor.count; i++) {
      uint32_t idx = 0;
      switch (idxAccessor.componentType) {
      case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
        idx = reinterpret_cast<const uint16_t *>(idxData)[i];
        break;
      case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
        idx = reinterpret_cast<const uint32_t *>(idxData)[i];
        break;
      case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
        idx = idxData[i];
        break;
      default:
        std::cerr << "[ModelLoader] Unsupported index type: "
                  << idxAccessor.componentType << std::endl;
        break;
      }
      outIndices.push_back(static_cast<uint32_t>(baseVertex + idx));
    }
  } else {
    // No indices: generate sequential
    size_t baseVertex = outVertices.size() - vertexCount;
    for (size_t i = 0; i < vertexCount; i++) {
      outIndices.push_back(static_cast<uint32_t>(baseVertex + i));
    }
  }
}

std::vector<Mesh> LoadModel(VkDevice device, VmaAllocator allocator,
                            VkQueue queue, uint32_t queueFamily,
                            const std::string &path) {
  tinygltf::Model model;
  tinygltf::TinyGLTF loader;
  std::string err, warn;

  bool loaded = loader.LoadBinaryFromFile(&model, &err, &warn, path);
  if (!warn.empty())
    std::cout << "[ModelLoader] Warning: " << warn << std::endl;
  if (!err.empty())
    std::cerr << "[ModelLoader] Error: " << err << std::endl;
  if (!loaded) {
    throw std::runtime_error("[ModelLoader] Failed to load: " + path);
  }

  std::cout << "[ModelLoader] Loaded: " << path << " (" << model.meshes.size()
            << " meshes)" << std::endl;

  std::vector<Mesh> result;

  for (size_t m = 0; m < model.meshes.size(); m++) {
    const tinygltf::Mesh &mesh = model.meshes[m];
    for (size_t p = 0; p < mesh.primitives.size(); p++) {
      std::vector<Vertex> vertices;
      std::vector<uint32_t> indices;

      ExtractPrimitive(model, mesh.primitives[p], vertices, indices);

      if (vertices.empty() || indices.empty())
        continue;

      Mesh vkMesh =
          CreateMesh(device, allocator, queue, queueFamily, vertices, indices);
      result.push_back(vkMesh);

      std::cout << "  Mesh[" << m << "].prim[" << p << "]: " << vertices.size()
                << " verts, " << indices.size() << " indices" << std::endl;
    }
  }

  std::cout << "[ModelLoader] Total primitives: " << result.size() << std::endl;
  return result;
}

void DestroyModel(VmaAllocator allocator, std::vector<Mesh> &meshes) {
  for (auto &mesh : meshes) {
    DestroyMesh(allocator, mesh);
  }
  meshes.clear();
}

void DrawModel(VkCommandBuffer cmd, const std::vector<Mesh> &meshes) {
  for (const auto &mesh : meshes) {
    DrawMesh(cmd, mesh);
  }
}
