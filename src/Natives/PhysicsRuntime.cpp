// PhysicsRuntime -- the hands, the gun and what they carry, solved where it counts and off the script's thread.
//
// docs/native-hand-physics-build-20261108.md has the reasons and what is proved. In short:
//   ANIMATION PASS (AfterHandStop, from VRIK_ApplyHandStop): the hands and the gun against the published world, applied to the arm's target in the same pass. The frame of delay through the script is gone.
//   PxScene::simulate (detour): the moving things are gathered, then each carried body is given the velocity that lands it on its pose for this step. It is where the hand will be when the step's result is seen.
//   simulate .. fetchResults: the static near list is scanned on a worker. In that window PhysX buffers scene writes and frees nothing, and fetchResults waits for the worker.
//
// EVERY PATH FALLS BACK BY ITSELF to the script's, and the master switch is xr_native_physics. Slots are proved by address before the first call: a wrong slot once wrote a zero pose over live shapes.
#include "Natives/PhysicsRuntime.hpp"
#include "Natives/HeldPicture.hpp"
#include "Natives/NativeFunctions.hpp"
#include "Physics/ProxySolver.hpp"
#include "Core/LiveControls.hpp"
#include "MinHook.h"
#include <RED4ext/RED4ext.hpp>
#include <RED4ext/Scripting/Natives/Generated/Vector4.hpp>
#include <windows.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "Core/GameBuild.hpp"

extern void Log(const char* fmt,...);
extern "C" {
    extern float CyberpunkVR_PlayerEntityPos[3];
    extern float CyberpunkVR_PlayerEntityQuat[4];
    extern int   CyberpunkVR_PlayerEntityValid;
}

namespace cvr::physicsruntime {
namespace {
namespace ph=cvr::physics;
namespace dv=cvr::debugview;
using ph::V3;using ph::Q;using ph::Pose;

constexpr uintptr_t kSceneTableRva=0x20CAF0,kSimulateRva=0x53760,kFetchResultsRva=0x53980;
constexpr int kSceneSimulate=56,kSceneFetchResults=61;
constexpr uintptr_t kDynamicTableRva=0x20B2E8;
struct Proved {int slot;uintptr_t rva;};
constexpr int kGetGlobalPose=20,kSetGlobalPose=21,kGetLinear=37,kSetLinear=38,kGetAngular=39,kSetAngular=40,kSetFlag=45,kGetFlags=47,kGetLinearDamping=57,kGetAngularDamping=59,kSetMaxSpin=60;
constexpr Proved kDynamicSlots[]={{kSetGlobalPose,0x4A8E0},{kGetLinear,0x4DCC0},{kSetLinear,0x4BD90},{kGetAngular,0x4D800},{kSetAngular,0x4BE10},{kSetFlag,0x4F1F0},{kGetFlags,0x4DED0},{kGetLinearDamping,0x4BCA0},{kGetAngularDamping,0x4BD60},{kSetMaxSpin,0x4BE90}};
template<class F> F Slot(void* o,int i) {return reinterpret_cast<F>((*reinterpret_cast<void***>(o))[i]);}
struct PxPose {Q q;V3 p;};

int64_t Now() {LARGE_INTEGER t;QueryPerformanceCounter(&t);return t.QuadPart;}
double Seconds(int64_t a,int64_t b) {static const double f=[] {LARGE_INTEGER q;QueryPerformanceFrequency(&q);return double(q.QuadPart);}();return double(b-a)/f;}
inline bool Enabled() {return g_liveControls.xrNativePhysics!=0;}

// ---- The hand, as it was last solved, in the world.
struct HandSample {Pose pose{},base{};V3 velocity{},spin{};int64_t at=0,baseAt=0;float period=.022f;bool valid=false;};
SRWLOCK handLock=SRWLOCK_INIT;
HandSample hands[2];
// THE SCRIPT'S FRAME OF A HAND IS ITS BONE, THIS FILE'S THE ARM SOLVE'S TARGET, and nothing proves they are the same turn. The difference is measured, slowly, and kept: a hand's shape, and anything held, are placed through it.
Q handTurnFix[2];
bool handTurnFixed[2]={false,false};

// ---- The gun in a hand.
struct GunHold {Pose grip{};bool have=false;int side=-1;bool touching=false;};
GunHold gun;
SRWLOCK gunLock=SRWLOCK_INIT;

// ---- A carried body.
// ALWAYS BY VELOCITY. Kinematic with setKinematicTarget was built first and worked in PhysX, and the game's own body position followed it, but the renderer did not: only the collision was carried, the mesh stayed behind and
// came back on release (seen in game 2026-11-08, kinds 10 and 12). The game draws what is dynamic, so the body stays dynamic and is given, just before the step, exactly the velocity that lands it on the target.
enum Mode:int {None=0,Velocity=2};
struct Carry {
    bool used=false,released=false,throwIt=false;
    uint32_t proxy=0,body=0;
    void* actor=nullptr;
    int side=0,mode=None,moverId=3;
    Pose offset{};       // the body in the script's frame of the hand
    int handFixSamples=0;
    Pose handFix{};bool handFixSet=false;   // the script's frame of the hand, in the sample's
    Pose fromGame{};     // the actor in the frame of what the game calls the body
    uint64_t askedAt=0;
    Pose command{};
    V3 throwV{},throwW{};
    float blocked=0;
    bool spinRaised=false;
    int kinematicSteps=0;bool clearedDirect=false;
    V3 grabbedAt{},playerAtGrab{};uint64_t grabbedTick=0;
    double landedSum=0;float landedMost=0,blockedMost=0;int landedSteps=0,handChanges=0;
    int otherSide=-1;bool handStops=false;
    void* swapTo=nullptr;void* realActor=nullptr;bool swapped=false;float realGoneAt=0;
    V3 placedAt{};bool placedSet=false;const uint8_t* instance=nullptr;
    int offNoContactSteps=0,offSteps=0;uint64_t offSince=0;float offRunMost=0;
    PxPose aimed{};bool aimedSet=false;
};
SRWLOCK carryLock=SRWLOCK_INIT;
Carry carries[2];
std::atomic<void*> carriedActors[2];
// THE REAL BODY OF A CARRY THAT HAS GONE OVER TO A SPAWNED REPLACEMENT: put away, kinematic; watched, since the game may release it at any time (it streams out with distance).
std::atomic<void*> realActors[2];
struct SwapEnd {bool used;uint32_t proxy,body;V3 placed;float goneAt;};
SwapEnd swapEnds[4];
constexpr float kRealAwayDown=20.f;
int dynamicProved=0;

// ---- Detours.
using SimulateFn=uint64_t(*)(void*,float,void*,void*,uint32_t,bool);
using FetchFn=uint64_t(*)(void*,bool,uint32_t*);
SimulateFn originalSimulate=nullptr;
FetchFn originalFetch=nullptr;
int installState=0;
std::atomic<uint64_t> lastSimulateTick{0};
std::atomic<bool> workerBusy{false},workerBroken{false};
HANDLE workerGo=nullptr,workerDone=nullptr;
float workerCenter[3];

// what was seen, for the log
DWORD simulateThread=0,fetchThread=0;
uint32_t simulates=0,fetchWaits=0,collects=0;
double arrivedSum=0,arrivedMost=0;uint32_t arrivedCount=0;
// HEALTH, a line every 10 s: anything but zeros in the middle four is worth reading.
std::atomic<uint32_t> heldFiltered{0},staleRejected{0},kinematicCleared{0},carryTimeouts{0},releasedWhileHeld{0};
SRWLOCK stopLock=SRWLOCK_INIT;
double stopSum=0,stopMost=0;uint32_t stopCount=0,stopFlips=0;
float stopLast[2][3];
double simulateGapSum=0,sinceAnimSum=0,stepMsSum=0,fetchWaitMs=0,fetchWaitMost=0,workerMsSum=0;
int64_t lastSimulateAt=0;
uint64_t saidAt=0;

bool ProveDynamic(void* actor) {
    if(dynamicProved)return dynamicProved>0;
    const uintptr_t dll=reinterpret_cast<uintptr_t>(GetModuleHandleA("PhysX3_x64.dll"));
    void** table=*reinterpret_cast<void***>(actor);
    bool good=dll && reinterpret_cast<uintptr_t>(table)-dll==kDynamicTableRva;
    if(good)for(const Proved& p:kDynamicSlots)if(reinterpret_cast<uintptr_t>(table[p.slot])-dll!=p.rva) {good=false;break;}
    dynamicProved=good?1:-1;
    if(!good)Log("[physics] a carried actor's table is not the one read: carried things stay on the script's way\n");
    return good;
}

// The turn from one to the other, as a rate, the whole angle: at a wrist flick 2*xyz/dt falls short by a fifth.
V3 Spin(Q from,Q to,float dt) {
    Q d=ph::Mul(to,ph::Conj(from));
    if(d.w<0)d={-d.x,-d.y,-d.z,-d.w};
    const float n=std::sqrt(d.x*d.x+d.y*d.y+d.z*d.z);
    if(n<1e-6f)return V3{};
    const float s=2.f*std::atan2(n,d.w)/(n*dt);
    return {d.x*s,d.y*s,d.z*s};
}

// ---- The animation pass.
void Publish(int side,const Pose& pose) {
    const int64_t now=Now();
    AcquireSRWLockExclusive(&handLock);
    HandSample& h=hands[side];
    const float dt=h.baseAt?float(Seconds(h.baseAt,now)):0.f;
    // OVER AT LEAST MOST OF A FRAME. The pass runs more than once a frame and the player's place changes once: a frame's walk divided by the few ms between two passes was several times the real speed,
    // and the carried body was aimed that far ahead, in the direction of walking (2026-11-08).
    if(!h.valid || dt>=.1f) {
        h.velocity=V3{};h.spin=V3{};h.base=pose;h.baseAt=now;h.valid=true;
    } else if(dt>=.012f) {
        constexpr float kFollow=.6f;
        const V3 v=(pose.p-h.base.p)*(1.f/dt);
        const V3 w=Spin(h.base.q,pose.q,dt);
        h.velocity=h.velocity+(v-h.velocity)*kFollow;
        h.spin=h.spin+(w-h.spin)*kFollow;
        h.period=h.period+(dt-h.period)*.2f;
        h.base=pose;
        h.baseAt=now;
    }
    h.pose=pose;
    h.at=now;
    ReleaseSRWLockExclusive(&handLock);
}
}  // namespace

bool RunsLists() {return Enabled() && !workerBroken && GetTickCount64()-lastSimulateTick.load()<300;}
bool HandsNative() {return Enabled() && CyberpunkVR_PlayerEntityValid;}
bool IsCarried(void* actor) {return actor && (carriedActors[0].load()==actor || carriedActors[1].load()==actor);}

// THE HAND STOPS WHERE THE HELD THING STOPS. What the world did to the body at the last step, for each hand on it: free is where the hand wanted it, solved where it may be. Own lock: written from the step, read in the pose pass.
struct HandStop {Pose free{},solved{};uint64_t at=0;};
HandStop handStops[2];
SRWLOCK handStopLock=SRWLOCK_INIT;
constexpr uint64_t kHandStopFreshMs=100;
constexpr float kHandStopMost=.6f;
void SetHandStop(int side,const Pose& free,const Pose& solved) {
    if(side<0||side>1)return;
    AcquireSRWLockExclusive(&handStopLock);
    handStops[side]={free,solved,GetTickCount64()};
    ReleaseSRWLockExclusive(&handStopLock);
}
void ClearHandStop(int side) {
    if(side<0||side>1)return;
    AcquireSRWLockExclusive(&handStopLock);
    handStops[side].at=0;
    ReleaseSRWLockExclusive(&handStopLock);
}
// The wrist moved rigidly as the world moved the thing. False if there is nothing to do.
bool ApplyHandStop(int side,Pose& wrist) {
    AcquireSRWLockShared(&handStopLock);
    const HandStop s=handStops[side];
    ReleaseSRWLockShared(&handStopLock);
    if(!s.at || GetTickCount64()-s.at>kHandStopFreshMs)return false;
    const float moved=ph::Length(s.solved.p-s.free.p);
    const Q turn=ph::Norm(ph::Mul(s.solved.q,ph::Conj(s.free.q)));
    if(moved>kHandStopMost || (moved<.001f && std::fabs(turn.w)>.99999f))return false;
    wrist.p=s.solved.p+ph::Rot(turn,wrist.p-s.free.p);
    wrist.q=ph::Norm(ph::Mul(turn,wrist.q));
    return true;
}
void AfterHandStop(int side,float* target,float* handRot) {
    if(!Enabled() || !CyberpunkVR_PlayerEntityValid || side<0 || side>1 || !target || !handRot)return;
    const Q entity=ph::Norm({CyberpunkVR_PlayerEntityQuat[0],CyberpunkVR_PlayerEntityQuat[1],CyberpunkVR_PlayerEntityQuat[2],CyberpunkVR_PlayerEntityQuat[3]});
    const V3 entityAt{CyberpunkVR_PlayerEntityPos[0],CyberpunkVR_PlayerEntityPos[1],CyberpunkVR_PlayerEntityPos[2]};
    Pose wrist{ph::Norm(ph::Mul(entity,Q{handRot[0],handRot[1],handRot[2],handRot[3]})),entityAt+ph::Rot(entity,{target[0],target[1],target[2]})};
    if(!std::isfinite(wrist.p.x+wrist.p.y+wrist.p.z+wrist.q.w))return;
    bool changed=false;
    // the hand's own shape, if the script has asked for this hand of late (not while it is glued to something)
    float chest[3];
    if(dv::MoverAsked(side,150) && dv::WorldCenter(chest)) {
        const Q shape=ph::Norm(ph::Mul(wrist.q,handTurnFix[side]));
        const float p[3]={wrist.p.x,wrist.p.y,wrist.p.z},q[4]={shape.x,shape.y,shape.z,shape.w};
        float op[3],oq[4];
        if(dv::MoverSolve(side,p,q,chest,.12f,false,op,oq,nullptr)) {
            wrist.p={op[0],op[1],op[2]};
            changed=true;
        }
    }
    // the gun in it
    GunHold g;
    AcquireSRWLockShared(&gunLock);
    g=gun;
    ReleaseSRWLockShared(&gunLock);
    if(g.have && g.side==side && dv::MoverAsked(2,150)) {
        const Pose tracked{ph::Norm(ph::Mul(wrist.q,g.grip.q)),ph::Apply(wrist,g.grip.p)};
        const float p[3]={tracked.p.x,tracked.p.y,tracked.p.z},q[4]={tracked.q.x,tracked.q.y,tracked.q.z,tracked.q.w};
        const float anchor[3]={wrist.p.x,wrist.p.y,wrist.p.z};
        float op[3],oq[4];
        bool contact=false;
        // ask keeps the mover's clock: MoverSolve has already seen this call
        if(dv::MoverSolve(2,p,q,anchor,0.f,true,op,oq,&contact)) {
            const Q solved{oq[0],oq[1],oq[2],oq[3]};
            const Q wristQ=ph::Norm(ph::Mul(solved,ph::Conj(g.grip.q)));
            const V3 wristP=V3{op[0],op[1],op[2]}-ph::Rot(wristQ,g.grip.p);
            wrist={wristQ,wristP};
            changed=true;
            AcquireSRWLockExclusive(&gunLock);
            gun.touching=contact;
            ReleaseSRWLockExclusive(&gunLock);
        }
    }
    if(changed) {
        const Q back=ph::Conj(entity);
        const V3 m=ph::Rot(back,wrist.p-entityAt);
        const Q mq=ph::Norm(ph::Mul(back,wrist.q));
        if(std::isfinite(m.x+m.y+m.z+mq.w)) {
            target[0]=m.x;target[1]=m.y;target[2]=m.z;
            handRot[0]=mq.x;handRot[1]=mq.y;handRot[2]=mq.z;handRot[3]=mq.w;
        }
    }
    // PUBLISHED BEFORE THE STOP: the tracked hand keeps driving the target, so it comes away from the wall as soon as the controller does.
    Publish(side,wrist);
    if(ApplyHandStop(side,wrist) && std::isfinite(wrist.p.x+wrist.p.y+wrist.p.z+wrist.q.w)) {
        const Q back=ph::Conj(entity);
        const V3 m=ph::Rot(back,wrist.p-entityAt);
        const Q mq=ph::Norm(ph::Mul(back,wrist.q));
        target[0]=m.x;target[1]=m.y;target[2]=m.z;
        handRot[0]=mq.x;handRot[1]=mq.y;handRot[2]=mq.z;handRot[3]=mq.w;
    }
}

namespace {
// ---- Before each step.
struct RecentlyHeld {std::atomic<void*> actor{nullptr};uint32_t proxy=0,body=0;uint64_t at=0;};
RecentlyHeld recent[4];
std::atomic<uint32_t> recentNext{0};
// IS IT STREAMING? The prop's instance keeps a position at +0x30 (three floats; read from a dump 2026-11-08). Read with ReadProcessMemory: the instance may be gone. How far the player is from where it was placed is what streaming would go by, not from where it was taken.
bool ReadPlaced(const uint8_t* instance,V3& at) {
    if(!instance)return false;
    float v[3]{};
    SIZE_T got=0;
    if(!ReadProcessMemory(GetCurrentProcess(),instance+0x30,v,sizeof v,&got) || got!=sizeof v)return false;
    if(!std::isfinite(v[0]+v[1]+v[2]) || std::fabs(v[0])>100000.f || std::fabs(v[1])>100000.f || std::fabs(v[2])>100000.f)return false;
    at={v[0],v[1],v[2]};
    return true;
}
void SayPlacement(const Carry& c,const char* when) {
    if(!c.placedSet)return;
    const V3 player{CyberpunkVR_PlayerEntityPos[0],CyberpunkVR_PlayerEntityPos[1],CyberpunkVR_PlayerEntityPos[2]};
    V3 now{};
    const bool read=ReadPlaced(c.instance,now);
    Log("[physics] placement %u/%u %s: the player %.1f m from where it was placed (%.1f %.1f %.1f); the instance %s (%.1f %.1f %.1f)\n",c.proxy,c.body,when,ph::Length(player-c.placedAt),c.placedAt.x,c.placedAt.y,c.placedAt.z,read?"now says":"could not be read, last",now.x,now.y,now.z);
}
void LetGoNow(Carry& c,bool throwIt) {
    SayPlacement(c,"let go");
    ClearHandStop(c.side);
    ClearHandStop(c.otherSide);
    Log("[physics] carry %u/%u let go: held off by more than 20 mm for %d steps, %d of them with no contact;" " landed off its aim mean %.1f most %.1f mm over %d steps; the world held it back most %.0f mm; the driving hand changed %d times\n",c.proxy,c.body,c.offSteps,c.offNoContactSteps,c.landedSteps?c.landedSum/c.landedSteps:0.0,c.landedMost,c.landedSteps,c.blockedMost*1000.f,c.handChanges);
    {
        RecentlyHeld& r=recent[recentNext++%4];
        r.proxy=c.proxy;r.body=c.body;r.at=GetTickCount64();
        r.actor=c.actor;
    }
    if(throwIt) {
        Slot<void(*)(void*,const V3*,bool)>(c.actor,kSetLinear)(c.actor,&c.throwV,true);
        Slot<void(*)(void*,const V3*,bool)>(c.actor,kSetAngular)(c.actor,&c.throwW,true);
    }
    if(c.swapped) {
        const int i=int(&c-carries);
        SwapEnd e{true,c.proxy,c.body,c.placedAt,0.f};
        if(c.realActor) {
            // THE REAL ONE IS STILL THERE: it takes over where the replacement is, going as it goes. The replacement is put away until the script has deleted it.
            PxPose at{};
            Slot<void*(*)(void*,PxPose*)>(c.actor,kGetGlobalPose)(c.actor,&at);
            PxPose away=at;
            away.p.z-=kRealAwayDown;
            Slot<void(*)(void*,const PxPose*,bool)>(c.actor,kSetGlobalPose)(c.actor,&away,true);
            Slot<void(*)(void*,uint32_t,bool)>(c.actor,kSetFlag)(c.actor,1,true);
            Slot<void(*)(void*,uint32_t,bool)>(c.realActor,kSetFlag)(c.realActor,1,false);
            Slot<void(*)(void*,const PxPose*,bool)>(c.realActor,kSetGlobalPose)(c.realActor,&at,true);
            const V3 v=throwIt?c.throwV:V3{},w=throwIt?c.throwW:V3{};
            Slot<void(*)(void*,const V3*,bool)>(c.realActor,kSetLinear)(c.realActor,&v,true);
            Slot<void(*)(void*,const V3*,bool)>(c.realActor,kSetAngular)(c.realActor,&w,true);
            Log("[swap] %u/%u let go: the real one takes over\n",c.proxy,c.body);
        } else {
            e.goneAt=c.realGoneAt>.1f?c.realGoneAt:.1f;
            Log("[swap] %u/%u let go: the real one is gone, the replacement stays (gone at %.1f m from its placement)\n",c.proxy,c.body,e.goneAt);
        }
        realActors[i]=nullptr;
        c.realActor=nullptr;
        for(auto& slot:swapEnds)if(!slot.used) {slot=e;break;}
    }
}
void DriveOne(Carry& c,const float* origin,float dt) {
    HandSample h;
    AcquireSRWLockShared(&handLock);
    h=hands[c.side];
    ReleaseSRWLockShared(&handLock);
    if(!h.valid)return;
    // THE HAND AS SAMPLED, no prediction. One frame on from the sample was for a body that was itself the picture; with the copy on the hand it put the collider ahead along the motion (seen in game 2026-11-09).
    const Pose& sample=h.pose;
    const Pose wrist{ph::Norm(ph::Mul(sample.q,c.handFix.q)),ph::Apply(sample,c.handFix.p)};
    Pose want{ph::Norm(ph::Mul(wrist.q,c.offset.q)),ph::Apply(wrist,c.offset.p)};
    const Pose free=want;     // where it would be with no wall and no weight
    // THE BODY IS WHERE THE HAND IS, every step; only the world holds it back (MoverSolve below). A limit on acceleration for its weight left the collider behind the copy drawn on the hand: 1.6 m/s2 at 17.5 kg, over 2 s to walking speed (2026-11-09). Weight is in the throw.
    c.command=want;
    const float p[3]={want.p.x,want.p.y,want.p.z},q[4]={want.q.x,want.q.y,want.q.z,want.q.w};
    const float anchor[3]={h.pose.p.x,h.pose.p.y,h.pose.p.z};
    float op[3],oq[4];
    dv::MoverTouched(c.moverId);
    Pose solved=want;
    c.blocked=0;
    bool touching=false;
    if(dv::MoverSolve(c.moverId,p,q,anchor,0.f,true,op,oq,&touching)) {
        solved={{oq[0],oq[1],oq[2],oq[3]},{op[0],op[1],op[2]}};
        // Only the world counts here, not the weight: the hand goes to the thing by this, and a 3.7 kg box was pulling the arms about (2026-11-08).
        c.blocked=ph::Length(solved.p-want.p);
        if(c.blocked>c.blockedMost)c.blockedMost=c.blocked;
        // HELD OFF: how long, and whether anything was touching. Off with no contact is the solver's own state keeping it there.
        if(c.blocked>.02f) {
            ++c.offSteps;
            if(!touching)++c.offNoContactSteps;
            if(!c.offSince)c.offSince=GetTickCount64();
            if(c.blocked>c.offRunMost)c.offRunMost=c.blocked;
        } else if(c.offSince) {
            const uint64_t ms=GetTickCount64()-c.offSince;
            if(ms>=1000)Log("[physics] carry %u/%u: held off for %.1f s, most %.0f mm, now free\n",c.proxy,c.body,double(ms)/1000.0,c.offRunMost*1000.f);
            c.offSince=0;
            c.offRunMost=0;
        }
    }
    if(c.handStops) {
        SetHandStop(c.side,free,solved);
        if(c.otherSide!=c.side)SetHandStop(c.otherSide,free,solved);
    }
    // WHAT THE HAND DOES NOT GET: walls and weight together. The mesh is drawn at the hand plus this.
    {
        const V3 back=solved.p-free.p;
        const Q turn=ph::Norm(ph::Mul(solved.q,ph::Conj(free.q)));
        const float dp[3]={back.x,back.y,back.z},dq[4]={turn.x,turn.y,turn.z,turn.w};
        const float fixQ[4]={c.handFix.q.x,c.handFix.q.y,c.handFix.q.z,c.handFix.q.w};
        cvr::heldpicture::Correct(c.proxy,c.body,dp,dq,c.side,fixQ);
    }
    const Q actorQ=ph::Norm(ph::Mul(solved.q,c.fromGame.q));
    const V3 actorP=ph::Apply(solved,c.fromGame.p);
    PxPose target{actorQ,{actorP.x-origin[0],actorP.y-origin[1],actorP.z-origin[2]}};
    if(!std::isfinite(target.p.x+target.p.y+target.p.z+target.q.w))return;
    if(c.swapTo) {
        // ONE STEP: the real one leaves, the replacement arrives where the hand has it. Never both there, never neither.
        const int i=int(&c-carries);
        void* real=c.actor;
        const V3 zero{};
        PxPose here{};
        Slot<void*(*)(void*,PxPose*)>(real,kGetGlobalPose)(real,&here);
        here.p.z-=kRealAwayDown;
        Slot<void(*)(void*,const PxPose*,bool)>(real,kSetGlobalPose)(real,&here,true);
        Slot<void(*)(void*,const V3*,bool)>(real,kSetLinear)(real,&zero,true);
        Slot<void(*)(void*,const V3*,bool)>(real,kSetAngular)(real,&zero,true);
        Slot<void(*)(void*,uint32_t,bool)>(real,kSetFlag)(real,1,true);
        Slot<void(*)(void*,const PxPose*,bool)>(c.swapTo,kSetGlobalPose)(c.swapTo,&target,true);
        Slot<void(*)(void*,const V3*,bool)>(c.swapTo,kSetLinear)(c.swapTo,&zero,true);
        Slot<void(*)(void*,const V3*,bool)>(c.swapTo,kSetAngular)(c.swapTo,&zero,true);
        c.realActor=real;
        realActors[i]=real;
        c.actor=c.swapTo;
        carriedActors[i]=c.swapTo;
        c.swapTo=nullptr;
        c.swapped=true;
        c.aimedSet=false;
        c.spinRaised=false;
        c.kinematicSteps=0;
        Log("[swap] %u/%u: carried by the replacement now\n",c.proxy,c.body);
        return;
    }
    if(c.mode==Velocity) {
        uint8_t flags=0;
        Slot<void*(*)(void*,uint8_t*)>(c.actor,kGetFlags)(c.actor,&flags);
        // THE GAME WAKES IT, through its own command, at the grip; written here as well, the game and PhysX disagreed about the body. Only if the game has not done so after a dozen steps.
        if(flags&1) {
            if(++c.kinematicSteps<12) {c.aimedSet=false;return;}
            Slot<void(*)(void*,uint32_t,bool)>(c.actor,kSetFlag)(c.actor,1,false);
            if(!c.clearedDirect) {
                c.clearedDirect=true;
                ++kinematicCleared;
                Log("[physics] carry %u/%u: the game left it kinematic for %d steps, cleared here\n",c.proxy,c.body,c.kinematicSteps);
            }
        }
        if(!c.spinRaised) {
            c.spinRaised=true;   // PhysX holds a body to 7 rad/s unless told otherwise: a wrist flick is more
            Slot<void(*)(void*,float)>(c.actor,kSetMaxSpin)(c.actor,50.f);
        }
        PxPose now{};
        Slot<void*(*)(void*,PxPose*)>(c.actor,kGetGlobalPose)(c.actor,&now);
        // how near the last step landed to where it was sent, for the log
        if(c.aimedSet) {
            const double miss=ph::Length(now.p-c.aimed.p)*1000.0;
            arrivedSum+=miss;
            ++arrivedCount;
            if(miss>arrivedMost)arrivedMost=miss;
            c.landedSum+=miss;
            ++c.landedSteps;
            if(float(miss)>c.landedMost)c.landedMost=float(miss);
        }
        c.aimed=target;
        c.aimedSet=true;
        // PHYSX TAKES OFF DAMPING AND ADDS GRAVITY BEFORE IT MOVES THE BODY: v' = (v + g dt)(1 - c dt). This is what lands it. Ignored, the body fell short: 0.87 of what it was told was measured on the script's way.
        const float keepV=1.f-Slot<float(*)(void*)>(c.actor,kGetLinearDamping)(c.actor)*dt;
        const float keepW=1.f-Slot<float(*)(void*)>(c.actor,kGetAngularDamping)(c.actor)*dt;
        V3 v=(target.p-now.p)*(1.f/dt);
        if(keepV>.2f)v=v*(1.f/keepV);
        v.z+=9.81f*dt;
        V3 w=Spin(now.q,target.q,dt);
        if(keepW>.2f)w=w*(1.f/keepW);
        constexpr float kMostSpeed=40.f,kMostSpin=50.f;
        const float s=ph::Length(v),t=ph::Length(w);
        if(s>kMostSpeed)v=v*(kMostSpeed/s);
        if(t>kMostSpin)w=w*(kMostSpin/t);
        Slot<void(*)(void*,const V3*,bool)>(c.actor,kSetLinear)(c.actor,&v,true);
        Slot<void(*)(void*,const V3*,bool)>(c.actor,kSetAngular)(c.actor,&w,true);
    }
}
// WHAT TAKES OVER AFTER THE GAME RELEASES A HELD ACTOR? It falls and stays in game, yet nothing is found under its id in the 0.6 s the script keeps asking. Looked for here, before a step, with carryLock held like the script's search.
struct ReleaseWatch {bool used;uint32_t proxy,body;void* old;uint64_t at,nextLook;};
ReleaseWatch releaseWatches[4];
constexpr uint64_t kWatchMs=5000,kWatchEveryMs=100;
void WatchRelease(uint32_t proxy,uint32_t body,void* old) {
    for(auto& w:releaseWatches)if(!w.used) {w={true,proxy,body,old,GetTickCount64(),0};return;}
}
void LookForReplacements() {
    const uint64_t now=GetTickCount64();
    for(auto& w:releaseWatches) {
        if(!w.used || now<w.nextLook)continue;
        w.nextLook=now+kWatchEveryMs;
        float at[3]{};
        void* actor=dv::FindDynamicActor(w.proxy,w.body,at);
        if(actor) {
            PxPose pose{};
            Slot<void*(*)(void*,PxPose*)>(actor,kGetGlobalPose)(actor,&pose);
            Log("[physics] after the release of %u/%u: an actor is back under that id after %llu ms, %s, at (%.2f %.2f %.2f)\n",w.proxy,w.body,(unsigned long long)(now-w.at),actor==w.old?"at the same address":"at a new address",pose.p.x+at[0],pose.p.y+at[1],pose.p.z+at[2]);
            w.used=false;
        } else if(now-w.at>=kWatchMs) {
            Log("[physics] after the release of %u/%u: no actor under that id in %llu ms\n",w.proxy,w.body,(unsigned long long)kWatchMs);
            w.used=false;
        }
    }
}
void LookForReplacementsGuarded() {
    __try {LookForReplacements();}
    __except(EXCEPTION_EXECUTE_HANDLER) {for(auto& w:releaseWatches)w.used=false;}
}
void StepCarriesGuarded(const float* origin,float dt) {
    LookForReplacementsGuarded();
    __try {
        for(int i=0;i<2;++i) {
            Carry& c=carries[i];
            if(!c.used)continue;
            // The script says when it lets go. This is for when it has stopped altogether: a long hitch must not drop what is held.
            const bool stale=GetTickCount64()-c.askedAt>750;
            if(stale && !c.released) {
                ++carryTimeouts;
                Log("[physics] carry %u/%u: the script has not asked for 750 ms: let go\n",c.proxy,c.body);
            }
            if(c.released || stale) {
                LetGoNow(c,c.released && c.throwIt);
                carriedActors[i]=nullptr;
                dv::MoverReset(c.moverId);
                c.used=false;
                continue;
            }
            DriveOne(c,origin,dt);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        for(int i=0;i<2;++i) {carries[i].used=false;carriedActors[i]=nullptr;}
        Log("[physics] an exception driving a carried body: all let go\n");
    }
}
bool OwnedScene(void* scene,const float** origin) {
    __try {
        void* owner=*reinterpret_cast<void**>(static_cast<char*>(scene)+8);
        if(!owner || *reinterpret_cast<void**>(static_cast<char*>(owner)+0x18)!=scene)return false;
        *origin=reinterpret_cast<const float*>(static_cast<char*>(owner)+0x134);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
void Say(float dt) {
    const uint64_t tick=GetTickCount64();
    if(tick-saidAt<10000)return;
    saidAt=tick;
    {
        AcquireSRWLockExclusive(&stopLock);
        const double mean=stopCount?stopSum/stopCount:0.,most=stopMost;
        const uint32_t flips=stopFlips;
        stopSum=stopMost=0;stopCount=stopFlips=0;
        ReleaseSRWLockExclusive(&stopLock);
        Log("[physics] health: capsule ignored held %u; freed shapes ignored %u, kinematic cleared here %u, timeouts %u, released while held %u; script's stop on the arm mean %.3f most %.3f m, reversed %u\n",
            heldFiltered.exchange(0),staleRejected.exchange(0),kinematicCleared.exchange(0),carryTimeouts.exchange(0),releasedWhileHeld.exchange(0),mean,most,flips);
    }
    Log("[physics] simulate: thread %lu, %u calls in 10 s, last dt %.1f ms, gap mean %.1f ms; hand sample's age at the step mean %.1f ms; before the step %.3f ms mean; fetchResults thread %lu, waits %u, %.3f ms total, most %.3f; worker %.3f ms a scan; lists here %d; carried body landed off its aim by mean %.1f most %.1f mm over %u steps\n",
        simulateThread,simulates,dt*1000.f,simulates?simulateGapSum/simulates:0.,simulates?sinceAnimSum/simulates:0.,simulates?stepMsSum/simulates:0.,fetchThread,fetchWaits,fetchWaitMs,fetchWaitMost,simulates?workerMsSum/simulates:0.,RunsLists()?1:0,arrivedCount?arrivedSum/arrivedCount:0.,arrivedMost,arrivedCount);
    arrivedSum=arrivedMost=0;arrivedCount=0;
    simulates=fetchWaits=0;
    simulateGapSum=sinceAnimSum=stepMsSum=fetchWaitMs=fetchWaitMost=workerMsSum=0;
}
// WHO RELEASES WHAT WAS CARRIED. A held prop's actor was found freed in a crash on letting go (2026-11-08, exe+0x3DE106: the game's hit converter asking getGlobalPose of a ray's hit). This says who did it, and a body freed while held is dropped here, so nothing is written to it afterwards.
using ReleaseFn=void(*)(void*);
ReleaseFn originalRelease=nullptr;
void SayRelease(const char* how,uint32_t proxy,uint32_t body,uint64_t ago) {
    void* frames[14]{};
    const USHORT n=RtlCaptureStackBackTrace(2,14,frames,nullptr);
    const uintptr_t exe=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    char text[320];
    int at=0;
    for(USHORT i=0;i<n && at<300;++i)at+=sprintf_s(text+at,sizeof text-at," %llx",(unsigned long long)(reinterpret_cast<uintptr_t>(frames[i])-exe));
    Log("[physics] the actor of %u/%u was released %s (%llu ms after it was let go), thread %lu, from exe+:%s\n",proxy,body,how,(unsigned long long)ago,GetCurrentThreadId(),at?text:" (none)");
}
void ActorRelease(void* actor) {
    if(actor && (realActors[0].load()==actor || realActors[1].load()==actor)) {
        AcquireSRWLockExclusive(&carryLock);
        for(int i=0;i<2;++i)if(carries[i].used && carries[i].realActor==actor) {
            const V3 player{CyberpunkVR_PlayerEntityPos[0],CyberpunkVR_PlayerEntityPos[1],CyberpunkVR_PlayerEntityPos[2]};
            carries[i].realGoneAt=carries[i].placedSet?ph::Length(player-carries[i].placedAt):.1f;
            carries[i].realActor=nullptr;
            realActors[i]=nullptr;
            Log("[swap] %u/%u: the game released the real body, %.1f m from its placement; the carry goes on\n",carries[i].proxy,carries[i].body,carries[i].realGoneAt);
        }
        ReleaseSRWLockExclusive(&carryLock);
    }
    if(carriedActors[0].load()==actor || carriedActors[1].load()==actor) {
        AcquireSRWLockExclusive(&carryLock);
        for(int i=0;i<2;++i)if(carries[i].used && carries[i].actor==actor) {
            SayRelease("WHILE HELD",carries[i].proxy,carries[i].body,0);
            SayPlacement(carries[i],"RELEASED BY THE GAME");
            WatchRelease(carries[i].proxy,carries[i].body,actor);
            {
                // IS IT STREAMING? How far the player and the thing have come from where it was taken.
                const V3 player{CyberpunkVR_PlayerEntityPos[0],CyberpunkVR_PlayerEntityPos[1],CyberpunkVR_PlayerEntityPos[2]};
                Log("[physics] released while held: held %.1f s; the player %.1f m from where it was when it took the thing, the thing was taken %.1f m from there; last sent to (%.1f %.1f %.1f), %.1f m from where it was taken\n",
                    double(GetTickCount64()-carries[i].grabbedTick)/1000.0,ph::Length(player-carries[i].playerAtGrab),ph::Length(carries[i].grabbedAt-carries[i].playerAtGrab),
                    carries[i].command.p.x,carries[i].command.p.y,carries[i].command.p.z,ph::Length(carries[i].command.p-carries[i].grabbedAt));
            }
            ++releasedWhileHeld;
            carriedActors[i]=nullptr;
            dv::MoverReset(carries[i].moverId);
            carries[i].used=false;
            carries[i].actor=nullptr;
        }
        ReleaseSRWLockExclusive(&carryLock);
    } else {
        for(RecentlyHeld& r:recent) {
            if(r.actor.load()!=actor)continue;
            const uint64_t ago=GetTickCount64()-r.at;
            if(ago<5000)SayRelease("soon after",r.proxy,r.body,ago);
            r.actor=nullptr;
        }
    }
    originalRelease(actor);
}
// THE PLAYER CAPSULE'S QUERY FILTER, exe+0xC5A664: preFilter(this, filter data, shape, actor, flags), read in the exe. Two jobs.
//   What is held does not stand in the player's way. This replaces hiding it from all queries (SetIsQueryable), which every crash on letting go was traced back to.
//   A shape that has been freed is ignored: the crash was this function calling through one.
using PreFilterFn=int(*)(void*,const void*,void*,void*,uint32_t*);
PreFilterFn originalPreFilter=nullptr;
uintptr_t physxLow=0,physxHigh=0;
bool InPhysX(void* object) {
    __try {
        const uintptr_t table=*reinterpret_cast<uintptr_t*>(object);
        return table>=physxLow && table<physxHigh;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
int PreFilter(void* self,const void* data,void* shape,void* actor,uint32_t* flags) {
    if(!shape || !InPhysX(shape) || (actor && !InPhysX(actor))) {
        const uint32_t n=++staleRejected;
        if(n<=10)Log("[physics] the capsule's query met a freed shape or actor (shape %p, actor %p): ignored\n",shape,actor);
        return 0;
    }
    if(Enabled() && IsCarried(actor)) {
        ++heldFiltered;
        return 0;
    }
    return originalPreFilter(self,data,shape,actor,flags);
}
uint64_t Simulate(void* scene,float dt,void* task,void* scratch,uint32_t size,bool control) {
    const float* origin=nullptr;
    if(!Enabled() || !OwnedScene(scene,&origin))return originalSimulate(scene,dt,task,scratch,size,control);
    const int64_t begin=Now();
    simulateThread=GetCurrentThreadId();
    ++simulates;
    if(lastSimulateAt)simulateGapSum+=Seconds(lastSimulateAt,begin)*1000.0;
    lastSimulateAt=begin;
    lastSimulateTick=GetTickCount64();
    {
        AcquireSRWLockShared(&handLock);
        const int64_t at=hands[0].at>hands[1].at?hands[0].at:hands[1].at;
        ReleaseSRWLockShared(&handLock);
        if(at)sinceAnimSum+=Seconds(at,begin)*1000.0;
    }
    const float step=dt>.001f?dt:.001f;
    float center[3];
    const bool centered=dv::WorldCenter(center);
    if(centered)dv::RuntimeCollect(center);
    AcquireSRWLockExclusive(&carryLock);
    StepCarriesGuarded(origin,step);
    ReleaseSRWLockExclusive(&carryLock);
    stepMsSum+=Seconds(begin,Now())*1000.0;
    Say(dt);
    const uint64_t out=originalSimulate(scene,dt,task,scratch,size,control);
    // THE WINDOW OPENS once simulate has returned; fetchResults closes it
    if(centered && !workerBroken && workerGo && !workerBusy.load()) {
        workerCenter[0]=center[0];workerCenter[1]=center[1];workerCenter[2]=center[2];
        workerBusy=true;
        ResetEvent(workerDone);
        SetEvent(workerGo);
    }
    return out;
}
uint64_t Fetch(void* scene,bool block,uint32_t* error) {
    if(workerBusy.load()) {
        const float* origin=nullptr;
        if(OwnedScene(scene,&origin)) {
            const int64_t begin=Now();
            fetchThread=GetCurrentThreadId();
            // the worker reads actors: nothing may free them until it has stopped
            const DWORD got=WaitForSingleObject(workerDone,100);
            const double ms=Seconds(begin,Now())*1000.0;
            ++fetchWaits;
            fetchWaitMs+=ms;
            if(ms>fetchWaitMost)fetchWaitMost=ms;
            if(got!=WAIT_OBJECT_0) {
                workerBroken=true;
                Log("[physics] the world worker did not finish in 100 ms: the lists go back to the script's thread\n");
            }
        }
    }
    return originalFetch(scene,block,error);
}
DWORD WINAPI Worker(void*) {
    for(;;) {
        WaitForSingleObject(workerGo,INFINITE);
        const int64_t begin=Now();
        dv::RuntimeScan(workerCenter);
        workerMsSum+=Seconds(begin,Now())*1000.0;
        workerBusy=false;
        SetEvent(workerDone);
    }
}
}  // namespace

void NoteScriptStop(int side,const float* moved) {
    if(side<0 || side>1)return;
    const double size=std::sqrt(double(moved[0])*moved[0]+double(moved[1])*moved[1]+double(moved[2])*moved[2]);
    AcquireSRWLockExclusive(&stopLock);
    stopSum+=size;
    if(size>stopMost)stopMost=size;
    ++stopCount;
    const float* l=stopLast[side];
    const double was=std::sqrt(double(l[0])*l[0]+double(l[1])*l[1]+double(l[2])*l[2]);
    if(size>.02 && was>.02 && (moved[0]*l[0]+moved[1]*l[1]+moved[2]*l[2])<-.5*size*was)++stopFlips;
    stopLast[side][0]=moved[0];stopLast[side][1]=moved[1];stopLast[side][2]=moved[2];
    ReleaseSRWLockExclusive(&stopLock);
}
bool HandSpin(int side,float* worldRate) {
    if(side<0 || side>1)return false;
    AcquireSRWLockShared(&handLock);
    const HandSample& h=hands[side];
    const bool ok=h.valid;
    if(ok) {worldRate[0]=h.spin.x;worldRate[1]=h.spin.y;worldRate[2]=h.spin.z;}
    ReleaseSRWLockShared(&handLock);
    return ok;
}
bool HandTurn(int side,float* worldTurn) {
    if(side<0 || side>1)return false;
    AcquireSRWLockShared(&handLock);
    const HandSample& h=hands[side];
    const bool ok=h.valid;
    if(ok) {worldTurn[0]=h.pose.q.x;worldTurn[1]=h.pose.q.y;worldTurn[2]=h.pose.q.z;worldTurn[3]=h.pose.q.w;}
    ReleaseSRWLockShared(&handLock);
    return ok;
}
void NoteHandTurn(int side,const float* worldTurn) {
    if(side<0 || side>1)return;
    AcquireSRWLockShared(&handLock);
    const HandSample h=hands[side];
    ReleaseSRWLockShared(&handLock);
    if(!h.valid)return;
    // only while the wrist is still: the script's bone is a frame old, and measured during a turn that frame is built into the difference
    if(handTurnFixed[side] && ph::Length(h.spin)>1.f)return;
    const Q fix=ph::Norm(ph::Mul(ph::Conj(h.pose.q),Q{worldTurn[0],worldTurn[1],worldTurn[2],worldTurn[3]}));
    handTurnFix[side]=handTurnFixed[side]?ph::Slerp(handTurnFix[side],fix,.1f):fix;
    handTurnFixed[side]=true;
}
void Tick() {
    if(installState || !Enabled())return;
    const uintptr_t dll=reinterpret_cast<uintptr_t>(GetModuleHandleA("PhysX3_x64.dll"));
    if(!dll)return;
    auto** table=reinterpret_cast<void**>(dll+kSceneTableRva);
    if(reinterpret_cast<uintptr_t>(table[kSceneSimulate])!=dll+kSimulateRva || reinterpret_cast<uintptr_t>(table[kSceneFetchResults])!=dll+kFetchResultsRva) {
        installState=-1;
        Log("[physics] this is not the PhysX the slots were read from: the script's way stays\n");
        return;
    }
    workerGo=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    workerDone=CreateEventW(nullptr,TRUE,TRUE,nullptr);
    HANDLE thread=CreateThread(nullptr,0,&Worker,nullptr,0,nullptr);
    if(thread) {
        SetThreadPriority(thread,THREAD_PRIORITY_BELOW_NORMAL);
        CloseHandle(thread);
    } else workerBroken=true;
    void* sim=reinterpret_cast<void*>(dll+kSimulateRva);
    void* fetch=reinterpret_cast<void*>(dll+kFetchResultsRva);
    // slot 0 of the rigid dynamic's table
    constexpr uintptr_t kDynamicReleaseRva=0x4A800;
    if(reinterpret_cast<uintptr_t>(reinterpret_cast<void**>(dll+kDynamicTableRva)[0])==dll+kDynamicReleaseRva) {
        void* release=reinterpret_cast<void*>(dll+kDynamicReleaseRva);
        if(MH_CreateHook(release,reinterpret_cast<void*>(&ActorRelease),reinterpret_cast<void**>(&originalRelease))!=MH_OK || MH_EnableHook(release)!=MH_OK)Log("[physics] the release watch could not be set\n");
    }
    if(MH_CreateHook(sim,reinterpret_cast<void*>(&Simulate),reinterpret_cast<void**>(&originalSimulate))!=MH_OK ||
       MH_CreateHook(fetch,reinterpret_cast<void*>(&Fetch),reinterpret_cast<void**>(&originalFetch))!=MH_OK ||
       MH_EnableHook(fetch)!=MH_OK || MH_EnableHook(sim)!=MH_OK) {
        installState=-1;
        Log("[physics] the detours could not be set: the script's way stays\n");
        return;
    }
    installState=1;
    Log("[physics] on: simulate and fetchResults are watched\n");
    {
        const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(dll);
        const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(dll+dos->e_lfanew);
        physxLow=dll;
        physxHigh=dll+nt->OptionalHeader.SizeOfImage;
        constexpr uintptr_t kCapsuleFilterRva=0xC5A664;
        const uintptr_t exe=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        void* filter=reinterpret_cast<void*>(exe+kCapsuleFilterRva);
        if(cvr::game_build::Supported() && !std::memcmp(filter,"\x40\x53\x48\x83\xEC\x50\x49\x8B\x00\x48\x8B\xDA",12) &&
           MH_CreateHook(filter,reinterpret_cast<void*>(&PreFilter),reinterpret_cast<void**>(&originalPreFilter))==MH_OK && MH_EnableHook(filter)==MH_OK)
            Log("[physics] the capsule's query filter is watched\n");
        else Log("[physics] the capsule's query filter is NOT watched: what is held can stand in the player's way\n");
    }
}
}  // namespace cvr::physicsruntime

namespace {
namespace ph=cvr::physics;
namespace rt=cvr::physicsruntime;
}
void VRNativePhysicsOn(RED4ext::IScriptable*,RED4ext::CStackFrame* stack,bool* out,int64_t) {
    stack->code++;
    if(out)*out=rt::HandsNative();
}
// VRGunNative(hand, the weapon's place and turn in the world, or w 0 if not read this frame) -> the gun's push this frame, world; w 0 not native, 1 free, 2 touching.
// The grip is measured here against the wrist that animated the weapon, so it holds no time difference.
void VRGunNative(RED4ext::IScriptable*,RED4ext::CStackFrame* stack,RED4ext::Vector4* out,int64_t) {
    using namespace cvr::physicsruntime;
    int32_t side=0;
    RED4ext::Vector4 at{},turn{};
    RED4ext::GetParameter(stack,&side);
    RED4ext::GetParameter(stack,&at);
    RED4ext::GetParameter(stack,&turn);
    stack->code++;
    if(!out)return;
    *out=RED4ext::Vector4{0,0,0,0};
    if(!HandsNative() || side<0 || side>1)return;
    HandSample h;
    AcquireSRWLockShared(&handLock);
    h=hands[side];
    ReleaseSRWLockShared(&handLock);
    // put away and drawn again: the new weapon's grip is measured afresh
    const bool wasAsked=cvr::debugview::MoverAsked(2,300);
    cvr::debugview::MoverTouched(2);
    AcquireSRWLockExclusive(&gunLock);
    if(!wasAsked) {gun.have=false;gun.touching=false;}
    const bool newHand=gun.side!=side || !gun.have;
    if(h.valid && at.W>.5f && !gun.touching) {
        const ph::Q back=ph::Conj(h.pose.q);
        const ph::V3 p=ph::Rot(back,ph::V3{at.X,at.Y,at.Z}-h.pose.p);
        const ph::Q q=ph::Norm(ph::Mul(back,ph::Q{turn.X,turn.Y,turn.Z,turn.W}));
        if(ph::Length(p)<.5f && std::isfinite(p.x+p.y+p.z+q.w)) {
            if(newHand) {gun.grip={q,p};gun.have=true;gun.side=side;}
            else {
                constexpr float kSettle=.15f;
                gun.grip.p=gun.grip.p+(p-gun.grip.p)*kSettle;
                gun.grip.q=ph::Slerp(gun.grip.q,q,kSettle);
            }
        }
    }
    const bool ready=gun.have && gun.side==side,touching=gun.touching;
    ReleaseSRWLockExclusive(&gunLock);
    if(!ready)return;
    float push[3];
    cvr::debugview::MoverPush(2,push);
    *out=RED4ext::Vector4{push[0],push[1],push[2],touching?2.f:1.f};
}
// VRCarryHold(proxy, body, hand, where the body is in the hand's frame, its turn there, how hard the hands can pull m/s/s, where the game has the body, its turn) -> x how far the world holds it from the hand, w 0 not native, 1 kinematic, 2 by velocity.
void VRCarryHold(RED4ext::IScriptable*,RED4ext::CStackFrame* stack,RED4ext::Vector4* out,int64_t) {
    using namespace cvr::physicsruntime;
    int32_t proxy=0,body=0,side=0;
    RED4ext::Vector4 offset{},turn{},pos{},rot{},handAt{},handTurn{};
    float accel=0;
    int32_t bone=-1;
    RED4ext::GetParameter(stack,&proxy);
    RED4ext::GetParameter(stack,&body);
    RED4ext::GetParameter(stack,&side);
    RED4ext::GetParameter(stack,&offset);
    RED4ext::GetParameter(stack,&turn);
    RED4ext::GetParameter(stack,&accel);
    RED4ext::GetParameter(stack,&pos);
    RED4ext::GetParameter(stack,&rot);
    RED4ext::GetParameter(stack,&handAt);
    RED4ext::GetParameter(stack,&handTurn);
    RED4ext::GetParameter(stack,&bone);
    stack->code++;
    if(!out)return;
    *out=RED4ext::Vector4{0,0,0,0};
    if(!RunsLists() || !HandsNative() || proxy<=0 || body<0 || side<0 || side>1)return;
    AcquireSRWLockExclusive(&carryLock);
    Carry* c=nullptr;
    int free=-1;
    for(int i=0;i<2;++i) {
        if(carries[i].used && !carries[i].released && carries[i].proxy==uint32_t(proxy) && carries[i].body==uint32_t(body))c=&carries[i];
        else if(!carries[i].used && free<0)free=i;
    }
    if(!c && free>=0) {
        float origin[3];
        void* actor=cvr::debugview::FindDynamicActor(uint32_t(proxy),uint32_t(body),origin);
        const int moverId=3+free;
        int shapes=0;
        if(actor && ProveDynamic(actor) && *reinterpret_cast<void***>(actor)==reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(GetModuleHandleA("PhysX3_x64.dll"))+kDynamicTableRva) && (shapes=cvr::debugview::ActorParts(actor,moverId))>0) {
            Carry& n=carries[free];
            n=Carry{};
            n.used=true;n.proxy=uint32_t(proxy);n.body=uint32_t(body);n.actor=actor;n.moverId=moverId;
            n.mode=Velocity;
            PxPose now{};
            Slot<void*(*)(void*,PxPose*)>(actor,kGetGlobalPose)(actor,&now);
            const ph::V3 world{now.p.x+origin[0],now.p.y+origin[1],now.p.z+origin[2]};
            const ph::Q gq=ph::Norm(ph::Q{rot.X,rot.Y,rot.Z,rot.W});
            n.fromGame={ph::Norm(ph::Mul(ph::Conj(gq),now.q)),ph::Rot(ph::Conj(gq),world-ph::V3{pos.X,pos.Y,pos.Z})};
            carriedActors[free]=actor;
            n.grabbedAt={pos.X,pos.Y,pos.Z};
            n.playerAtGrab={CyberpunkVR_PlayerEntityPos[0],CyberpunkVR_PlayerEntityPos[1],CyberpunkVR_PlayerEntityPos[2]};
            n.grabbedTick=GetTickCount64();
            n.instance=cvr::heldpicture::Instance(uint32_t(proxy));
            n.placedSet=ReadPlaced(n.instance,n.placedAt);
            SayPlacement(n,"taken");
            cvr::debugview::MoverTouched(moverId);
            Log("[physics] carry %d/%d: native, %d shapes; the actor is %.3f m from what the game calls the body\n",proxy,body,shapes,ph::Length(n.fromGame.p));
            c=&n;
        }
    }
    if(c) {
        c->askedAt=GetTickCount64();
        // THE OTHER HAND HAS IT NOW: the frame difference was measured for the hand that let go. Kept, the target jumped by the difference between the hands and the body was thrown at it.
        if(c->side!=side) {
            c->side=side;
            ++c->handChanges;
            c->handFixSet=false;
            c->handFixSamples=0;
        }
        c->offset={ph::Norm(ph::Q{turn.X,turn.Y,turn.Z,turn.W}),ph::V3{offset.X,offset.Y,offset.Z}};
        // MEASURED AT THE GRIP, while the hand is still, then frozen. Kept running, the script's bone being a frame old put a frame of every wrist turn into it: the object trailed the wrist (seen in game 2026-11-08).
        AcquireSRWLockShared(&handLock);
        const HandSample h=hands[side];
        ReleaseSRWLockShared(&handLock);
        if(h.valid) {
            const ph::Q back=ph::Conj(h.pose.q);
            const ph::Pose fix{ph::Norm(ph::Mul(back,ph::Q{handTurn.X,handTurn.Y,handTurn.Z,handTurn.W})),ph::Rot(back,ph::V3{handAt.X,handAt.Y,handAt.Z}-h.pose.p)};
            if(ph::Length(fix.p)<.3f) {
                if(!c->handFixSet) {c->handFix=fix;c->handFixSet=true;}
                else if(c->handFixSamples<30 && ph::Length(h.spin)<1.f && ph::Length(h.velocity)<.5f) {
                    ++c->handFixSamples;
                    c->handFix.p=c->handFix.p+(fix.p-c->handFix.p)*.2f;
                    c->handFix.q=ph::Slerp(c->handFix.q,fix.q,.2f);
                }
            }
        }
        // the mesh on the hand, where this kind has the records for it
        const float vo[3]={offset.X,offset.Y,offset.Z},vt[4]={turn.X,turn.Y,turn.Z,turn.W};
        const bool drawn=bone>=0 && cvr::heldpicture::Hold(uint32_t(proxy),uint32_t(body),bone,vo,vt,false,1000.f);
        // gone over to a replacement, the copy is still on the hand whether the real one's records are there or not
        *out=RED4ext::Vector4{c->blocked,(drawn||c->swapped)?1.f:0.f,c->swapped?1.f:0.f,float(c->mode)};
    }
    ReleaseSRWLockExclusive(&carryLock);
}
// VRCarryLetGo(proxy, body, how fast it leaves, how fast it turns, whether to throw): at the next step it is a free body again.
// VRCarryHands(proxy, body, the other hand on it or -1, whether the hands stop with it): every tick, after VRCarryHold.
// VRCarrySwap(proxy, body, the replacement's body, its position, its turn): the carry goes over to the replacement at the next step, and the real body is put away. The carry keeps the real one's ids: the script's and the picture's keys.
void VRCarrySwap(RED4ext::IScriptable*,RED4ext::CStackFrame* stack,bool* out,int64_t) {
    using namespace cvr::physicsruntime;
    int32_t proxy=0,body=0;
    RED4ext::Handle<RED4ext::IScriptable> next;
    RED4ext::Vector4 pos{},rot{};
    RED4ext::GetParameter(stack,&proxy);
    RED4ext::GetParameter(stack,&body);
    RED4ext::GetParameter(stack,&next);
    RED4ext::GetParameter(stack,&pos);
    RED4ext::GetParameter(stack,&rot);
    stack->code++;
    if(out)*out=false;
    if(!next.instance)return;
    const auto* raw=reinterpret_cast<const uint8_t*>(next.instance);
    const uint32_t newProxy=*reinterpret_cast<const uint32_t*>(raw+0x40),newBody=*reinterpret_cast<const uint32_t*>(raw+0x44);
    AcquireSRWLockExclusive(&carryLock);
    for(auto& c:carries)if(c.used && !c.released && !c.swapped && !c.swapTo && c.proxy==uint32_t(proxy) && c.body==uint32_t(body)) {
        float origin[3];
        void* actor=cvr::debugview::FindDynamicActor(newProxy,newBody,origin);
        if(!actor || !ProveDynamic(actor) || *reinterpret_cast<void***>(actor)!=reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(GetModuleHandleA("PhysX3_x64.dll"))+kDynamicTableRva)) {
            Log("[swap] %d/%d: no dynamic actor for the replacement %u/%u\n",proxy,body,newProxy,newBody);
            break;
        }
        if(cvr::debugview::ActorParts(actor,c.moverId)<=0) {
            Log("[swap] %d/%d: the replacement has no shapes the solver can use\n",proxy,body);
            cvr::debugview::ActorParts(c.actor,c.moverId);
            break;
        }
        PxPose now{};
        Slot<void*(*)(void*,PxPose*)>(actor,kGetGlobalPose)(actor,&now);
        const ph::V3 world{now.p.x+origin[0],now.p.y+origin[1],now.p.z+origin[2]};
        const ph::Q gq=ph::Norm(ph::Q{rot.X,rot.Y,rot.Z,rot.W});
        c.fromGame={ph::Norm(ph::Mul(ph::Conj(gq),now.q)),ph::Rot(ph::Conj(gq),world-ph::V3{pos.X,pos.Y,pos.Z})};
        c.swapTo=actor;
        Log("[swap] %d/%d: going over to the replacement %u/%u at the next step, %.3f m from what the game calls its body\n",proxy,body,newProxy,newBody,ph::Length(c.fromGame.p));
        if(out)*out=true;
        break;
    }
    ReleaseSRWLockExclusive(&carryLock);
}
// VRCarrySwapEnd(proxy, body) -> where the real one was placed, and w: -1 not ended yet, 0 the real one took over (delete the replacement), above 0 the real one was gone, at this distance from its placement (the replacement stays).
void VRCarrySwapEnd(RED4ext::IScriptable*,RED4ext::CStackFrame* stack,RED4ext::Vector4* out,int64_t) {
    using namespace cvr::physicsruntime;
    int32_t proxy=0,body=0;
    RED4ext::GetParameter(stack,&proxy);
    RED4ext::GetParameter(stack,&body);
    stack->code++;
    if(!out)return;
    *out=RED4ext::Vector4{0,0,0,-1.f};
    AcquireSRWLockExclusive(&carryLock);
    for(auto& e:swapEnds)if(e.used && e.proxy==uint32_t(proxy) && e.body==uint32_t(body)) {
        *out=RED4ext::Vector4{e.placed.x,e.placed.y,e.placed.z,e.goneAt};
        e.used=false;
        break;
    }
    ReleaseSRWLockExclusive(&carryLock);
}
// VRBodyIds(body interface) -> proxy, body, 0, 1
void VRBodyIds(RED4ext::IScriptable*,RED4ext::CStackFrame* stack,RED4ext::Vector4* out,int64_t) {
    RED4ext::Handle<RED4ext::IScriptable> h;
    RED4ext::GetParameter(stack,&h);
    stack->code++;
    if(!out)return;
    *out=RED4ext::Vector4{0,0,0,0};
    if(!h.instance)return;
    const auto* raw=reinterpret_cast<const uint8_t*>(h.instance);
    *out=RED4ext::Vector4{float(*reinterpret_cast<const uint32_t*>(raw+0x40)),float(*reinterpret_cast<const uint32_t*>(raw+0x44)),0,1.f};
}
void VRCarryHands(RED4ext::IScriptable*,RED4ext::CStackFrame* stack,bool* out,int64_t) {
    using namespace cvr::physicsruntime;
    int32_t proxy=0,body=0,other=-1;
    bool stops=false;
    RED4ext::GetParameter(stack,&proxy);
    RED4ext::GetParameter(stack,&body);
    RED4ext::GetParameter(stack,&other);
    RED4ext::GetParameter(stack,&stops);
    stack->code++;
    if(out)*out=false;
    AcquireSRWLockExclusive(&carryLock);
    for(auto& c:carries)if(c.used && !c.released && c.proxy==uint32_t(proxy) && c.body==uint32_t(body)) {
        const int was=c.otherSide;
        c.otherSide=other>=0&&other<=1?other:-1;
        c.handStops=stops;
        if(was!=c.otherSide && was!=c.side)ClearHandStop(was);
        if(out)*out=true;
    }
    ReleaseSRWLockExclusive(&carryLock);
}
void VRCarryLetGo(RED4ext::IScriptable*,RED4ext::CStackFrame* stack,bool* out,int64_t) {
    using namespace cvr::physicsruntime;
    int32_t proxy=0,body=0;
    RED4ext::Vector4 v{},w{};
    bool throwIt=false;
    RED4ext::GetParameter(stack,&proxy);
    RED4ext::GetParameter(stack,&body);
    RED4ext::GetParameter(stack,&v);
    RED4ext::GetParameter(stack,&w);
    RED4ext::GetParameter(stack,&throwIt);
    stack->code++;
    bool found=false;
    AcquireSRWLockExclusive(&carryLock);
    for(auto& c:carries)if(c.used && c.proxy==uint32_t(proxy) && c.body==uint32_t(body)) {
        c.released=true;
        c.throwIt=throwIt;
        c.throwV={v.X,v.Y,v.Z};
        c.throwW={w.X,w.Y,w.Z};
        found=true;
    }
    ReleaseSRWLockExclusive(&carryLock);
    if(found)cvr::heldpicture::LetGo(uint32_t(proxy),uint32_t(body));
    if(out)*out=found;
}
