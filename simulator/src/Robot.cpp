#include "Robot.h"
#include "SimulationFilter.h"
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <iostream>

Robot::Robot()
    : mChassis(nullptr), mThrottleInput(0.0f), mTurnInput(0.0f),
      mWheelMaterial(nullptr) {}

Robot::~Robot() {
  // Physics objects are released by the scene/physics release
}

void Robot::Initialize(PxPhysics *physics, PxScene *scene, PxMaterial *material,
                       PxVec3 startPos) {
  // 1. Create Chassis with a simple box (mesh hulls cause instability)
  mChassis = physics->createRigidDynamic(PxTransform(startPos));
  if (!mChassis) {
    std::cerr << "[Robot] Failed to create chassis!" << std::endl;
    return;
  }

  // Simple box matching robot footprint
  PxShape *chassisShape = physics->createShape(
      PxBoxGeometry(ROBOT_WIDTH / 2.0f, 0.15f, ROBOT_LENGTH / 2.0f), *material);
  mChassis->attachShape(*chassisShape);
  chassisShape->release();
  PxRigidBodyExt::updateMassAndInertia(*mChassis, CHASSIS_DENSITY);
  scene->addActor(*mChassis);

  // Filter: Chassis collides with GROUND, OBSTACLE, BLOCK
  SetActorFilter(mChassis, FilterGroup::eCHASSIS,
                 FilterGroup::eGROUND | FilterGroup::eOBSTACLE |
                     FilterGroup::eCHASSIS | FilterGroup::eBLOCK);

  // Damping
  mChassis->setLinearDamping(0.5f);
  mChassis->setAngularDamping(0.05f);

  // Create slippery material for wheels (allows skid-steering)
  mWheelMaterial = physics->createMaterial(0.2f, 0.2f, 0.0f);

  // 2. Create Wheels
  CreateWheels(physics, scene, mWheelMaterial);

  std::cout << "[Robot] Initialized at (" << startPos.x << ", " << startPos.y
            << ", " << startPos.z << ")" << std::endl;
}

void Robot::CreateWheels(PxPhysics *physics, PxScene *scene,
                         PxMaterial *material) {
  float xOffset = ROBOT_WIDTH / 2.0f;
  float zSpacing = ROBOT_LENGTH / 3.0f;
  float zStart = -ROBOT_LENGTH / 2.0f;

  for (int side = -1; side <= 1; side += 2) {
    for (int i = 0; i < 4; i++) {
      float zPos = zStart + (i * zSpacing);

      PxShape *wheelShape = physics->createShape(
          PxCapsuleGeometry(WHEEL_RADIUS, WHEEL_WIDTH / 2.0f), *material);

      PxTransform wheelLocalPose(side * xOffset, -0.20f, zPos);
      PxTransform chassisPose = mChassis->getGlobalPose();
      PxTransform wheelGlobalPose = chassisPose.transform(wheelLocalPose);

      PxRigidDynamic *wheelActor = physics->createRigidDynamic(wheelGlobalPose);
      wheelActor->attachShape(*wheelShape);
      wheelShape->release();
      PxRigidBodyExt::updateMassAndInertia(*wheelActor, WHEEL_DENSITY);

      // Wheels collide with ground, obstacles, AND blocks
      SetActorFilter(wheelActor, FilterGroup::eWHEEL,
                     FilterGroup::eGROUND | FilterGroup::eOBSTACLE |
                         FilterGroup::eBLOCK);

      scene->addActor(*wheelActor);
      mWheels.push_back(wheelActor);

      // Create revolute joint
      PxTransform jointFrameChassis(wheelLocalPose.p);
      PxTransform jointFrameWheel(PxVec3(0, 0, 0));

      PxRevoluteJoint *joint = PxRevoluteJointCreate(
          *physics, mChassis, jointFrameChassis, wheelActor, jointFrameWheel);

      joint->setDriveVelocity(0.0f);
      joint->setRevoluteJointFlag(PxRevoluteJointFlag::eDRIVE_ENABLED, true);
      joint->setDriveForceLimit(DRIVE_TORQUE);

      mWheelJoints.push_back(joint);
    }
  }
}

void Robot::Update(float dt) {
  if (!mChassis)
    return;

  // Direct left/right drive (mThrottleInput = left, mTurnInput = right)
  float leftInput = std::max(-1.0f, std::min(1.0f, mThrottleInput));
  float rightInput = std::max(-1.0f, std::min(1.0f, mTurnInput));

  float maxVelocity = 20.0f; // Rad/s

  for (size_t i = 0; i < mWheelJoints.size(); i++) {
    PxRevoluteJoint *joint = mWheelJoints[i];
    float input = (i < 4) ? leftInput : rightInput;
    joint->setDriveVelocity(input * maxVelocity);
    joint->setDriveForceLimit(DRIVE_TORQUE);
  }
}

glm::mat4 Robot::GetTransformMatrix(float visualScale) const {
  if (!mChassis)
    return glm::mat4(1.0f);

  PxTransform pose = mChassis->getGlobalPose();

  glm::quat q(pose.q.w, pose.q.x, pose.q.y, pose.q.z);
  glm::mat4 rotation = glm::mat4_cast(q);

  glm::mat4 translation =
      glm::translate(glm::mat4(1.0f), glm::vec3(pose.p.x, pose.p.y, pose.p.z));

  glm::mat4 scale = glm::scale(glm::mat4(1.0f), glm::vec3(visualScale));

  return translation * rotation * scale;
}

// --- Intake/Outtake ---

PxVec3 Robot::GetFrontPosition() const {
  if (!mChassis)
    return PxVec3(0);

  PxTransform pose = mChassis->getGlobalPose();
  // Front is +Z in local space
  PxVec3 localFront(0.0f, 0.0f, ROBOT_LENGTH / 2.0f + 0.05f);
  return pose.transform(localFront);
}

bool Robot::TryIntake(GameBlock &block, PxPhysics *physics) {
  if (!mChassis || block.held || !block.body)
    return false;

  // Check capacity
  if (IsIntakeFull())
    return false;

  // Check distance from block to robot front
  PxVec3 frontPos = GetFrontPosition();
  PxVec3 blockPos = block.body->getGlobalPose().p;
  float dist = (frontPos - blockPos).magnitude();

  if (dist > INTAKE_RANGE)
    return false;

  // Teleport block to inside the robot
  PxTransform chassisPose = mChassis->getGlobalPose();
  PxVec3 holdPos = chassisPose.transform(PxVec3(0.0f, 0.0f, 0.0f));
  block.body->setGlobalPose(PxTransform(holdPos, chassisPose.q));
  block.body->setLinearVelocity(PxVec3(0));
  block.body->setAngularVelocity(PxVec3(0));

  // Disable collision while held
  PxU32 numShapes = block.body->getNbShapes();
  if (numShapes > 0) {
    PxShape *shape = nullptr;
    block.body->getShapes(&shape, 1);
    if (shape) {
      PxFilterData fd;
      fd.word0 = 0;
      fd.word1 = 0;
      shape->setSimulationFilterData(fd);
    }
  }

  // Attach at robot center via fixed joint
  PxTransform localFrame(PxVec3(0.0f, 0.0f, 0.0f));
  PxFixedJoint *joint = PxFixedJointCreate(*physics, mChassis, localFrame,
                                           block.body, PxTransform(PxIdentity));

  if (joint) {
    HeldBlock hb;
    hb.block = &block;
    hb.joint = joint;
    mHeldBlocks.push_back(hb);
    block.held = true;
    std::cout << "[Robot] Intake! Holding " << mHeldBlocks.size() << "/"
              << MAX_HELD_BLOCKS << " blocks." << std::endl;
    return true;
  }

  return false;
}

void Robot::Outtake() {
  if (mHeldBlocks.empty())
    return;

  // Eject the most recently picked up block (LIFO)
  HeldBlock hb = mHeldBlocks.back();
  mHeldBlocks.pop_back();

  if (!hb.block || !hb.block->body)
    return;

  // Release joint
  if (hb.joint) {
    hb.joint->release();
  }

  // Teleport block to front of robot
  PxTransform pose = mChassis->getGlobalPose();
  PxVec3 forward = pose.q.rotate(PxVec3(0, 0, 1));
  PxVec3 ejectPos = pose.p + forward * (ROBOT_LENGTH / 2.0f + 0.15f);
  ejectPos.y = pose.p.y; // Same height as chassis

  hb.block->body->setGlobalPose(PxTransform(ejectPos, pose.q));
  hb.block->body->setLinearVelocity(PxVec3(0));
  hb.block->body->setAngularVelocity(PxVec3(0));

  // Re-enable collision
  PxU32 numShapes = hb.block->body->getNbShapes();
  if (numShapes > 0) {
    PxShape *shape = nullptr;
    hb.block->body->getShapes(&shape, 1);
    if (shape) {
      PxFilterData fd;
      fd.word0 = FilterGroup::eBLOCK;
      fd.word1 = FilterGroup::eGROUND | FilterGroup::eCHASSIS |
                 FilterGroup::eOBSTACLE | FilterGroup::eBLOCK |
                 FilterGroup::eWHEEL;
      shape->setSimulationFilterData(fd);
    }
  }

  // Give a gentle forward velocity instead of impulse (block is very light)
  hb.block->body->setLinearVelocity(forward * 1.0f);

  hb.block->held = false;

  std::cout << "[Robot] Outtake! " << mHeldBlocks.size() << " blocks remaining."
            << std::endl;
}
