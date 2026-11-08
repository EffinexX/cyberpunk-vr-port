// Emitters of our own in the game's Wwise (Audio/WwiseEmitter.hpp).
#include "Audio/WwiseEmitter.hpp"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>

extern void Log(const char*, ...);

namespace cvr::wwise {
namespace {

// Read from the game's own call of SetPosition (RVA 0xa172b4, build of 2026-11-08): front and top as floats, then the position as doubles, flags 3, and every vector as (x, z, y) of the game's.
struct WorldTransform {
    float front[3];
    float top[3];
    double position[3];
};
static_assert(offsetof(WorldTransform, position) == 24 && sizeof(WorldTransform) == 48);

using RegisterFn = int (*)(uint64_t);
using SetPositionFn = int (*)(uint64_t, const WorldTransform&, int);
using SetRtpcFn = int (*)(const char*, float, uint64_t, int32_t, int, bool);
using SetSwitchFn = int (*)(const char*, const char*, uint64_t);
using SetBusVolumeFn = int (*)(uint64_t, uint64_t, float);
using GetListenersFn = int (*)(uint64_t, uint64_t*, uint32_t&);
using PostEventFn = uint32_t (*)(const char*, uint64_t, uint32_t, void*, void*, uint32_t, void*, uint32_t);
using ExecuteFn = int (*)(const char*, int, uint64_t, int32_t, int, uint32_t);

constexpr uint64_t kFirstEmitter = 0x5652544F00000001ull, kAllListeners = ~0ull;
constexpr int kEmitters = 4, kScrapeBodies = 3, kAkSuccess = 1, kPositionBoth = 3, kCurveLinear = 4, kActionStop = 0;
constexpr int kAllEmitters = kEmitters + kScrapeBodies * 2;
constexpr ULONGLONG kScrapeAgainMs = 1100;   // the shortest slide event is 1.4 s (eventsmetadata)
constexpr int32_t kScrapeFadeMs = 150;

std::mutex g_lock;
int g_state = 0;  // 0 not looked for, 1 ready, -1 missing
RegisterFn g_register;
SetPositionFn g_setPosition;
SetRtpcFn g_setRtpc;
SetSwitchFn g_setSwitch;
SetBusVolumeFn g_setBusVolume;
GetListenersFn g_getListeners;
PostEventFn g_postEvent;
ExecuteFn g_execute;
bool g_registered[kAllEmitters];
// Two emitters a body: the start of the next slide overlaps the end of the last. No object with a destructor here: it is used under __try.
struct ScrapeState {char event[80];ULONGLONG started;int which;};
ScrapeState g_scrape[kScrapeBodies];
int g_next = 0, g_said = 0;
uint64_t g_listener = kAllListeners;

template <class T>
bool Find(HMODULE exe, T& out, const char* name) {
    out = reinterpret_cast<T>(GetProcAddress(exe, name));
    if (!out) Log("[wwise] the game does not export %s\n", name);
    return out != nullptr;
}

bool Ready() {
    if (g_state) return g_state > 0;
    const HMODULE exe = GetModuleHandleW(nullptr);
    bool ok = Find(exe, g_register, "?RegisterGameObj@SoundEngine@AK@@YA?AW4AKRESULT@@_K@Z");
    ok = Find(exe, g_setPosition, "?SetPosition@SoundEngine@AK@@YA?AW4AKRESULT@@_KAEBVAkWorldTransform@@W4AkSetPositionFlags@@@Z") && ok;
    ok = Find(exe, g_setRtpc, "?SetRTPCValue@SoundEngine@AK@@YA?AW4AKRESULT@@PEBDM_KHW4AkCurveInterpolation@@_N@Z") && ok;
    ok = Find(exe, g_setSwitch, "?SetSwitch@SoundEngine@AK@@YA?AW4AKRESULT@@PEBD0_K@Z") && ok;
    ok = Find(exe, g_setBusVolume, "?SetGameObjectOutputBusVolume@SoundEngine@AK@@YA?AW4AKRESULT@@_K0M@Z") && ok;
    ok = Find(exe, g_getListeners, "?GetListeners@Query@SoundEngine@AK@@YA?AW4AKRESULT@@_KPEA_KAEAI@Z") && ok;
    ok = Find(exe, g_postEvent, "?PostEvent@SoundEngine@AK@@YAIPEBD_KIP6AXW4AkCallbackType@@PEAUAkCallbackInfo@@@ZPEAXIPEAUAkExternalSourceInfo@@I@Z") && ok;
    ok = Find(exe, g_execute, "?ExecuteActionOnEvent@SoundEngine@AK@@YA?AW4AKRESULT@@PEBDW4AkActionOnEventType@12@_KHW4AkCurveInterpolation@@I@Z") && ok;
    g_state = ok ? 1 : -1;
    return ok;
}

void Place(uint64_t emitter, float x, float y, float z, float gain, int& registerResult, int& positionResult, int& volumeResult) {
    const int index = int(emitter - kFirstEmitter);
    registerResult = kAkSuccess;
    if (!g_registered[index]) {
        registerResult = g_register(emitter);
        g_registered[index] = registerResult == kAkSuccess;
    }
    const WorldTransform place{{0.f, 0.f, 1.f}, {0.f, 1.f, 0.f}, {double(x), double(z), double(y)}};
    positionResult = g_setPosition(emitter, place, kPositionBoth);
    if (g_listener == kAllListeners) {
        uint64_t listeners[4] = {};
        uint32_t count = 4;
        if (g_getListeners(emitter, listeners, count) == kAkSuccess && count > 0) {
            g_listener = listeners[0];
            Log("[wwise] the emitter hears on listener %llu (%u of them)\n", static_cast<unsigned long long>(g_listener), count);
        }
    }
    volumeResult = g_setBusVolume(emitter, g_listener, gain);
}

uint32_t PlayOn(uint64_t emitter, const char* event, float x, float y, float z, float gain, const char* group, const char* state, int& registerResult, int& positionResult, int& volumeResult) {
    Place(emitter, x, y, z, gain, registerResult, positionResult, volumeResult);
    // Without a value of their own these curves would stand wherever their default is; the gain below does the rest.
    g_setRtpc("ph_impact_force", 100.f, emitter, 0, kCurveLinear, false);
    g_setRtpc("lcm_foley_volume", 0.f, emitter, 0, kCurveLinear, false);
    g_setRtpc("RTPC_w_melee_volume", 0.f, emitter, 0, kCurveLinear, false);
    if (group && *group && state && *state) g_setSwitch(group, state, emitter);
    return g_postEvent(event, emitter, 0, nullptr, nullptr, 0, nullptr, 0);
}

}  // namespace

static bool PlayGuarded(const char* event, float x, float y, float z, float gain, const char* group, const char* state) {
    if (!event || !*event || !Ready()) return false;
    const uint64_t emitter = kFirstEmitter + uint64_t(g_next);
    int registerResult = 0, positionResult = 0, volumeResult = 0;
    uint32_t playing = 0;
    __try {
        playing = PlayOn(emitter, event, x, y, z, gain, group, state, registerResult, positionResult, volumeResult);
        if (!playing) {
            // The game may have thrown all objects away at a load: once more from scratch.
            g_registered[g_next] = false;
            playing = PlayOn(emitter, event, x, y, z, gain, group, state, registerResult, positionResult, volumeResult);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_state = -1;
        Log("[wwise] a call into the game's audio engine faulted: off until the next start\n");
        return false;
    }
    if (g_said < 12 || (!playing && g_said < 40)) {
        ++g_said;
        Log("[wwise] %s at %.2f %.2f %.2f, gain %.3f: register %d, position %d, volume %d (listener %llu), playing id %u\n", event, x, y, z, gain, registerResult, positionResult, volumeResult,
            static_cast<unsigned long long>(g_listener), playing);
    }
    g_next = (g_next + 1) % kEmitters;
    return playing != 0;
}

bool Play(const char* event, float x, float y, float z, float gain, const char* group, const char* state) {
    std::lock_guard lock(g_lock);
    return PlayGuarded(event, x, y, z, gain, group, state);
}

static void StopScrape(int body, ScrapeState& s) {
    for (int i = 0; i < 2; ++i) g_execute(s.event, kActionStop, kFirstEmitter + kEmitters + uint64_t(body) * 2 + i, kScrapeFadeMs, kCurveLinear, 0);
    s.event[0] = 0;
}

static bool ScrapeInner(int body, const char* event, float x, float y, float z, float gain) {
    ScrapeState& s = g_scrape[body];
    if (!*event) {
        if (s.event[0]) StopScrape(body, s);
        return true;
    }
    const ULONGLONG now = GetTickCount64();
    const bool same = std::strcmp(s.event, event) == 0;
    if (s.event[0] && !same) StopScrape(body, s);
    int registerResult = 0, positionResult = 0, volumeResult = 0;
    if (same && now - s.started < kScrapeAgainMs) {
        Place(kFirstEmitter + kEmitters + uint64_t(body) * 2 + s.which, x, y, z, gain, registerResult, positionResult, volumeResult);
        return true;
    }
    s.which ^= 1;
    const uint64_t emitter = kFirstEmitter + kEmitters + uint64_t(body) * 2 + s.which;
    uint32_t playing = PlayOn(emitter, event, x, y, z, gain, nullptr, nullptr, registerResult, positionResult, volumeResult);
    if (!playing) {
        g_registered[emitter - kFirstEmitter] = false;
        playing = PlayOn(emitter, event, x, y, z, gain, nullptr, nullptr, registerResult, positionResult, volumeResult);
    }
    if (g_said < 24 || (!playing && g_said < 60)) {
        ++g_said;
        Log("[wwise] scrape %d: %s, gain %.3f: register %d, position %d, volume %d, playing id %u\n", body, event, gain, registerResult, positionResult, volumeResult, playing);
    }
    if (!playing) {
        s.event[0] = 0;
        return false;
    }
    strcpy_s(s.event, event);
    s.started = now;
    return true;
}

static bool ScrapeGuarded(int body, const char* event, float x, float y, float z, float gain) {
    if (body < 0 || body >= kScrapeBodies || !event || !Ready() || std::strlen(event) >= 80) return false;
    __try {
        return ScrapeInner(body, event, x, y, z, gain);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_state = -1;
        Log("[wwise] a call into the game's audio engine faulted: off until the next start\n");
        return false;
    }
}

bool Scrape(int body, const char* event, float x, float y, float z, float gain) {
    std::lock_guard lock(g_lock);
    return ScrapeGuarded(body, event, x, y, z, gain);
}

}  // namespace cvr::wwise
