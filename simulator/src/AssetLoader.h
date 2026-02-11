#pragma once

#include "PxPhysicsAPI.h"
#include "cooking/PxCooking.h"
#include <tiny_gltf.h>
#include <vector>

using namespace physx;

class AssetLoader {
public:
  // Creates a PxRigidStatic from a tinygltf Model (triangle mesh collision)
  static PxRigidStatic *CreateStaticBody(PxPhysics *physics, PxScene *scene,
                                         const tinygltf::Model &model,
                                         PxMaterial *material,
                                         PxTransform transform,
                                         PxVec3 scale = PxVec3(1.0f));

  // Creates a PxRigidDynamic from a tinygltf Model (convex hull collision)
  static PxRigidDynamic *
  CreateDynamicConvexBody(PxPhysics *physics, PxScene *scene,
                          const tinygltf::Model &model, PxMaterial *material,
                          PxTransform transform, float density,
                          PxVec3 scale = PxVec3(1.0f));

private:
  // Helper to extract vertices and indices from a tinygltf primitive
  static void ExtractMeshData(const tinygltf::Model &model,
                              const tinygltf::Primitive &prim,
                              std::vector<PxVec3> &vertices,
                              std::vector<PxU32> &indices, PxVec3 scale);
};
