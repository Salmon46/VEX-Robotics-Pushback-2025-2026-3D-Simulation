#pragma once

#include <PxPhysicsAPI.h>

using namespace physx;

enum class BlockColor { RED, BLUE };

struct GameBlock {
  PxRigidDynamic *body = nullptr;
  BlockColor color = BlockColor::RED;
  bool held = false;
};
