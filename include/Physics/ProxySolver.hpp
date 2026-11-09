#pragma once
// ProxySolver -- a body held by the hand that the world stops: the gun, a hand, a carried thing.
//
// A PROXY POSE chases the tracked pose, as in haptic rendering ("god object"), and what is shown is the proxy. Every move of it, turning as well as
// going, is checked against the world, and when something is in the way the answer may turn the body as well as move it.
//
// WHY NOT ONLY SWEEPS. PhysX sweeps a shape along a line and cannot sweep a turn. The old mover turned the gun first and swept only the wrist's travel, so a wrist swing with a rifle was
// invisible to it: 3.8 cm of muzzle a frame at 200 deg/s, with no travel to sweep (docs/hand-physics-lag-weaponstop-threading-20261108.md). Here a turn is cut into steps short enough
// that the far end of the body cannot jump across a surface in one of them, and each step is tested.
//
// THE ANSWER IS A POSITION-BASED RIGID CORRECTION: for a contact at arm r with normal n, a push is shared between going and turning by 1 : |r x n|^2 / k^2, k being a stiffness length.
// A contact at the muzzle is mostly answered by turning, one by the grip mostly by going. The wrist therefore stays put while a barrel swings against a wall.
//
// No game or PhysX type appears here: the world is the IWorld below, so tools and tests can run the same code (tests/physics).
#include <cmath>

namespace cvr::physics {

struct V3 {float x=0,y=0,z=0;};
struct Q {float x=0,y=0,z=0,w=1;};
struct Pose {Q q{};V3 p{};};

inline V3 operator+(V3 a,V3 b) {return {a.x+b.x,a.y+b.y,a.z+b.z};}
inline V3 operator-(V3 a,V3 b) {return {a.x-b.x,a.y-b.y,a.z-b.z};}
inline V3 operator*(V3 a,float s) {return {a.x*s,a.y*s,a.z*s};}
inline float Dot(V3 a,V3 b) {return a.x*b.x+a.y*b.y+a.z*b.z;}
inline V3 Cross(V3 a,V3 b) {return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
inline float Length(V3 a) {return std::sqrt(Dot(a,a));}
inline Q Mul(Q a,Q b) {
    return {a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z};
}
inline Q Conj(Q q) {return {-q.x,-q.y,-q.z,q.w};}
inline Q Norm(Q q) {
    const float n=std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);
    return n>1e-6f?Q{q.x/n,q.y/n,q.z/n,q.w/n}:Q{};
}
inline V3 Rot(Q q,V3 v) {
    const V3 u{q.x,q.y,q.z};
    const V3 t=Cross(u,v)*2.f;
    return v+t*q.w+Cross(u,t);
}
inline V3 Apply(const Pose& a,V3 v) {return a.p+Rot(a.q,v);}
// The angle between two turns, 0..pi.
inline float Angle(Q a,Q b) {
    const float d=std::fabs(a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w);
    return 2.f*std::acos(d>1.f?1.f:d);
}
// Short way round, at a constant rate.
inline Q Slerp(Q a,Q b,float t) {
    float d=a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w;
    if(d<0) {b={-b.x,-b.y,-b.z,-b.w};d=-d;}
    if(d>.9995f)return Norm({a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t,a.w+(b.w-a.w)*t});
    const float th=std::acos(d),s=std::sin(th),u=std::sin((1-t)*th)/s,v=std::sin(t*th)/s;
    return Norm({a.x*u+b.x*v,a.y*u+b.y*v,a.z*u+b.z*v,a.w*u+b.w*v});
}

// In the body's frame. A box, or a capsule along its local X.
struct Part {V3 center{},half{};Q q{};bool capsule=false;float radius=0,halfHeight=0;};
struct Hit {float distance=0;V3 normal{},point{};bool pointExact=false,moving=false;};

class IWorld {
public:
    virtual ~IWorld()=default;
    // The first thing a part meets going `distance` along unit `dir`; `pose` is the part's own, in the world.
    virtual bool Sweep(const Part& part,const Pose& pose,const V3& dir,float distance,Hit& hit)=0;
    // The deepest overlap of a part: the unit way out, and how far.
    virtual bool Penetration(const Part& part,const Pose& pose,V3& out,float& depth)=0;
    // The first surface on a line that lies at least `skip` along it.
    virtual bool Line(const V3& from,const V3& dir,float length,float skip,Hit& hit)=0;
};

struct Options {
    float skin=.002f;        // m kept between the body and what it is stopped by, along the surface's normal
    float arcMost=.012f;     // m the far end of the body may move in one step of a turn
    int mostSteps=32;
    bool turnAnswer=true;    // contacts may turn the body. Off for a hand: its turn is the controller's.
    bool substeps=true;      // off: the old mover's behaviour, kept for comparing
    float stiffness=.12f;    // m. Larger: more of an answer is going, less is turning.
    float mostTurn=1.2f;     // rad a contact may turn it off the tracked turn
    float giveUp=1.f;        // m from the tracked place at which the body comes through whatever holds it
    int projectPasses=4;
};
struct State {Pose pose{};bool on=false;};
struct Result {
    Pose pose{};
    bool contact=false,tethered=false,gaveUp=false;
    Hit lastHit{};
    int steps=0;
};

namespace detail {
inline Pose PartPose(const Pose& body,const Part& p) {return {Mul(body.q,p.q),Apply(body,p.center)};}
inline float Clamp1(float v) {return v<-1.f?-1.f:v>1.f?1.f:v;}
// The part's point deepest against normal n. Soft near a face seen edge-on, so that the point does not jump from one end of a barrel lying flat on a wall to the other.
inline V3 Deepest(const Part& part,const Pose& pose,V3 n) {
    constexpr float kSoft=.15f;
    if(part.capsule) {
        const V3 ax=Rot(pose.q,{1,0,0});
        return pose.p+ax*(-Clamp1(Dot(ax,n)/kSoft)*part.halfHeight)-n*part.radius;
    }
    const V3 ax[3]={Rot(pose.q,{1,0,0}),Rot(pose.q,{0,1,0}),Rot(pose.q,{0,0,1})};
    const float h[3]={part.half.x,part.half.y,part.half.z};
    V3 at=pose.p;
    for(int i=0;i<3;++i)at=at+ax[i]*(-Clamp1(Dot(ax[i],n)/kSoft)*h[i]);
    return at;
}
inline float Reach(const Part& p) {
    const float own=p.capsule?p.halfHeight+p.radius:Length(p.half);
    return Length(p.center)+own;
}
inline float Thinnest(const Part& p) {
    if(p.capsule)return p.radius;
    float t=p.half.x;
    if(p.half.y<t)t=p.half.y;
    if(p.half.z<t)t=p.half.z;
    return t;
}
// Moves the body by `depth` along n at `point`, shared between going and turning.
inline void Correct(Pose& body,V3 point,V3 n,float depth,bool turn,float k) {
    const V3 r=point-body.p;
    const V3 rn=Cross(r,n);
    const float inv=1.f/(k*k);
    const float w=turn?Dot(rn,rn)*inv:0.f;
    const float l=depth/(1.f+w);
    body.p=body.p+n*l;
    if(!turn)return;
    V3 th=rn*(l*inv);
    const float a=Length(th);
    constexpr float kMostAtOnce=.35f;
    if(a>kMostAtOnce)th=th*(kMostAtOnce/a);
    const Q d=Norm({th.x*.5f,th.y*.5f,th.z*.5f,1.f});
    body.q=Norm(Mul(d,body.q));
}
// Out of whatever it overlaps. True if it was in something.
inline bool Project(IWorld& w,const Part* parts,int count,Pose& body,const Q& tracked,const Options& o,bool mayTurn,Result& r) {
    bool any=false;
    for(int pass=0;pass<o.projectPasses;++pass) {
        bool moved=false;
        for(int b=0;b<count;++b) {
            const Pose pose=PartPose(body,parts[b]);
            V3 out{};
            float depth=0;
            if(!w.Penetration(parts[b],pose,out,depth) || !(depth>0.f))continue;
            const bool turn=mayTurn && Angle(body.q,tracked)<o.mostTurn;
            Correct(body,Deepest(parts[b],pose,out),out,depth+o.skin,turn,o.stiffness);
            moved=any=true;
            r.contact=true;
        }
        if(!moved)break;
    }
    return any;
}
// Goes from body.p towards `to`, stopped at what is in the way and slid along it. Each surface met stays a constraint of the rest of the move, so that a corner cannot push it back into the first wall.
inline void Go(IWorld& w,const Part* parts,int count,Pose& body,V3 to,const Options& o,Result& r) {
    V3 planes[3];
    int nPlanes=0;
    V3 left=to-body.p;
    const V3 wanted=left;
    for(int slide=0;slide<4;++slide) {
        const float dist=Length(left);
        if(dist<1e-4f)return;
        const V3 dir=left*(1.f/dist);
        float best=dist;
        Hit nearest{};
        bool hit=false;
        for(int b=0;b<count;++b) {
            const Pose pose=PartPose(body,parts[b]);
            Hit h{};
            if(!w.Sweep(parts[b],pose,dir,best,h) || !(h.distance<=best))continue;
            if(Dot(dir,h.normal)>0)h.normal=h.normal*-1.f;
            best=h.distance;
            nearest=h;
            hit=true;
        }
        if(!hit) {
            body.p=body.p+left;
            return;
        }
        r.contact=true;
        r.lastHit=nearest;
        // the gap is kept along the normal: taken along the move, a grazing slide kept almost none of it
        const float into=-Dot(dir,nearest.normal);
        const float slack=o.skin/(into>.05f?into:.05f);
        const float go=best>slack?best-slack:0.f;
        body.p=body.p+dir*go;
        left=left-dir*go;
        // arrived closer than the skin (or exactly touching, from a move that ended there): the gap is made
        {
            const float gapNow=(best-go)*into;
            if(gapNow<o.skin)body.p=body.p+nearest.normal*(o.skin-gapNow);
        }
        if(nPlanes<3)planes[nPlanes++]=nearest.normal;
        if(nPlanes==1) {
            const float d=Dot(left,planes[0]);
            if(d<0)left=left-planes[0]*d;
        } else if(nPlanes==2) {
            V3 crease=Cross(planes[0],planes[1]);
            const float n=Length(crease);
            if(n<1e-3f) {
                const float d=Dot(left,planes[1]);
                if(d<0)left=left-planes[1]*d;
            } else {
                crease=crease*(1.f/n);
                left=crease*Dot(left,crease);
            }
        } else return;
        if(Dot(left,wanted)<=0)return;
    }
}
// Nothing may lie between what holds the body and the body: it has got behind a surface. It comes back across that surface, along its normal.
inline void Tether(IWorld& w,const Part* parts,int count,Pose& body,const Q& tracked,V3 anchor,float skip,const Options& o,bool mayTurn,Result& r) {
    constexpr float kBack=.012f;
    constexpr int kMostPoints=24;
    for(int b=0;b<count && b*2<kMostPoints;++b) {
        const Pose pose=PartPose(body,parts[b]);
        const V3 ends[2]={pose.p,Deepest(parts[b],pose,(anchor-pose.p)*(1.f/(Length(anchor-pose.p)+1e-5f)))};
        for(const V3& at:ends) {
            const V3 way=at-anchor;
            const float length=Length(way);
            if(length<.03f)continue;
            const V3 dir=way*(1.f/length);
            Hit h{};
            if(!w.Line(anchor,dir,length,skip,h) || h.distance>=length)continue;
            V3 n=h.normal;
            if(Dot(n,dir)>0)n=n*-1.f;
            const float depth=(length-h.distance)*-Dot(dir,n)+kBack;
            Correct(body,at,n,depth,mayTurn && Angle(body.q,tracked)<o.mostTurn,o.stiffness);
            r.tethered=true;
            r.contact=true;
            r.lastHit=h;
        }
    }
}
}  // namespace detail

// One move: from where it was left to the tracked pose, as far as the world lets it. `anchor` is what holds it, null for nothing.
inline Result Solve(IWorld& w,const Part* parts,int count,State& s,const Pose& tracked,const V3* anchor,float anchorSkip,const Options& o) {
    using namespace detail;
    Result r{};
    if(count<=0) {
        s.pose=tracked;s.on=true;
        r.pose=tracked;
        return r;
    }
    if(!s.on) {
        Pose body=tracked;
        Project(w,parts,count,body,tracked.q,o,false,r);
        if(anchor)Tether(w,parts,count,body,tracked.q,*anchor,anchorSkip,o,false,r);
        s.pose=body;s.on=true;
        r.pose=body;
        return r;
    }
    Pose body=s.pose;
    // What moved into it since last time (a door, a lift, the near list refilled), turn as it stands.
    Project(w,parts,count,body,tracked.q,o,o.turnAnswer,r);
    int steps=1;
    float mostStep=1e3f;     // rad a step may turn
    if(o.substeps) {
        float reach=0,thin=1e3f;
        for(int b=0;b<count;++b) {
            const float a=Reach(parts[b]),t=Thinnest(parts[b]);
            if(a>reach)reach=a;
            if(t<thin)thin=t;
        }
        // a thin blade must not be able to jump a surface inside one step
        float arc=thin*2.f;
        arc=arc>o.arcMost?o.arcMost:arc<.004f?.004f:arc;
        const int need=int(std::ceil(Angle(body.q,tracked.q)*reach/arc));
        steps=need<1?1:need>o.mostSteps?o.mostSteps:need;
        mostStep=Angle(body.q,tracked.q)/float(steps);
        if(mostStep<1e-5f)mostStep=1e-5f;
    }
    r.steps=steps;
    for(int i=0;i<steps;++i) {
        // A STEP TURNS AT MOST ITS SHARE. Aimed at "all that is left", the last step took the whole of what a contact had held back in one jump, straight over a post (tests/physics).
        const float left=Angle(body.q,tracked.q);
        const float t=left>mostStep?mostStep/left:1.f;
        const Q turnTo=Slerp(body.q,tracked.q,t);
        const float share=1.f/float(steps-i);
        const V3 goTo=body.p+(tracked.p-body.p)*share;
        const Pose before=body;
        body.q=turnTo;
        const bool met=Project(w,parts,count,body,tracked.q,o,o.turnAnswer,r);
        // AS FAR AS A CONTACT MAY TURN IT: past this the turn is held where it stands. Answered by going alone, a body turned hard against a thin sheet came out on its far side (tests/physics).
        if(met && o.turnAnswer && Angle(body.q,tracked.q)>=o.mostTurn)body=before;
        Go(w,parts,count,body,goTo,o,r);
    }
    if(Length(body.p-tracked.p)>o.giveUp || Angle(body.q,tracked.q)>o.mostTurn*2.f) {
        r.gaveUp=true;
        body=tracked;
    }
    if(anchor) {
        Tether(w,parts,count,body,tracked.q,*anchor,anchorSkip,o,o.turnAnswer,r);
        if(r.tethered)Project(w,parts,count,body,tracked.q,o,o.turnAnswer,r);
    }
    s.pose=body;
    r.pose=body;
    return r;
}

}  // namespace cvr::physics
