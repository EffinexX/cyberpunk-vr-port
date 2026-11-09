// Runs include/Physics/ProxySolver.hpp against the game's own PxGeometryQuery, loaded from PhysX3Common_x64.dll, without the game.
//   physics_test [game bin\x64 folder] [replay file]
// Exit code: number of failed checks.
#include "Physics/ProxySolver.hpp"
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#define _USE_MATH_DEFINES
#include <math.h>

using namespace cvr::physics;

struct Bounds {V3 lo,hi;};
struct BoxG {int type;V3 half;};
struct CapsuleG {int type;float radius,halfHeight;};
struct SphereG {int type;float radius;};
struct Tf {Q q;V3 p;};
using PenetrationFn=bool(*)(V3*,float*,const void*,const void*,const void*,const void*);
using SweepFn=bool(*)(const V3*,float,const void*,const void*,const void*,const void*,void*,const uint16_t*,float);
PenetrationFn penetration=nullptr;
SweepFn sweep=nullptr;
int normalAt=-1;

struct Obstacle {int kind=3;V3 half{};float radius=0,halfHeight=0;Pose pose{};};
struct Geo {alignas(8) uint8_t b[32];};
static void Make(const Part& p,Geo& g) {
    std::memset(g.b,0,sizeof g.b);
    if(p.capsule) {const CapsuleG c{2,p.radius,p.halfHeight};std::memcpy(g.b,&c,sizeof c);}
    else {const BoxG c{3,p.half};std::memcpy(g.b,&c,sizeof c);}
}
static void Make(const Obstacle& o,Geo& g) {
    std::memset(g.b,0,sizeof g.b);
    if(o.kind==3) {const BoxG c{3,o.half};std::memcpy(g.b,&c,sizeof c);}
    else if(o.kind==2) {const CapsuleG c{2,o.radius,o.halfHeight};std::memcpy(g.b,&c,sizeof c);}
    else {const SphereG c{0,o.radius};std::memcpy(g.b,&c,sizeof c);}
}

// TRIANGLES, by the rig's own tests: an exact separating-axis overlap of a box and a triangle, and a sweep that marches. Not PhysX's mesh code (that needs cooking, whose layouts are unproved here);
// what it checks is what the solver does with real surfaces and sheets.
struct Tri {V3 a,b,c;};
static bool BoxTri(const Part& part,const Pose& pose,const Tri& t,V3& way,float& depth) {
    const V3 h=part.capsule?V3{part.halfHeight+part.radius,part.radius,part.radius}:part.half;
    const V3 ax[3]={Rot(pose.q,{1,0,0}),Rot(pose.q,{0,1,0}),Rot(pose.q,{0,0,1})};
    const float hs[3]={h.x,h.y,h.z};
    const V3 v[3]={t.a-pose.p,t.b-pose.p,t.c-pose.p};
    const V3 e[3]={t.b-t.a,t.c-t.b,t.a-t.c};
    V3 axes[13];
    int n=0;
    axes[n++]=Cross(e[0],e[1]);
    for(int i=0;i<3;++i)axes[n++]=ax[i];
    for(int i=0;i<3;++i)for(int k=0;k<3;++k)axes[n++]=Cross(ax[i],e[k]);
    depth=1e9f;
    bool any=false;
    for(int i=0;i<n;++i) {
        const float len=Length(axes[i]);
        if(len<1e-5f)continue;
        const V3 d=axes[i]*(1.f/len);
        const float p0=Dot(v[0],d),p1=Dot(v[1],d),p2=Dot(v[2],d);
        const float lo=p0<p1?(p0<p2?p0:p2):(p1<p2?p1:p2),hi=p0>p1?(p0>p2?p0:p2):(p1>p2?p1:p2);
        const float r=std::fabs(Dot(ax[0],d))*hs[0]+std::fabs(Dot(ax[1],d))*hs[1]+std::fabs(Dot(ax[2],d))*hs[2];
        if(lo>r || hi<-r)return false;
        // push the box to the side of the triangle it needs least to go
        const float toPlus=hi+r,toMinus=r-lo;
        const float o=toPlus<toMinus?toPlus:toMinus;
        if(o<depth) {depth=o;way=toPlus<toMinus?d:d*-1.f;any=true;}
    }
    return any;
}
static bool RayTri(V3 o,V3 d,const Tri& t,float& at,V3& n) {
    const V3 e1=t.b-t.a,e2=t.c-t.a,p=Cross(d,e2);
    const float det=Dot(e1,p);
    if(std::fabs(det)<1e-8f)return false;
    const float inv=1.f/det;
    const V3 s=o-t.a;
    const float u=Dot(s,p)*inv;
    if(u<0 || u>1)return false;
    const V3 q=Cross(s,e1);
    const float v=Dot(d,q)*inv;
    if(v<0 || u+v>1)return false;
    at=Dot(e2,q)*inv;
    n=Cross(e1,e2);
    const float l=Length(n);
    if(l<1e-8f)return false;
    n=n*(1.f/l);
    return at>=0;
}
class World:public IWorld {
public:
    std::vector<Obstacle> list;
    std::vector<Tri> tris;
    bool Sweep(const Part& part,const Pose& pose,const V3& dir,float distance,Hit& out) override {
        constexpr uint16_t kFlags=1|2|4|16|128|256;
        Geo a;Make(part,a);
        const Tf t{pose.q,pose.p};
        bool any=false;
        float best=distance;
        for(const Obstacle& o:list) {
            Geo b;Make(o,b);
            const Tf s{o.pose.q,o.pose.p};
            uint8_t hit[128]{};
            if(!sweep(&dir,best,a.b,&t,b.b,&s,hit,&kFlags,0.f))continue;
            // +40 is the offset the game's own log measured. Scanning from +16 can stop early at +32, where the end of the hit position and the normal's x can pass for a unit vector.
            if(normalAt<0) {
                const float* f=reinterpret_cast<const float*>(hit+40);
                const float n=f[0]*f[0]+f[1]*f[1]+f[2]*f[2];
                if(n>.99f && n<1.01f && f[3]>=0 && f[3]<=best+1e-3f)normalAt=40;
            }
            if(normalAt<0)continue;
            const float* f=reinterpret_cast<const float*>(hit+normalAt);
            if(f[3]<=best) {
                best=f[3];
                out.distance=f[3];
                out.normal={f[0],f[1],f[2]};
                any=true;
            }
        }
        if(!tris.empty()) {
            // march: first free of what it starts in (PhysX's assume-no-initial-overlap), then fine steps to the first touch
            const float stepOf=.002f;
            std::vector<char> ignored(tris.size(),0);
            V3 w{};float d=0;
            for(size_t i=0;i<tris.size();++i)if(BoxTri(part,pose,tris[i],w,d))ignored[i]=1;
            for(float t=stepOf;t<best;t+=stepOf) {
                const Pose at{pose.q,pose.p+dir*t};
                for(size_t i=0;i<tris.size();++i) {
                    if(ignored[i] || !BoxTri(part,at,tris[i],w,d))continue;
                    V3 n=Cross(tris[i].b-tris[i].a,tris[i].c-tris[i].a);
                    n=n*(1.f/Length(n));
                    if(Dot(n,dir)>0)n=n*-1.f;
                    out.distance=t-stepOf>0?t-stepOf:0;
                    out.normal=n;
                    return true;
                }
            }
        }
        return any;
    }
    bool Penetration(const Part& part,const Pose& pose,V3& way,float& depth) override {
        Geo a;Make(part,a);
        const Tf t{pose.q,pose.p};
        bool any=false;
        depth=0;
        for(const Obstacle& o:list) {
            Geo b;Make(o,b);
            const Tf s{o.pose.q,o.pose.p};
            V3 d{};
            float e=0;
            if(penetration(&d,&e,a.b,&t,b.b,&s) && e>depth) {depth=e;way=d;any=true;}
        }
        for(const ::Tri& tr:tris) {
            V3 d{};float e=0;
            if(BoxTri(part,pose,tr,d,e) && e>depth) {depth=e;way=d;any=true;}
        }
        return any;
    }
    bool Line(const V3& from,const V3& dir,float length,float skip,Hit& out) override {
        constexpr uint16_t kFlags=1|2|4|16|128|256;
        const BoxG thin{3,{.003f,.003f,.003f}};
        const Tf t{Q{},from};
        bool any=false;
        float best=length;
        for(const Obstacle& o:list) {
            Geo b;Make(o,b);
            const Tf s{o.pose.q,o.pose.p};
            uint8_t hit[128]{};
            if(!sweep(&dir,best,&thin,&t,b.b,&s,hit,&kFlags,0.f) || normalAt<0)continue;
            const float* f=reinterpret_cast<const float*>(hit+normalAt);
            if(f[3]>=skip && f[3]<=best) {best=f[3];out.distance=f[3];out.normal={f[0],f[1],f[2]};any=true;}
        }
        for(const ::Tri& tr:tris) {
            float at=0;V3 n{};
            if(RayTri(from,dir,tr,at,n) && at>=skip && at<=best) {best=at;out.distance=at;out.normal=n;any=true;}
        }
        return any;
    }
    // How far any part is inside anything, for the checks.
    float Worst(const Part* parts,int n,const Pose& body) {
        float worst=0;
        for(int i=0;i<n;++i) {
            V3 w{};float d=0;
            if(Penetration(parts[i],{Mul(body.q,parts[i].q),Apply(body,parts[i].center)},w,d) && d>worst)worst=d;
        }
        return worst;
    }
};

static Q AboutZ(float a) {return {0,0,std::sin(a*.5f),std::cos(a*.5f)};}
static int failed=0;
static void Check(const char* what,bool ok,const char* fmt,double a,double b=0) {
    printf("  %-4s %-58s ",ok?"ok":"FAIL",what);
    printf(fmt,a,b);
    printf("\n");
    if(!ok)++failed;
}
// A rifle: origin at the grip, barrel along +Y, 70 cm long.
static const Part kRifle{{0,.35f,0},{.02f,.35f,.04f}};
static Obstacle Wall(float x,float half=.1f) {Obstacle o;o.half={half,5,5};o.pose.p={x+half,0,0};return o;}
static float TipX(const Pose& p) {return Apply(p,{0,.7f,0}).x;}

// The barrel swung by the wrist alone towards a wall on its right, which it would pass by the far end. Nothing about the wrist moves.
struct Swing {float tip;float wrist;float deepest;float angle;};
static Swing RunSwing(World& w,const Options& o,float degPerFrame,int frames) {
    State s;
    Solve(w,&kRifle,1,s,Pose{Q{},{}},nullptr,0,o);
    Swing out{-1e3f,0,0,0};
    for(int f=1;f<=frames;++f) {
        const Pose t{AboutZ(-float(M_PI)/180.f*degPerFrame*f),{}};
        const Result r=Solve(w,&kRifle,1,s,t,nullptr,0,o);
        const float tip=TipX(r.pose);
        if(tip>out.tip)out.tip=tip;
        const float d=w.Worst(&kRifle,1,r.pose);
        if(d>out.deepest)out.deepest=d;
        out.wrist=Length(r.pose.p);
        out.angle=Angle(r.pose.q,Q{});
    }
    return out;
}

static int RunReplay(World& w,const char* path) {
    FILE* f=nullptr;
    if(fopen_s(&f,path,"r") || !f) {printf("no replay file %s\n",path);return 1;}
    std::vector<Part> parts;
    State s;
    Options o;
    char tag[32];
    int frames=0,contacts=0;
    float worst=0;
    while(fscanf_s(f,"%31s",tag,unsigned(sizeof tag))==1) {
        if(!strcmp(tag,"part")) {
            Part p;int cap=0;
            fscanf_s(f,"%f %f %f %f %f %f %f %f %f %f %d %f %f",&p.center.x,&p.center.y,&p.center.z,&p.half.x,&p.half.y,&p.half.z,&p.q.x,&p.q.y,&p.q.z,&p.q.w,&cap,&p.radius,&p.halfHeight);
            p.capsule=cap!=0;
            parts.push_back(p);
        } else if(!strcmp(tag,"tri")) {
            ::Tri t;
            fscanf_s(f,"%f %f %f %f %f %f %f %f %f",&t.a.x,&t.a.y,&t.a.z,&t.b.x,&t.b.y,&t.b.z,&t.c.x,&t.c.y,&t.c.z);
            w.tris.push_back(t);
        } else if(!strcmp(tag,"clear")) {
            w.list.clear();
            w.tris.clear();
        } else if(!strcmp(tag,"shape")) {
            Obstacle b;
            fscanf_s(f,"%d %f %f %f %f %f %f %f %f %f %f %f %f",&b.kind,&b.half.x,&b.half.y,&b.half.z,&b.radius,&b.halfHeight,&b.pose.q.x,&b.pose.q.y,&b.pose.q.z,&b.pose.q.w,&b.pose.p.x,&b.pose.p.y,&b.pose.p.z);
            w.list.push_back(b);
        } else if(!strcmp(tag,"frame")) {
            Pose t;
            fscanf_s(f,"%f %f %f %f %f %f %f",&t.q.x,&t.q.y,&t.q.z,&t.q.w,&t.p.x,&t.p.y,&t.p.z);
            const Result r=Solve(w,parts.data(),int(parts.size()),s,t,nullptr,0,o);
            if(r.contact)++contacts;
            const float d=w.Worst(parts.data(),int(parts.size()),r.pose);
            if(d>worst)worst=d;
            ++frames;
        }
    }
    fclose(f);
    printf("replay: %d frames, %zu parts, %d with contact, deepest overlap %.4f m\n",frames,parts.size(),contacts,worst);
    return worst>.01f?1:0;
}

int main(int argc,char** argv) {
    const std::string bin=argc>1?argv[1]:"E:\\SteamLibrary\\steamapps\\common\\Cyberpunk 2077\\bin\\x64";
    SetDllDirectoryA(bin.c_str());
    HMODULE common=LoadLibraryA((bin+"\\PhysX3Common_x64.dll").c_str());
    if(!common) {printf("cannot load PhysX3Common: %lu\n",GetLastError());return 100;}
    penetration=reinterpret_cast<PenetrationFn>(GetProcAddress(common,"?computePenetration@PxGeometryQuery@physx@@SA_NAEAVPxVec3@2@AEAMAEBVPxGeometry@2@AEBVPxTransform@2@23@Z"));
    sweep=reinterpret_cast<SweepFn>(GetProcAddress(common,"?sweep@PxGeometryQuery@physx@@SA_NAEBVPxVec3@2@MAEBVPxGeometry@2@AEBVPxTransform@2@12AEAUPxSweepHit@2@V?$PxFlags@W4Enum@PxHitFlag@physx@@G@2@M@Z"));
    if(!penetration || !sweep) {printf("exports not found\n");return 101;}
    World w;
    // finds where this DLL puts a hit's normal, with a sweep that must hit
    {
        w.list={Wall(.5f)};
        Hit h;
        const Part ball{{},{.05f,.05f,.05f}};
        const bool got=w.Sweep(ball,{},{1,0,0},2.f,h);
        printf("hit normal at +%d, first sweep %s, distance %.3f, normal %.2f %.2f %.2f\n",normalAt,got?"hit":"MISSED",h.distance,h.normal.x,h.normal.y,h.normal.z);
        if(!got)return 102;
    }
    const Options good;
    Options old;
    old.substeps=false;old.turnAnswer=false;
    constexpr float kWall=.3f;   // the barrel's tip would reach x=.7 at 90 degrees

    printf("\nfree air\n");
    {
        w.list.clear();
        State s;
        Solve(w,&kRifle,1,s,{},nullptr,0,good);
        Pose t{Norm({.2f,.5f,-.3f,.8f}),{.4f,-.2f,.9f}};
        const Result r=Solve(w,&kRifle,1,s,t,nullptr,0,good);
        Check("reaches the tracked pose exactly",Length(r.pose.p-t.p)<1e-4f && Angle(r.pose.q,t.q)<1e-3f,"%.5f m %.5f rad",Length(r.pose.p-t.p),Angle(r.pose.q,t.q));
        Check("reports no contact",!r.contact,"%.0f",r.contact);
    }
    printf("\nside-on: the wrist swings a rifle into a wall (the reported fault)\n");
    {
        w.list={Wall(kWall)};
        const Swing n=RunSwing(w,good,6.f,20);
        Check("new: the tip stays on its side of the wall",n.tip<=kWall+.003f,"tip x %.3f, wall %.3f",n.tip,kWall);
        Check("new: never inside it",n.deepest<.004f,"deepest %.4f m",n.deepest);
        Check("new: the wrist hardly moves, the barrel is what stops",n.wrist<.05f,"wrist moved %.3f m, turned %.2f rad",n.wrist,n.angle);
    }
    printf("\nthe same on a thin sheet, a fast flick (45 deg a frame, 2000 deg/s)\n");
    {
        w.list={Wall(kWall,.0008f)};
        const Swing n=RunSwing(w,good,45.f,6);
        Check("new: does not pass the sheet",n.tip<=kWall+.005f,"tip x %.3f, wall %.3f",n.tip,kWall);
    }
    printf("\na post the muzzle would jump over in one frame\n");
    {
        Obstacle post;post.half={.02f,.02f,1.f};post.pose.p={.23f,.554f,0};
        w.list={post};
        const Swing n=RunSwing(w,good,45.f,1);
        const Swing o=RunSwing(w,old,45.f,1);
        Check("new: stopped by it",n.angle<float(M_PI)/6.f,"turned %.3f rad of %.3f",n.angle,float(M_PI)/4.f);
        Check("old behaviour jumps it (the test can tell)",o.angle>float(M_PI)/4.f-.02f,"turned %.3f rad of %.3f",o.angle,float(M_PI)/4.f);
    }
    printf("\nhand straight at a wall\n");
    {
        w.list={Wall(kWall)};
        State s;
        const Pose side{AboutZ(float(M_PI)*-.5f),{}};
        Solve(w,&kRifle,1,s,Pose{side.q,{-.6f,0,0}},nullptr,0,good);
        Result r{};
        for(int f=0;f<15;++f)r=Solve(w,&kRifle,1,s,Pose{side.q,{-.6f+.05f*f,0,0}},nullptr,0,good);
        Check("stops at the wall, a gap kept",TipX(r.pose)<=kWall && TipX(r.pose)>kWall-.03f,"tip x %.4f, wall %.3f",TipX(r.pose),kWall);
        Check("turned barely",Angle(r.pose.q,side.q)<.15f,"%.3f rad",Angle(r.pose.q,side.q));
        for(int f=0;f<3;++f)r=Solve(w,&kRifle,1,s,Pose{side.q,{-.8f,0,0}},nullptr,0,good);
        Check("comes back free once the hand does",Length(r.pose.p-V3{-.8f,0,0})<1e-3f && Angle(r.pose.q,side.q)<2e-3f,"%.4f m %.4f rad",Length(r.pose.p-V3{-.8f,0,0}),Angle(r.pose.q,side.q));
    }
    printf("\na corner: two walls, the hand pushed diagonally in\n");
    {
        Obstacle a=Wall(.3f);
        Obstacle b;b.half={5,.1f,5};b.pose.p={0,.3f+.1f,0};
        w.list={a,b};
        const Part ball{{},{.05f,.05f,.05f}};
        State s;
        Solve(w,&ball,1,s,{},nullptr,0,good);
        Result r{};
        float worst=0;
        for(int f=1;f<20;++f) {
            r=Solve(w,&ball,1,s,Pose{Q{},{.02f*f,.03f*f,0}},nullptr,0,good);
            const float d=w.Worst(&ball,1,r.pose);
            if(d>worst)worst=d;
        }
        Check("rests in the corner",r.pose.p.x<=.25f && r.pose.p.y<=.25f && r.pose.p.x>.23f && r.pose.p.y>.23f,"x %.3f y %.3f",r.pose.p.x,r.pose.p.y);
        Check("never inside either wall",worst<.003f,"deepest %.4f m",worst);
    }
    printf("\nslide along a wall at a grazing angle\n");
    {
        w.list={Wall(.3f)};
        const Part ball{{},{.05f,.05f,.05f}};
        State s;
        Solve(w,&ball,1,s,Pose{Q{},{.2f,0,0}},nullptr,0,good);
        Result r{};
        for(int f=1;f<=50;++f) {r=Solve(w,&ball,1,s,Pose{Q{},{.2f+.01f*f,.02f*f,0}},nullptr,0,good);if(f<10)printf("    f%d x %.5f contact %d hit %.5f\n",f,r.pose.p.x,int(r.contact),r.lastHit.distance);}
        Check("slides the whole way along",std::fabs(r.pose.p.y-1.f)<.005f,"y %.3f of 1.000",r.pose.p.y);
        Check("keeps its gap, along the normal",r.pose.p.x<=.25f-.0015f && r.pose.p.x>=.25f-.006f,"gap %.4f m",.25f-r.pose.p.x);
    }
    printf("\nthe gun got behind a thin wall: the tether, side-on\n");
    {
        w.list={Wall(.2f,.002f)};
        State s;
        s.on=true;
        s.pose=Pose{AboutZ(float(M_PI)*-.5f),{.1f,0,0}};  // the barrel through the sheet
        const V3 anchor{0,0,0};
        Result r=Solve(w,&kRifle,1,s,s.pose,&anchor,0,good);
        Check("brought back to the wrist's side",TipX(r.pose)<=.205f,"tip x %.3f",TipX(r.pose));
    }
    printf("\ntriangle sheets (one surface, no thickness)\n");
    {
        w.list.clear();
        const V3 p0{kWall,-5,-5},p1{kWall,5,-5},p2{kWall,5,5},p3{kWall,-5,5};
        w.tris={{p0,p1,p2},{p0,p2,p3}};
        const Swing n=RunSwing(w,good,6.f,20);
        Check("a wrist swing stops at the sheet",n.tip<=kWall+.003f,"tip x %.3f, wall %.3f",n.tip,kWall);
        Check("the wrist stays put",n.wrist<.05f,"wrist moved %.3f m",n.wrist);
        const Swing f=RunSwing(w,good,45.f,2);
        Check("a 2000 deg/s flick does not pass it",f.tip<=kWall+.005f,"tip x %.3f",f.tip);
        State s;
        const Pose side{AboutZ(float(M_PI)*-.5f),{}};
        Solve(w,&kRifle,1,s,Pose{side.q,{-.6f,0,0}},nullptr,0,good);
        Result r{};
        for(int f2=0;f2<15;++f2)r=Solve(w,&kRifle,1,s,Pose{side.q,{-.6f+.05f*f2,0,0}},nullptr,0,good);
        Check("a hand pushed straight at it is stopped",TipX(r.pose)<=kWall && TipX(r.pose)>kWall-.03f,"tip x %.4f",TipX(r.pose));
        // a replay of the same, through the file format the game writes
        char path[MAX_PATH];
        GetTempPathA(MAX_PATH,path);
        const std::string file=std::string(path)+"physics_replay_test.txt";
        FILE* o=nullptr;
        fopen_s(&o,file.c_str(),"w");
        fprintf(o,"part 0 .35 0 .02 .35 .04 0 0 0 1 0 0 0\nclear\n");
        for(const ::Tri& t:w.tris)fprintf(o,"tri %f %f %f %f %f %f %f %f %f\n",t.a.x,t.a.y,t.a.z,t.b.x,t.b.y,t.b.z,t.c.x,t.c.y,t.c.z);
        for(int i=0;i<=30;++i) {const Q q=AboutZ(-float(M_PI)/180.f*3.f*i);fprintf(o,"frame %f %f %f %f 0 0 0\n",q.x,q.y,q.z,q.w);}
        fclose(o);
        World fresh;
        Check("the replay format reads back, nothing passes",RunReplay(fresh,file.c_str())==0,"%.0f",0);
        w.tris.clear();
    }
    printf("\nlimits and cost\n");
    {
        w.list={Wall(kWall)};
        State s;
        Solve(w,&kRifle,1,s,{},nullptr,0,good);
        LARGE_INTEGER a,b,q;QueryPerformanceFrequency(&q);
        QueryPerformanceCounter(&a);
        int n=0;
        for(int i=0;i<2000;++i) {Solve(w,&kRifle,1,s,Pose{AboutZ(-.8f+.8f*std::sin(i*.1f)),{}},nullptr,0,good);++n;}
        QueryPerformanceCounter(&b);
        const double ms=double(b.QuadPart-a.QuadPart)*1000.0/double(q.QuadPart)/n;
        Check("a solve in contact is cheap (one part, one obstacle)",ms<.5,"%.4f ms",ms);
    }
    if(argc>2)failed+=RunReplay(w,argv[2]);
    printf("\n%d failed\n",failed);
    return failed;
}
