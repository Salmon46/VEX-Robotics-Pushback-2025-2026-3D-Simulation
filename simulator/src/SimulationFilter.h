#pragma once
#include <PxPhysicsAPI.h>
#include <vector>

using namespace physx;

// Filter Groups
enum FilterGroup {
  eGROUND = (1 << 0),
  eCHASSIS = (1 << 1),
  eWHEEL = (1 << 2), // Wheels should not collide with Chassis
  eOBSTACLE = (1 << 3),
  eBLOCK = (1 << 4) // Game blocks
};

// Helper to set filter data on an actor
inline void SetActorFilter(PxRigidActor *actor, PxU32 filterGroup,
                           PxU32 filterMask) {
  PxFilterData filterData;
  filterData.word0 = filterGroup; // Word0 = Own ID
  filterData.word1 = filterMask;  // Word1 = Mask of what to collide with

  // Iterate shapes
  const PxU32 nbShapes = actor->getNbShapes();
  std::vector<PxShape *> shapes(nbShapes);
  actor->getShapes(shapes.data(), nbShapes);

  for (PxU32 i = 0; i < nbShapes; i++) {
    shapes[i]->setSimulationFilterData(filterData);
  }
}

// Custom Filter Shader
inline PxFilterFlags VehicleFilterShader(PxFilterObjectAttributes attributes0,
                                         PxFilterData filterData0,
                                         PxFilterObjectAttributes attributes1,
                                         PxFilterData filterData1,
                                         PxPairFlags &pairFlags,
                                         const void *constantBlock,
                                         PxU32 constantBlockSize) {
  // Let triggers through
  if (PxFilterObjectIsTrigger(attributes0) ||
      PxFilterObjectIsTrigger(attributes1)) {
    pairFlags = PxPairFlag::eTRIGGER_DEFAULT;
    return PxFilterFlag::eDEFAULT;
  }

  // Generate masks
  // 0 collides with 1 if (G0 & M1) AND (G1 & M0)
  if ((filterData0.word0 & filterData1.word1) &&
      (filterData1.word0 & filterData0.word1)) {
    pairFlags = PxPairFlag::eCONTACT_DEFAULT | PxPairFlag::eNOTIFY_TOUCH_FOUND;
    return PxFilterFlag::eDEFAULT;
  }

  // Otherwise, suppress collision
  return PxFilterFlag::eSUPPRESS;
}
