#include "PhysicsWorld.h"
#include "SimulationFilter.h" // [FIX] Moved to top

// Define PVD constants
#define PVD_HOST "127.0.0.1"

PhysicsWorld::PhysicsWorld() {}

PhysicsWorld::~PhysicsWorld() { Cleanup(); }

void PhysicsWorld::Initialize() {
  // 1. Foundation
  mFoundation =
      PxCreateFoundation(PX_PHYSICS_VERSION, mAllocator, mErrorCallback);
  if (!mFoundation) {
    std::cerr << "PxCreateFoundation failed!" << std::endl;
    return;
  }

  // 2. PVD (Visual Debugger)
  mPvd = PxCreatePvd(*mFoundation);
  PxPvdTransport *transport =
      PxDefaultPvdSocketTransportCreate(PVD_HOST, 5425, 10);
  mPvd->connect(*transport, PxPvdInstrumentationFlag::eALL);

  // 3. Physics
  mPhysics = PxCreatePhysics(PX_PHYSICS_VERSION, *mFoundation,
                             PxTolerancesScale(), true, mPvd);
  if (!mPhysics) {
    std::cerr << "PxCreatePhysics failed!" << std::endl;
    return;
  }

  // 4. Dispatcher (CPU Multithreading)
  mDispatcher = PxDefaultCpuDispatcherCreate(2); // 2 threads

  // 5. Scene
  PxSceneDesc sceneDesc(mPhysics->getTolerancesScale());
  sceneDesc.gravity = PxVec3(0.0f, -9.81f, 0.0f);
  sceneDesc.cpuDispatcher = mDispatcher;
  sceneDesc.filterShader = VehicleFilterShader; // Use our custom shader

  // Enable PVD in scene
  mScene = mPhysics->createScene(sceneDesc);
  PxPvdSceneClient *pvdClient = mScene->getScenePvdClient();
  if (pvdClient) {
    pvdClient->setScenePvdFlag(PxPvdSceneFlag::eTRANSMIT_CONSTRAINTS, true);
    pvdClient->setScenePvdFlag(PxPvdSceneFlag::eTRANSMIT_CONTACTS, true);
    pvdClient->setScenePvdFlag(PxPvdSceneFlag::eTRANSMIT_SCENEQUERIES, true);
  }

  // 6. Default Material
  mMaterial = mPhysics->createMaterial(
      0.5f, 0.5f,
      0.0f); // StaticFriction, DynamicFriction, Restitution (0.0 = no bounce)

  // 7. Cooking
  // PxCooking class is deprecated in PhysX 5. We use free functions
  // (PxCookTriangleMesh) instead. No explicit initialization needed for Cooking
  // library, just header inclusion.

  std::cout << "PhysX Initialized Successfully!" << std::endl;
}

void PhysicsWorld::Update(float deltaTime) {
  if (mScene) {
    mScene->simulate(deltaTime);
    mScene->fetchResults(true);
  }
}

void PhysicsWorld::Cleanup() {
  if (mScene)
    mScene->release();
  if (mDispatcher)
    mDispatcher->release();
  if (mPhysics)
    mPhysics->release();

  if (mPvd) {
    PxPvdTransport *transport = mPvd->getTransport();
    mPvd->release();
    if (transport)
      transport->release();
  }
  if (mFoundation)
    mFoundation->release();
}
