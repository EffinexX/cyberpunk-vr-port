// ThingHighlight -- the game's own outline on a loose prop. What is left of HeldPicture.cpp: the picture of a carried thing was retired on 2026-11-08, when the body itself came to be driven on the hand
// (Natives/PhysicsRuntime.cpp). The picture's method is in git history before branch native-hand-physics.
#include "Natives/NativeFunctions.hpp"
#include "Core/GameBuild.hpp"
#include <RED4ext/Scripting/Utils.hpp>
#include <RED4ext/Scripting/Natives/Vector4.hpp>
#include <windows.h>
#include <cstring>

extern void Log(const char* fmt, ...);

namespace {
constexpr uintptr_t kListenerEntryRva=0x5C3010,kSystemRva=0x3BFCDD0,kListeners=0x222018,kProxyId=0x160,kListenerAt=0x100;
}

// THE GAME'S OWN OUTLINE ON A THING WITHOUT AN ENTITY. entRenderHighlightEvent ends in sub_14079FE8C(render object, params) and that in a command of the render scene: slot 0x2D8 (sub_1407A0190), arguments &object+8, &object+0x10,
// on, mask, opacity, 0. Read in the exe, all callers (sub_14079FE8C, sub_140402428, sub_1403DA0F4) alike. Mask: 1 an outline, 2 a fill, the index <<2, 0x40 through walls. The render side (sub_14067AAB4) calls slot 0xD0 of what +0x10 holds
// and goes on only for what that answers with a type (+0x7D >>3) of 1, 5 or 7. A prop's object is at instance+0xC8 and is of the same make: sub_14020A038 reads it the same way, +8 a thing with a vtable, +0x10 the handle. The first try passed
// +0x10 first and null second, read as fill and outline: a crash on the render thread at sub_14067AAFF. The whole object lights up: a node's instances together.
namespace {
uintptr_t base=0;
uintptr_t System() {return *reinterpret_cast<uintptr_t*>(base+kSystemRva);}
// The instance behind a proxy, if the listener registered for it is one of these.
uint8_t* InstanceOf(uint32_t proxy) {
    const uintptr_t system=System();
    if(!system)return nullptr;
    const uintptr_t listener=*reinterpret_cast<uintptr_t*>(system+kListeners+uintptr_t(proxy&0xFFFF)*8);
    if(!listener)return nullptr;
    const uintptr_t table=*reinterpret_cast<uintptr_t*>(listener);
    if(!table || *reinterpret_cast<uintptr_t*>(table+8)!=base+kListenerEntryRva)return nullptr;
    auto* instance=reinterpret_cast<uint8_t*>(listener-kListenerAt);
    return *reinterpret_cast<uint32_t*>(instance+kProxyId)==proxy?instance:nullptr;
}
bool Readable(uintptr_t at,size_t size) {
    if(at<0x10000)return false;
    MEMORY_BASIC_INFORMATION m{};
    if(!VirtualQuery(reinterpret_cast<void*>(at),&m,sizeof(m)) || m.State!=MEM_COMMIT || (m.Protect&(PAGE_NOACCESS|PAGE_GUARD)))return false;
    return at+size<=reinterpret_cast<uintptr_t>(m.BaseAddress)+m.RegionSize;
}
// In the image, in .text: a slot of a vtable that has it.
bool CodeOfImage(uintptr_t at) {
    static uintptr_t lo=0,hi=0;
    if(!lo) {
        const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
        const auto* section=IMAGE_FIRST_SECTION(nt);
        for(int i=0;i<nt->FileHeader.NumberOfSections;++i,++section)if(!std::memcmp(section->Name,".text",5)) {
            lo=base+section->VirtualAddress;
            hi=lo+section->Misc.VirtualSize;
        }
    }
    return at>=lo && at<hi;
}
void* AnswerOf(uintptr_t held) {
    __try {
        using Get=void*(*)(void*);
        return reinterpret_cast<Get>((*reinterpret_cast<void***>(held))[0xD0/8])(reinterpret_cast<void*>(held));
    } __except(EXCEPTION_EXECUTE_HANDLER) {return nullptr;}
}
}  // namespace
void VRThingHighlight(RED4ext::IScriptable*,RED4ext::CStackFrame* stack,bool* out,int64_t) {
    int32_t proxy=0,fill=0,outline=0;
    bool through=false;
    RED4ext::GetParameter(stack,&proxy);
    RED4ext::GetParameter(stack,&fill);
    RED4ext::GetParameter(stack,&outline);
    RED4ext::GetParameter(stack,&through);
    stack->code++;
    if(out)*out=false;
    if(!base)base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    if(!cvr::game_build::Supported() || proxy<=0)return;
    uint8_t* instance=InstanceOf(uint32_t(proxy));
    if(!instance)return;
    const uintptr_t object=*reinterpret_cast<uintptr_t*>(instance+0xC8);
    if(!Readable(object,0x18))return;
    const uintptr_t owner=*reinterpret_cast<uintptr_t*>(object+8),held=*reinterpret_cast<uintptr_t*>(object+0x10);
    if(!Readable(owner,16) || !Readable(held,16))return;
    const uintptr_t table=*reinterpret_cast<uintptr_t*>(held);
    if(!Readable(table,0xD8) || !CodeOfImage(*reinterpret_cast<uintptr_t*>(table+0xD0)))return;
    void* answer=AnswerOf(held);
    const int type=answer && Readable(reinterpret_cast<uintptr_t>(answer)+0x7D,1)?*(reinterpret_cast<uint8_t*>(answer)+0x7D)>>3:-1;
    {
        static int saidType[64]={};
        if(type>=0 && type<64 && saidType[type]<1) {
            ++saidType[type];
            Log("[picture] outline on proxy %d: render object type %d, %s\n",proxy,type,type==1 || type==5 || type==7?"it takes one":"it does not take one: its lines instead");
        } else if(type<0) {
            static bool said=false;
            if(!said) {said=true;Log("[picture] outline on proxy %d: no render object answers\n",proxy);}
        }
    }
    if(type!=1 && type!=5 && type!=7)return;
    void* renderer=*reinterpret_cast<void**>(base+0x3BBCAE8);
    if(!renderer)return;
    using GetScene=void*(*)(void*);
    void* scene=reinterpret_cast<GetScene>((*reinterpret_cast<void***>(renderer))[0x4D0/8])(renderer);
    if(!scene)return;
    void* entry=(*reinterpret_cast<void***>(scene))[0x2D8/8];
    if(reinterpret_cast<uintptr_t>(entry)!=base+0x7A0190) {
        static bool said=false;
        if(!said) {said=true;Log("[picture] the scene's highlight is not where it was read: no highlight\n");}
        return;
    }
    const bool on=fill>0 || outline>0;
    uint8_t mask=through?0x40:0;
    if(fill>0)mask|=uint8_t(fill<<2)|2;
    else if(outline>0)mask|=uint8_t(outline<<2);
    if(outline>0)mask|=1;
    using Command=void(*)(void*,void*,void*,uint8_t,uint8_t,float,uint8_t);
    reinterpret_cast<Command>(entry)(scene,reinterpret_cast<void*>(object+8),reinterpret_cast<void*>(object+0x10),on?1:0,mask,1.f,0);
    if(out)*out=true;
}

