#pragma once

#include "PxPhysicsAPI.h"
#include "cooking/PxCooking.h"
#include <iostream>

using namespace physx;

class PhysicsWorld {
public:
  PhysicsWorld();
  ~PhysicsWorld();

  void Initialize();
  void Cleanup();

  // Simulation
  void Update(float deltaTime);

  // Getters
  PxPhysics *GetPhysics() const { return mPhysics; }
  PxScene *GetScene() const { return mScene; }
  PxMaterial *GetDefaultMaterial() const { return mMaterial; }

private:
  // Core PhysX Objects
  PxFoundation *mFoundation = nullptr;
  PxPhysics *mPhysics = nullptr;
  PxDefaultCpuDispatcher *mDispatcher = nullptr;
  PxScene *mScene = nullptr;
  PxMaterial *mMaterial = nullptr;
  PxPvd *mPvd = nullptr; // Visual Debugger

  // Memory Management
  PxDefaultAllocator mAllocator;
  PxDefaultErrorCallback mErrorCallback;
};
