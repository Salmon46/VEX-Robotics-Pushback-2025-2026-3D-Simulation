#include "AssetLoader.h"
#include <iostream>
#include <vector>

void AssetLoader::ExtractMeshData(const tinygltf::Model &model,
                                  const tinygltf::Primitive &prim,
                                  std::vector<PxVec3> &vertices,
                                  std::vector<PxU32> &indices, PxVec3 scale) {
  // Extract Positions
  if (prim.attributes.find("POSITION") == prim.attributes.end())
    return;

  const tinygltf::Accessor &posAccessor =
      model.accessors.at(prim.attributes.at("POSITION"));
  const tinygltf::BufferView &posView =
      model.bufferViews[posAccessor.bufferView];
  const tinygltf::Buffer &posBuffer = model.buffers[posView.buffer];
  const float *posData = reinterpret_cast<const float *>(
      &posBuffer.data[posView.byteOffset + posAccessor.byteOffset]);
  size_t posStride =
      posView.byteStride ? posView.byteStride / sizeof(float) : 3;

  vertices.reserve(vertices.size() + posAccessor.count);
  for (size_t i = 0; i < posAccessor.count; i++) {
    vertices.push_back(PxVec3(posData[i * posStride + 0] * scale.x,
                              posData[i * posStride + 1] * scale.y,
                              posData[i * posStride + 2] * scale.z));
  }

  // Extract Indices
  if (prim.indices >= 0) {
    const tinygltf::Accessor &idxAccessor = model.accessors[prim.indices];
    const tinygltf::BufferView &idxView =
        model.bufferViews[idxAccessor.bufferView];
    const tinygltf::Buffer &idxBuffer = model.buffers[idxView.buffer];
    const uint8_t *idxData =
        &idxBuffer.data[idxView.byteOffset + idxAccessor.byteOffset];

    size_t baseVertex = vertices.size() - posAccessor.count;
    indices.reserve(indices.size() + idxAccessor.count);

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
        break;
      }
      indices.push_back(static_cast<PxU32>(baseVertex + idx));
    }
  } else {
    size_t baseVertex = vertices.size() - posAccessor.count;
    for (size_t i = 0; i < posAccessor.count; i++) {
      indices.push_back(static_cast<PxU32>(baseVertex + i));
    }
  }
}

PxRigidStatic *AssetLoader::CreateStaticBody(PxPhysics *physics, PxScene *scene,
                                             const tinygltf::Model &model,
                                             PxMaterial *material,
                                             PxTransform transform,
                                             PxVec3 scale) {
  std::cout << "[AssetLoader] Creating static body from " << model.meshes.size()
            << " meshes." << std::endl;

  PxRigidStatic *body = physics->createRigidStatic(transform);
  int shapeCount = 0;

  for (size_t m = 0; m < model.meshes.size(); m++) {
    for (size_t p = 0; p < model.meshes[m].primitives.size(); p++) {
      std::vector<PxVec3> vertices;
      std::vector<PxU32> indices;

      ExtractMeshData(model, model.meshes[m].primitives[p], vertices, indices,
                      scale);

      if (vertices.empty() || indices.size() < 3)
        continue;

      // Cook triangle mesh
      PxTriangleMeshDesc meshDesc;
      meshDesc.points.count = static_cast<PxU32>(vertices.size());
      meshDesc.points.stride = sizeof(PxVec3);
      meshDesc.points.data = vertices.data();
      meshDesc.triangles.count = static_cast<PxU32>(indices.size() / 3);
      meshDesc.triangles.stride = 3 * sizeof(PxU32);
      meshDesc.triangles.data = indices.data();

      PxDefaultMemoryOutputStream writeBuffer;
      PxTriangleMeshCookingResult::Enum cookResult;
      bool success =
          PxCookTriangleMesh(PxCookingParams(physics->getTolerancesScale()),
                             meshDesc, writeBuffer, &cookResult);

      if (!success) {
        std::cerr << "[AssetLoader] Failed to cook triangle mesh " << m << "."
                  << p << std::endl;
        continue;
      }

      PxDefaultMemoryInputData readBuffer(writeBuffer.getData(),
                                          writeBuffer.getSize());
      PxTriangleMesh *triMesh = physics->createTriangleMesh(readBuffer);

      if (triMesh) {
        PxShape *shape =
            physics->createShape(PxTriangleMeshGeometry(triMesh), *material);
        body->attachShape(*shape);
        shape->release();
        triMesh->release();
        shapeCount++;
      }
    }
  }

  if (shapeCount > 0) {
    scene->addActor(*body);
    std::cout << "[AssetLoader] Static body created with " << shapeCount
              << " shapes." << std::endl;
  } else {
    body->release();
    body = nullptr;
    std::cerr << "[AssetLoader] No valid shapes, static body not created!"
              << std::endl;
  }

  return body;
}

PxRigidDynamic *AssetLoader::CreateDynamicConvexBody(
    PxPhysics *physics, PxScene *scene, const tinygltf::Model &model,
    PxMaterial *material, PxTransform transform, float density, PxVec3 scale) {

  // Collect all vertices from all primitives
  std::vector<PxVec3> allVertices;
  std::vector<PxU32> dummyIndices;

  for (size_t m = 0; m < model.meshes.size(); m++) {
    for (size_t p = 0; p < model.meshes[m].primitives.size(); p++) {
      ExtractMeshData(model, model.meshes[m].primitives[p], allVertices,
                      dummyIndices, scale);
    }
  }

  if (allVertices.empty()) {
    std::cerr << "[AssetLoader] No vertices for convex body!" << std::endl;
    return nullptr;
  }

  // Cook convex hull
  PxConvexMeshDesc convexDesc;
  convexDesc.points.count = static_cast<PxU32>(allVertices.size());
  convexDesc.points.stride = sizeof(PxVec3);
  convexDesc.points.data = allVertices.data();
  convexDesc.flags = PxConvexFlag::eCOMPUTE_CONVEX;

  PxDefaultMemoryOutputStream writeBuffer;
  PxConvexMeshCookingResult::Enum cookResult;
  bool success =
      PxCookConvexMesh(PxCookingParams(physics->getTolerancesScale()),
                       convexDesc, writeBuffer, &cookResult);

  if (!success) {
    std::cerr << "[AssetLoader] Failed to cook convex mesh!" << std::endl;
    return nullptr;
  }

  PxDefaultMemoryInputData readBuffer(writeBuffer.getData(),
                                      writeBuffer.getSize());
  PxConvexMesh *convexMesh = physics->createConvexMesh(readBuffer);

  if (!convexMesh) {
    std::cerr << "[AssetLoader] Failed to create convex mesh!" << std::endl;
    return nullptr;
  }

  PxRigidDynamic *body = physics->createRigidDynamic(transform);
  PxShape *shape =
      physics->createShape(PxConvexMeshGeometry(convexMesh), *material);
  body->attachShape(*shape);
  shape->release();
  convexMesh->release();

  PxRigidBodyExt::updateMassAndInertia(*body, density);
  scene->addActor(*body);

  std::cout << "[AssetLoader] Dynamic convex body created." << std::endl;
  return body;
}
