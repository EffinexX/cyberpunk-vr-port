#pragma once
// The native side of the hands, the gun and what they carry: docs/native-hand-physics-build-20261108.md.
#include <cstdint>

namespace cvr::physicsruntime {
// From VRIK_ApplyHandStop, the animation pass: the hand's model-space target and turn, changed in place by what stops the hand and the gun in it.
void AfterHandStop(int side,float* target,float* handRot);
// Once a frame from the script: installs the detours when PhysX is there.
void Tick();
bool RunsLists();    // the simulate detour and the worker keep the world lists
bool HandsNative();  // the hands are solved in the animation pass
bool IsCarried(void* actor);
void NoteScriptStop(int side,const float* moved);   // how far the script's stop moved a hand this pass, model space
bool HandSpin(int side,float* worldRate);   // how fast that wrist turns, rad/s
bool HandTurn(int side,float* worldTurn);   // the wrist as the arm is solved with it this frame, world
void NoteHandTurn(int side,const float* worldTurn);   // the script's turn of a hand's shape, world, against which the animation pass's is measured
}

namespace cvr::debugview {
// Natives/DebugView.cpp, for the above.
void RuntimeCollect(const float* center);
void RuntimeScan(const float* center);
bool WorldCenter(float* out);
bool MoverSolve(int id,const float* p,const float* q,const float* anchor,float skip,bool turnAnswer,float* outP,float* outQ,bool* contact);
bool MoverAsked(int id,uint64_t withinMs);
void MoverTouched(int id);
void MoverReset(int id);
void MoverPush(int id,float* out);
void* FindDynamicActor(uint32_t proxy,uint32_t body,float* origin);
int ActorParts(void* actor,int moverId);
}
