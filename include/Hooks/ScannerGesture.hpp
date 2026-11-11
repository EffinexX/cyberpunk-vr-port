#pragma once
#include <cmath>
#include <cstdint>

namespace cvr::input {
void PublishScannerVision(int state);
int ScannerVision(); // -1 means no recent game-state sample

class ScannerRequest {
    uint64_t started{};
    bool pending{},accepted{};
public:
    void Update(bool& requested,int vision,uint64_t now) {
        if(!requested){pending=accepted=false;started=0;return;}
        if(!pending){pending=true;started=now;accepted=false;}
        if(vision==1)accepted=true;
        // An input request is not proof that the game opened the scanner.
        // Release rejected/stale requests and respect a game-driven exit.
        if((accepted && vision==0) || (vision!=1 && now>=started && now-started>=750)) {
            requested=false;pending=accepted=false;started=0;
        }
    }
};
// Only a squeeze that STARTS at the ear counts. A hand already squeezing -- a shell or a magazine on
// its way from the belt to a gun held close in -- passes within the zone and must not open the scanner.
class ScannerSqueeze {
    bool was{},carried{};
public:
    bool Update(bool gripHeld,bool atEar) {
        if(gripHeld && !was)carried=!atEar;
        if(!gripHeld)carried=false;
        was=gripHeld;
        return gripHeld && !carried;
    }
    void Reset(){was=carried=false;}
};
// The runtime reports the controller's grip centre, below/outside the ear,
// rather than the point where the fingers touch the headset.
struct ScannerEarZone {
    float x=-0.18f, y=-0.08f, z=0.08f;
    float radius=0.22f, margin=0.04f;
    bool Contains(float px,float py,float pz,bool armed) const {
        if(!std::isfinite(px) || !std::isfinite(py) || !std::isfinite(pz) || px>-.04f)return false;
        const float r0=radius>.02f && radius<=.60f ? radius : .22f;
        const float extra=margin>=0 && margin<=.30f ? margin : .04f;
        const float r=r0+(armed?extra:0);
        const float dx=px-x,dy=py-y,dz=pz-z;
        return dx*dx+dy*dy+dz*dz<=r*r;
    }
};
// The flashlight: the trigger of the hand at the ear, the scanner's zone. Only a squeeze begun with the hand already
// there counts, so a trigger held for aiming survives the hand passing the head. The return value says the squeeze
// belongs to this gesture and the game must not see it: from first travel, since the game aims long before .60.
class EarTriggerGesture {
    uint64_t inSince{},outSince{};
    bool armed{},active{},carried{},down{};
public:
    bool Armed() const {return armed;}
    bool Update(float trigger,bool inZone,bool eligible,uint64_t now,uint64_t enterMs,uint64_t exitMs,bool& fired) {
        fired=false;
        if(inZone) {
            outSince=0;
            if(!inSince)inSince=now;
            if(now-inSince>=enterMs)armed=true;
        } else {
            inSince=0;
            if(!outSince)outSince=now;
            if(now-outSince>=exitMs)armed=false;
        }
        if(!active && trigger>.10f){active=true;carried=!armed || !eligible;}
        if(active && !eligible)carried=true;
        if(trigger<=.05f)active=carried=down=false;
        if(active && !carried && !down && trigger>.60f){down=true;fired=true;}
        return active && !carried;
    }
};
}
