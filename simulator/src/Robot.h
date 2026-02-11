#pragma once

#include <PxPhysicsAPI.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>

#include "GameBlock.h"

using namespace physx;

class Robot {
public:
  Robot();
  ~Robot();

  // Initialize the robot physics (chassis + 8-wheel drive)
  void Initialize(PxPhysics *physics, PxScene *scene, PxMaterial *material,
                  PxVec3 startPos);

  // Update simulation (apply motor forces)
  void Update(float dt);

  // Drive controls: left [-1,1], right [-1,1]
  void SetDriveInput(float left, float right) {
    mThrottleInput = left; // Repurposed: left side power
    mTurnInput = right;    // Repurposed: right side power
  }

  // Get the model transform matrix from physics pose
  glm::mat4 GetTransformMatrix(float visualScale = 0.01f) const;

  // --- Intake/Outtake ---
  // Try to pick up a block (checks proximity to front of robot). Max 8 blocks.
  bool TryIntake(GameBlock &block, PxPhysics *physics);
  // Eject the most recently held block forward
  void Outtake();
  // Is robot currently holding any blocks?
  bool HasBlock() const { return !mHeldBlocks.empty(); }
  // How many blocks are held?
  int GetHeldCount() const { return static_cast<int>(mHeldBlocks.size()); }
  // Is intake full?
  bool IsIntakeFull() const { return mHeldBlocks.size() >= MAX_HELD_BLOCKS; }

  // Get world position of robot's front face
  PxVec3 GetFrontPosition() const;

  // Accessors
  PxRigidDynamic *GetChassis() const { return mChassis; }

private:
  void CreateWheels(PxPhysics *physics, PxScene *scene, PxMaterial *material);

  // Physics objects
  PxRigidDynamic *mChassis;
  std::vector<PxRigidDynamic *> mWheels;
  std::vector<PxRevoluteJoint *> mWheelJoints;
  PxMaterial *mWheelMaterial;

  // Intake state — holds up to MAX_HELD_BLOCKS blocks
  struct HeldBlock {
    GameBlock *block;
    PxFixedJoint *joint;
  };
  std::vector<HeldBlock> mHeldBlocks;
  static constexpr size_t MAX_HELD_BLOCKS = 8;

  // Drive state
  float mThrottleInput;
  float mTurnInput;

  // Configuration (VEX Robot dimensions)
  const float ROBOT_WIDTH = 0.35f;
  const float ROBOT_LENGTH = 0.35f;
  const float WHEEL_RADIUS = 0.055f;
  const float WHEEL_WIDTH = 0.025f;
  const float CHASSIS_DENSITY = 50.0f;
  const float WHEEL_DENSITY = 10.0f;
  const float DRIVE_TORQUE = 500.0f;
  const float INTAKE_RANGE = 0.35f;
  const float OUTTAKE_IMPULSE = 0.5f;
};
