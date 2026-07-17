// Embody — a true first-person body for Skyrim Special Edition (Enhanced Camera, re-derived for the SSE engine).
// Copyright (C) 2026  Seleucid
//
// This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public
// License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any
// later version. It is distributed WITHOUT ANY WARRANTY; see the GNU General Public License for details. You should
// have received a copy of the GNU General Public License along with this program; if not, see
// <https://www.gnu.org/licenses/>.
//
// ---------------------------------------------------------------------------------------------------------------
// Framework-light: raw SKSE ABI, -nostdlib, links kernel32/user32 only, no CommonLibSSE. Version-independent — all
// engine addresses are resolved at load from the Address Library (see SKSEPlugin_Load); NO hardcoded RVAs.
// Two call-site hooks drive everything (installed in SKSEPlugin_Load at RelocationID-resolved addresses):
//   * UpdateCamera call site   -> Hook_UpdateCamera:      per-frame camera-state tracking, tuning hotkeys, cleanup,
//                                                          Tween-menu body-hold.
//   * UpdateFirstPerson call site -> Hook_UpdateFirstPerson: the body-ride (correctly-timed, late-frame).
// Full design writeup: ARCHITECTURE.md.  Fast dev/agent orientation: START-HERE.md.

#include <cstdint>
#include <windows.h>

typedef uint32_t UInt32;
struct SKSEPluginVersionData {
    UInt32 dataVersion; UInt32 pluginVersion;
    char name[256]; char author[256]; char supportEmail[252];
    UInt32 versionIndependenceEx; UInt32 versionIndependence;
    UInt32 compatibleVersions[16]; UInt32 seVersionRequired;
};
extern "C" __declspec(dllexport) SKSEPluginVersionData SKSEPlugin_Version = {
    1,                 // dataVersion (struct layout version)
    54,                // plugin version = release 0.54 (build 54; SKSE logs this)
    "Embody",          // name
    "Seleucid",        // author
    "",                // support email
    1,                 // versionIndependenceEx = kVersionIndependentEx_NoStructUse (we use raw offsets, no SKSE structs)
    1,                 // versionIndependence  = kVersionIndependent_AddressLibraryPostAE — resolve via Address Library,
                       //   so SKSE loads us on ANY post-AE runtime (Steam 1.6.x AND GOG 1.6.1179). THIS is what makes
                       //   GOG/future-update loading possible; do not zero it.
    { 0 },             // compatibleVersions (ignored when version-independent)
    0,                 // seVersionRequired
};

// Diagnostic logging to C:\Embody.log. Dev builds = 1 (default). Release builds pass -DEMBODY_LOG=0 on the compiler
// command line (see package.sh --release) so the shipped mod writes no files. All logging routes through LogLine.
#ifndef EMBODY_LOG
#define EMBODY_LOG 1
#endif

static uintptr_t g_base = 0;
static void  (*g_orig)(void*)         = 0;   // UpdateCamera call target (0x5510A0)
static void  (*g_origFP)(void*, void*) = 0;  // UpdateFirstPerson call target (0xD1BF70)
static void* (*g_get3D)(void*)        = 0;   // Get3D(TESObjectREFR*) @ 0x2E5770
static uintptr_t g_fadeAddr = 0;             // ThirdpersonFade movss @ 0x8E4DAD (F3 0F -> EB 58)
static BYTE g_fadeOrig[2] = {0,0};
static uint32_t g_lastId = 0xFFFFFFFF;
static uint64_t g_iniMtime = 0;              // last-seen Embody.ini write time, for live auto-reload on external edit/MCM
static float g_frozen[12] = {0};             // last first-person body local transform (rotate@+0x48 [9] + translate@+0x6C [3])
static bool  g_frozenValid = false;          // used to hold the body still during the Tween menu (TAB), flicker-free
static int g_anchorMode = 2;                 // 0=off, 1=root XY-pin, 2=head-pin (DEFAULT — user-preferred after A/B). Scroll Lock cycles.
// Anchor trims: 4 pose states x 3 axes. Pose changes with sneak AND weapon state, so each combo gets its own
// set. Keys edit the CURRENT state's set: ArrowUp/Down = fwd/back, ArrowLeft/Right = lateral, PgUp/PgDn = up/down.
// [state][axis]: state = (sneaking?2:0)|(drawn?1:0); axis 0=fwd 1=lat(right+) 2=up
static float g_trim[4][3] = {
    { -14.0f, 0.0f, -5.0f },                 // 0: standing + sheathed (user-tuned 2026-07-16, w/ 0.92 scale)
    { -10.0f, 0.0f,  0.0f },                 // 1: standing + drawn    (user-tuned 2026-07-16)
    { -10.0f, 0.0f,  0.0f },                 // 2: sneaking + sheathed — INTENTIONALLY == sneak-drawn: equal values
    { -10.0f, 0.0f,  0.0f },                 // 3: sneaking + drawn       mean NO visible foot-jump on draw/sheathe
                                             //    while sneaking (hunched stance hides trim imperfection anyway).
                                             //    Both remain independently tunable for other animation setups.
};
static const char* g_trimName[4] = { "stand-sheathed", "stand-DRAWN", "sneak-sheathed", "sneak-DRAWN" };
static int g_lastState = 0;                  // pose state as of last anchor frame (keys edit this set)
// Body scale (Insert/Delete, 1% steps): lets users who trim the body lower/forward shrink it so the feet
// don't clip the floor (body root is at ground level, so scaling pulls the head down, feet stay planted).
static float g_bodyScale = 0.92f;            // user-tuned default 2026-07-16 (pairs with the sheathed trim)
// Max yaw deviation the body may take from the camera during strafes, stored as (cos,sin) of the cap angle.
// (1,0) = 0° = hard pin (current default, user-approved). Future ini/MCM knob: users trade "moonwalk legs"
// (cap 0) against "body swings toward movement" (higher cap) to taste.
static float g_yawCapCos = 1.0f, g_yawCapSin = 0.0f;
// --- Remappable hotkeys ([Controls] in the INI; MCM "Controls" page rebinds these) ------------------------------
// Stored as DirectInput scan codes (DIK_*) — NOT Win32 VK codes — because that is what MCM Helper's keymap control
// writes, so the MCM and this plugin share one number for each bind. The polling sites convert to a VK via dxToVk()
// for GetAsyncKeyState. These are the compiled defaults; readIni() overrides from [Controls].
static int g_keySave       = 0xC7;   // DIK_HOME    — save current live values to the INI
static int g_keyAnchorMode = 0x46;   // DIK_SCROLL  — cycle anchor mode (off/head-pin/root-pin)
static int g_keyFwdPlus    = 0xC8;   // DIK_UP      — body forward
static int g_keyFwdMinus   = 0xD0;   // DIK_DOWN    — body back
static int g_keyLatMinus   = 0xCB;   // DIK_LEFT    — body left
static int g_keyLatPlus    = 0xCD;   // DIK_RIGHT   — body right
static int g_keyUpPlus     = 0xC9;   // DIK_PRIOR   — body up   (Page Up)
static int g_keyUpMinus    = 0xD1;   // DIK_NEXT    — body down (Page Down)
static int g_keyScalePlus  = 0xD2;   // DIK_INSERT  — body scale +1%
static int g_keyScaleMinus = 0xD3;   // DIK_DELETE  — body scale -1%
// Master hotkey gate. ALL tuning hotkeys are OFF by default so our keys can't clash with other mods / vanilla UI
// (arrows, Home, Ins/Del etc. are high-traffic). Everything the hotkeys do is also on the MCM, so off-by-default
// costs nothing. Enabled via the MCM Controls page toggle or [Controls] bHotkeysEnabled=1 for INI-only users.
static int g_hotkeysEnabled = 0;
// DirectInput scan code (what MCM Helper's keymap stores) -> Win32 virtual-key (what GetAsyncKeyState wants). The
// main-row keys share numbering but the extended keys (arrows, Home, PgUp, Ins/Del...) do NOT, and those are exactly
// our defaults — so an explicit table is required, not MapVirtualKey. 0 = unmapped (bind ignored). US-layout DIK set.
static int dxToVk(int dx) {
    switch (dx) {
        case 0x01: return 0x1B;                                 // Esc
        case 0x02: return 0x31; case 0x03: return 0x32; case 0x04: return 0x33; case 0x05: return 0x34;
        case 0x06: return 0x35; case 0x07: return 0x36; case 0x08: return 0x37; case 0x09: return 0x38;
        case 0x0A: return 0x39; case 0x0B: return 0x30;         // 1..0
        case 0x0C: return 0xBD; case 0x0D: return 0xBB;         // - =
        case 0x0E: return 0x08; case 0x0F: return 0x09;         // Backspace, Tab
        case 0x10: return 0x51; case 0x11: return 0x57; case 0x12: return 0x45; case 0x13: return 0x52;
        case 0x14: return 0x54; case 0x15: return 0x59; case 0x16: return 0x55; case 0x17: return 0x49;
        case 0x18: return 0x4F; case 0x19: return 0x50;         // Q..P
        case 0x1A: return 0xDB; case 0x1B: return 0xDD;         // [ ]
        case 0x1C: return 0x0D; case 0x1D: return 0x11;         // Enter, LCtrl
        case 0x1E: return 0x41; case 0x1F: return 0x53; case 0x20: return 0x44; case 0x21: return 0x46;
        case 0x22: return 0x47; case 0x23: return 0x48; case 0x24: return 0x4A; case 0x25: return 0x4B;
        case 0x26: return 0x4C;                                 // A..L
        case 0x27: return 0xBA; case 0x28: return 0xDE; case 0x29: return 0xC0;   // ; ' `
        case 0x2A: return 0x10; case 0x2B: return 0xDC;         // LShift, backslash
        case 0x2C: return 0x5A; case 0x2D: return 0x58; case 0x2E: return 0x43; case 0x2F: return 0x56;
        case 0x30: return 0x42; case 0x31: return 0x4E; case 0x32: return 0x4D;   // Z..M
        case 0x33: return 0xBC; case 0x34: return 0xBE; case 0x35: return 0xBF;   // , . /
        case 0x36: return 0x10; case 0x37: return 0x6A; case 0x38: return 0x12;   // RShift, Num*, LAlt
        case 0x39: return 0x20; case 0x3A: return 0x14;         // Space, CapsLock
        case 0x3B: return 0x70; case 0x3C: return 0x71; case 0x3D: return 0x72; case 0x3E: return 0x73;
        case 0x3F: return 0x74; case 0x40: return 0x75; case 0x41: return 0x76; case 0x42: return 0x77;
        case 0x43: return 0x78; case 0x44: return 0x79;         // F1..F10
        case 0x45: return 0x90; case 0x46: return 0x91;         // NumLock, ScrollLock
        case 0x47: return 0x67; case 0x48: return 0x68; case 0x49: return 0x69; case 0x4A: return 0x6D;
        case 0x4B: return 0x64; case 0x4C: return 0x65; case 0x4D: return 0x66; case 0x4E: return 0x6B;
        case 0x4F: return 0x61; case 0x50: return 0x62; case 0x51: return 0x63; case 0x52: return 0x60;
        case 0x53: return 0x6E;                                 // Numpad 7..0 and .
        case 0x57: return 0x7A; case 0x58: return 0x7B;         // F11, F12
        case 0x9C: return 0x0D; case 0x9D: return 0x11;         // NumpadEnter, RCtrl
        case 0xB5: return 0x6F; case 0xB8: return 0x12;         // Numpad/, RAlt
        case 0xC5: return 0x13;                                 // Pause
        case 0xC7: return 0x24; case 0xC8: return 0x26; case 0xC9: return 0x21;   // Home, Up, PgUp
        case 0xCB: return 0x25; case 0xCD: return 0x27;         // Left, Right
        case 0xCF: return 0x23; case 0xD0: return 0x28; case 0xD1: return 0x22;   // End, Down, PgDn
        case 0xD2: return 0x2D; case 0xD3: return 0x2E;         // Insert, Delete
        default: return 0;
    }
}
// Effect-shader redirection (Stoneflesh teal, Muffle shimmer...): the engine picks which player model gets the
// shader by reading PlayerCharacter.playerFlags.isInThirdPersonMode (byte @ +0xBE3 bit0 on 1.6.1170). While in
// true first person we briefly set it around the effect-attach calls so the shader lands on the visible BODY
// (IC's technique). Weapon drawn -> leave it, so the hi-fi arms (the visible ones) get the shader instead.
static bool (*g_origSRE1)(void*) = 0;        // ShaderReferenceEffect::Update call site 1 original (0x5D1A60)
static float g_scaleBase = 0.0f;             // engine's own root scale (captured; RaceMenu setscale etc.)
static float g_scaleLastW = -1.0f;           // last value WE wrote (to detect engine rewrites vs our own)
static float g_camDbg[3] = {0,0,0};          // last camera pos (diag)
// Version-independent resolved addresses (absolute; filled in SKSEPlugin_Load from the Address Library versionlib
// by loadVersionLib+vlibResolve). We ship stable RelocationIDs, NOT hardcoded RVAs, so a Bethesda update that only
// shifts addresses is handled automatically once SKSE + Address Library publish their version bump.
static uintptr_t g_relPlayer = 0;            // = &(*g_thePlayer)          (id 403521)
static uintptr_t g_relCamera = 0;            // = &(*g_playerCamera)       (id 400802)
static uintptr_t g_relShaderState = 0;       // = &(*BSShaderManager::State) (id 390951)
static void* g_vtblFade = 0;                 // BSFadeNode vtable          (id 252901)
static void* g_vtblSSN  = 0;                 // ShadowSceneNode vtable     (id 254646)

// Toggle the engine's third-person body-fade: skip=true patches the movss to a jmp (body never fades).
static void patchFade(bool skip) {
    if (!g_fadeAddr) return;
    DWORD op;
    if (!VirtualProtect((void*)g_fadeAddr, 2, PAGE_EXECUTE_READWRITE, &op)) return;
    if (skip) { ((BYTE*)g_fadeAddr)[0]=0xEB; ((BYTE*)g_fadeAddr)[1]=0x58; }
    else      { ((BYTE*)g_fadeAddr)[0]=g_fadeOrig[0]; ((BYTE*)g_fadeAddr)[1]=g_fadeOrig[1]; }
    VirtualProtect((void*)g_fadeAddr, 2, op, &op);
    FlushInstructionCache(GetCurrentProcess(), (void*)g_fadeAddr, 2);
}

// Logging. In release (EMBODY_LOG=0) both LogLine and LOGF expand to nothing, so the format strings AND the
// wsprintfA formatting are removed entirely — the release DLL contains no log text and does no logging work.
// Use LOGF(fmt, ...) for formatted lines and LogLine("literal") for fixed ones.
#if EMBODY_LOG
static void LogLine(const char* s) {
    HANDLE h = CreateFileA("C:\\Embody.log", FILE_APPEND_DATA, FILE_SHARE_READ, NULL,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) { DWORD w; WriteFile(h, s, (DWORD)lstrlenA(s), &w, NULL); CloseHandle(h); }
}
#define LOGF(...) do { char _lb[224]; wsprintfA(_lb, __VA_ARGS__); LogLine(_lb); } while (0)
#else
#define LogLine(s) ((void)0)
#define LOGF(...)  ((void)0)
#endif
static void WriteU32(BYTE* p, uint32_t v) { for (int i=0;i<4;i++) p[i]=(BYTE)(v>>(i*8)); }
static void WriteU64(BYTE* p, uint64_t v) { for (int i=0;i<8;i++) p[i]=(BYTE)(v>>(i*8)); }

static int scmp(const char* a, const char* b) {
    while (*a && *a == *b) { ++a; ++b; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}
// True if n is a real NiNode (safe to read its +0x118 children array). AsNode() = vtable slot 3, returns
// `this` for NiNode-derived, null otherwise. Gating recursion on this stops walks from misreading a
// non-node (spell cast-art, particle systems, geometry leaves) as having children — the instant-CTD-on-cast.
static bool isNode(void* n) {
    if (!n || (uintptr_t)n < 0x10000) return false;
    void** vt = *(void***)n;
    if ((uintptr_t)vt < 0x10000) return false;
    return ((void*(*)(void*))vt[3])(n) == n;
}
// True if n's vtable is BSFadeNode's = a stable player-body root. Bails on a transient/rebuilding 3D
// (mid spell-cast / equip / POV swap) so we never operate on a half-built skeleton.
static bool isPlayerBody(void* n) {
    if (!n || (uintptr_t)n < 0x10000) return false;
    return *(void**)n == g_vtblFade;       // BSFadeNode vtable RVA
}
static void* FindNode(void* n, const char* name, int depth) {
    if (!n || depth > 40) return 0;
    const char* nm = *(const char**)((BYTE*)n + 0x10);      // NiObjectNET.name
    if (nm && (uintptr_t)nm > 0x10000 && scmp(nm, name) == 0) return n;
    if (!isNode(n)) return 0;                               // not a node -> no children array to read
    void** data = *(void***)((BYTE*)n + 0x118);             // NiNode.children._data
    uint16_t cap = *(uint16_t*)((BYTE*)n + 0x120);          // ._capacity
    if (data && (uintptr_t)data > 0x10000 && cap > 0 && cap <= 128) {
        for (uint16_t i = 0; i < cap; ++i) {
            void* c = data[i];
            if (c) { void* f = FindNode(c, name, depth + 1); if (f) return f; }
        }
    }
    return 0;
}

// Recursively set (on) / clear (off) the render flags across the body subtree. Per RENDER-mechanism.md, a
// DynamicNode child still faces per-geometry gates in SSN::OnVisible: kHidden (kill), and BSFadeNode fade. So
// on show we clear kHidden (root-only clear misses children), set kAlwaysDraw (1<<11, exempts frustum culling —
// z-buffer still applies so no see-through walls), and force fadeAmount opaque (+0x100), on EVERY node.
// Same +0xF4 flags field / +0x118 children walk as FindNode; isNode() gates recursion so cast-art leaves are safe.
static void setAlwaysDraw(void* n, bool on, int depth) {
    if (!n || depth > 60) return;
    uint32_t* fl = (uint32_t*)((BYTE*)n + 0xF4);
    if (on) { *fl &= ~1u; *fl |= 0x800u; *(float*)((BYTE*)n + 0x100) = 1.0f; }  // clear kHidden, set kAlwaysDraw, opaque
    else *fl &= ~0x800u;                                                        // restore (leave kHidden to the engine)
    if (!isNode(n)) return;                                 // leaf (geometry / cast-art) -> no children array
    void** data = *(void***)((BYTE*)n + 0x118);
    uint16_t cap = *(uint16_t*)((BYTE*)n + 0x120);
    if (data && (uintptr_t)data > 0x10000 && cap > 0 && cap <= 128)
        for (uint16_t i = 0; i < cap; ++i) { void* c = data[i]; if (c) setAlwaysDraw(c, on, depth + 1); }
}

// Inflate every node's world bounding-sphere radius (NiBound.radius @ NiAVObject+0xF0) so the body always
// passes the view-frustum / portal bound test. This is the actual cull we're fighting: pitch rotates the
// frustum, the body's bound leaves it, and the whole node is culled BEFORE per-geometry kAlwaysDraw matters.
// MUST run AFTER NiAVObject::Update (which recomputes bounds), else it's immediately overwritten.
static void inflateBounds(void* n, int depth) {
    if (!n || depth > 60) return;
    *(float*)((BYTE*)n + 0xF0) = 1.0e9f;                   // worldBound.radius = huge
    if (!isNode(n)) return;                                // leaf -> no children to recurse
    void** data = *(void***)((BYTE*)n + 0x118);
    uint16_t cap = *(uint16_t*)((BYTE*)n + 0x120);
    if (data && (uintptr_t)data > 0x10000 && cap > 0 && cap <= 128) {
        for (uint16_t i = 0; i < cap; ++i) { void* c = data[i]; if (c) inflateBounds(c, depth + 1); }
    }
}

static void setArmScale(void* root, float s) {
    void* L = FindNode(root, "NPC L UpperArm [LUar]", 0); if (L) *(float*)((BYTE*)L + 0x78) = s;
    void* R = FindNode(root, "NPC R UpperArm [RUar]", 0); if (R) *(float*)((BYTE*)R + 0x78) = s;
}
// Body-ride per-frame in first person: show body, hide head, weapon-gated arms.
static void applyBodyRide(void* firstNode) {
    if (!g_get3D) return;
    void* player = *(void**)(g_relPlayer);
    if (!player) return;
    void* thirdNode = g_get3D(player);
    if (!isPlayerBody(thirdNode)) return;                  // bail on transient/rebuilding 3D (cast/equip/POV)
    uint32_t* flags = (uint32_t*)((BYTE*)thirdNode + 0xF4);
    // THE FIX (build 28; see RENDER-mechanism.md): body renders IFF an ancestor is in the
    // portal graph's always-drawn list (pg+0x58) — normally via the "DynamicNode" container. When the engine
    // room-assigns the body instead (stairs/transitions), the visible-room walk can drop it -> every vanish
    // we've chased. So in 1st person keep the body parented under DynamicNode (engine-blessed dynamic-object
    // container: full render context, drawn in the end-loop, bypasses room culling). Build 25's steal failed
    // because the bare SSN isn't a render container; DynamicNode is.
    {
        void* ssn = *(void**)(g_relShaderState);
        void* pg = (ssn && (uintptr_t)ssn > 0x10000 && *(void**)ssn == g_vtblSSN)
                   ? *(void**)((BYTE*)ssn + 0x228) : 0;    // BSPortalGraph (interiors only)
        if (pg && (uintptr_t)pg > 0x10000) {
            void** data = *(void***)((BYTE*)pg + 0x58);
            uint32_t cnt = *(uint32_t*)((BYTE*)pg + 0x68);
            void* dyn = 0;
            if (data && (uintptr_t)data > 0x10000 && cnt <= 4096) {
                for (uint32_t i = 0; i < cnt; ++i) {        // find the DynamicNode container by name
                    void* it = data[i];
                    if (!it || (uintptr_t)it < 0x10000) continue;
                    const char* nm = *(const char**)((BYTE*)it + 0x10);
                    if (nm && (uintptr_t)nm > 0x10000 && scmp(nm, "DynamicNode") == 0) { dyn = it; break; }
                }
            }
            if (dyn && isNode(dyn)) {
                void* parent = *(void**)((BYTE*)thirdNode + 0x30);
                if (parent != dyn) {                        // steal to DynamicNode (re-assert if engine re-rooms)
                    if (parent && isNode(parent)) {
                        void** pvt = *(void***)parent;      // DetachChild2 = vfunc 0x38
                        ((void(*)(void*, void*))pvt[0x38])(parent, thirdNode);
                    }
                    void** dvt = *(void***)dyn;             // AttachChild(child, firstAvail=false) = vfunc 0x35
                    ((void(*)(void*, void*, uint32_t))dvt[0x35])(dyn, thirdNode, 0);
                    LOGF("[Embody] STEAL2 body %p: parent %p -> DynamicNode %p\r\n", thirdNode, parent, dyn);
                }
            }
        }
    }
    *flags &= ~1u;                                          // SHOW body (clear kHidden)
    setAlwaysDraw(thirdNode, true, 0);                      // exempt body from culling + force per-node fade opaque
    *(float*)((BYTE*)thirdNode + 0x130) = 1.0f;             // BSFadeNode currentFade — the LIVE gate per OnVisible decomp (+0x130, not +0x158)
    { // body scale: out = engineScale * g_bodyScale. Detect engine rewrites (RaceMenu/setscale) so we always
      // multiply the ENGINE's value, never our own output (no compounding).
        float* sc = (float*)((BYTE*)thirdNode + 0x78);
        if (*sc != g_scaleLastW) g_scaleBase = *sc;         // engine wrote a fresh value -> new base
        float out = g_scaleBase * g_bodyScale;
        *sc = out; g_scaleLastW = out;
    }
    void* head = FindNode(thirdNode, "NPC Head [Head]", 0);
    if (head) *(float*)((BYTE*)head + 0x78) = 0.002f;       // HIDE head
    // weapon-gated arms: drawn -> first-person arms (hi-fi); sheathed -> third-person body arms.
    uint32_t as2 = *(uint32_t*)((BYTE*)player + 0xCC);      // AE: ActorState@0xC0 + actorState2@0xC
    bool drawn = (((as2 >> 5) & 7u) >= 3u);                 // weaponState >= kDrawn(3)
    if (firstNode) setArmScale(firstNode, drawn ? 1.0f : 0.001f);
    setArmScale(thirdNode, drawn ? 0.001f : 1.0f);
}
// EC-style body-to-camera anchor: move the whole third-person body so a chosen node sits at the camera.
// Non-accumulating (sets root local outright each frame) so it can't drift; engine resets it when off.
// newRootLocal = camPos - (anchorWorld - rootWorld); the (anchor-root) offset is pose-stable frame-to-frame,
// so one-frame-stale world values are fine. mode1 pins head (natural look-down), mode2 pins spine2 (upper chest).
static void applyAnchor(void* thirdNode) {
    if (!g_anchorMode || !thirdNode) return;
    void* pcam = *(void**)(g_relCamera);            // PlayerCamera singleton
    if (!pcam) return;
    void* camRoot = *(void**)((BYTE*)pcam + 0x20);         // TESCamera.cameraRoot
    if (!camRoot) return;
    float* camT = (float*)((BYTE*)camRoot + 0xA0);         // cameraRoot world.translate = eye pos
    float* rL  = (float*)((BYTE*)thirdNode + 0x6C);        // root local.translate (what we drive)
    // pose-state trim: sneak and weapon state both change the stance -> 4 independent trim sets
    void* player = *(void**)(g_relPlayer);
    uint32_t as1 = player ? *(uint32_t*)((BYTE*)player + 0xC8) : 0;   // actorState1 (engine reads it here too)
    uint32_t as2 = player ? *(uint32_t*)((BYTE*)player + 0xCC) : 0;   // actorState2
    bool drawn = (((as2 >> 5) & 7u) >= 3u);
    bool sneak = ((as1 >> 9) & 1u) != 0;
    int st = (sneak ? 2 : 0) | (drawn ? 1 : 0);
    g_lastState = st;
    float offFwd = g_trim[st][0], offLat = g_trim[st][1], offUp = g_trim[st][2];
    // Gimbal-free horizontal axes from the camera's RIGHT vector (X column): pitch rotates AROUND it, so its
    // XY part stays a unit horizontal vector at every pitch — no singularity, no blending needed.
    float rx, ry, fx, fy;
    {
        float* rot = (float*)((BYTE*)camRoot + 0x7C);       // camRoot world.rotate (3x3 row-major)
        rx = rot[0]; ry = rot[3];                           // X column XY = camera right (horizontal at any pitch)
        float len = rx * rx + ry * ry;
        if (len > 1e-6f) {
            float g = 1.0f; for (int k = 0; k < 4; ++k) g = 0.5f * (g + 1.0f / (g * len));  // 1/sqrt(len), len~1
            rx *= g; ry *= g;
        } else { rx = 1; ry = 0; }
        fx = -ry; fy = rx;                                  // forward = right rotated 90° CCW (X-right, Y-fwd, Z-up)
    }
    for (int i = 0; i < 3; ++i) g_camDbg[i] = camT[i];
    // YAW-CLAMP: strafe locomotion rotates the model to face movement direction (fine in 3rd person, but in 1st
    // the body swings sideways under the camera). Let the body deviate from camera yaw only up to the cap angle
    // (cap 0° = hard pin, current default), then rebuild the root rotation as a pure-yaw upright matrix.
    {
        float* rR = (float*)((BYTE*)thirdNode + 0x48);      // root local.rotate (3x3 row-major)
        float bx = fx, by = fy;                             // final body facing; default = camera facing
        if (g_yawCapSin > 0.0f) {                           // cap > 0: allow deviation toward the engine's yaw
            float ex = rR[1], ey = rR[4];                   // engine's current facing (Y column XY, pre-overwrite)
            float el = ex * ex + ey * ey;
            if (el > 1e-6f) {
                float g = 1.0f; for (int k = 0; k < 4; ++k) g = 0.5f * (g + 1.0f / (g * el));
                ex *= g; ey *= g;
                float dot = fx * ex + fy * ey;              // cos(delta from camera yaw)
                if (dot >= g_yawCapCos) { bx = ex; by = ey; }        // within cap -> follow animation
                else {                                                // outside -> clamp at the cap edge
                    float crs = fx * ey - fy * ex;                    // sign picks which side to clamp toward
                    float s = (crs >= 0.0f) ? g_yawCapSin : -g_yawCapSin;
                    bx = fx * g_yawCapCos - fy * s;                   // camera facing rotated by +/-cap
                    by = fy * g_yawCapCos + fx * s;
                }
            }
        }
        float brx = by, bry = -bx;                          // body right = facing rotated 90° CW
        rR[0] = brx; rR[1] = bx; rR[2] = 0.0f;              // X column = right, Y column = forward, Z column = up
        rR[3] = bry; rR[4] = by; rR[5] = 0.0f;
        rR[6] = 0.0f; rR[7] = 0.0f; rR[8] = 1.0f;
    }
    if (g_anchorMode == 1) {
        // ROOT XY-PIN (pitch-stable): body root at fixed horizontal offset from camera; Z left to the engine.
        rL[0] = camT[0] + fx * offFwd + rx * offLat;
        rL[1] = camT[1] + fy * offFwd + ry * offLat;
        rL[2] += offUp;
    } else {
        // HEAD-PIN (mode 2, DEFAULT — user-preferred): head node glued to camera, trims applied after.
        void* anchor = FindNode(thirdNode, "NPC Head [Head]", 0);
        if (!anchor) return;
        float* anW = (float*)((BYTE*)anchor + 0xA0);
        float* rW  = (float*)((BYTE*)thirdNode + 0xA0);
        for (int i = 0; i < 3; ++i) rL[i] = camT[i] - (anW[i] - rW[i]);
        rL[0] += fx * offFwd + rx * offLat;
        rL[1] += fy * offFwd + ry * offLat;
        rL[2] += offUp;
    }
}

// Restore body nodes on leaving first person.
static void restoreThirdNode() {
    if (!g_get3D) return;
    void* player = *(void**)(g_relPlayer);
    if (!player) return;
    void* thirdNode = g_get3D(player);
    if (!isPlayerBody(thirdNode)) return;                  // bail on transient/rebuilding 3D
    setAlwaysDraw(thirdNode, false, 0);                     // restore normal culling for third person
    if (g_scaleBase > 0.0f) { *(float*)((BYTE*)thirdNode + 0x78) = g_scaleBase; g_scaleLastW = -1.0f; }  // un-scale
    void* head = FindNode(thirdNode, "NPC Head [Head]", 0);
    if (head) *(float*)((BYTE*)head + 0x78) = 1.0f;
    setArmScale(thirdNode, 1.0f);
}

// --- effect-shader redirection wrappers ---
// Lie window: only in true first person AND weapon sheathed (drawn = visible hi-fi arms should get the shader).
static BYTE* effectLieBegin() {
    if (g_lastId != 0) return 0;
    void* player = *(void**)(g_relPlayer);
    if (!player) return 0;
    uint32_t as2 = *(uint32_t*)((BYTE*)player + 0xCC);
    if ((((as2 >> 5) & 7u) >= 3u)) return 0;               // drawn -> no lie (arms take the effect)
    BYTE* fb = (BYTE*)player + 0xBE3;                      // playerFlags byte3: bit0 = isInThirdPersonMode
    if (*fb & 1u) return 0;                                // already "third person"? nothing to do
    *fb |= 1u;
    return fb;
}
extern "C" bool Hook_SRE1(void* a1) {                      // call site 1 (E8 -> Sub 0x5D1A60)
    BYTE* fb = effectLieBegin();
    bool r = g_origSRE1 ? g_origSRE1(a1) : false;
    if (fb) *fb &= ~1u;
    return r;
}
extern "C" void Hook_SRE2(void* a1) {                      // call site 2 (was: call [vtbl+0x1D8] on a1)
    BYTE* fb = effectLieBegin();
    if (a1 && (uintptr_t)a1 > 0x10000) {
        void** vt = *(void***)a1;
        if ((uintptr_t)vt > 0x10000) ((void(*)(void*))vt[0x3B])(a1);   // 0x1D8/8
    }
    if (fb) *fb &= ~1u;
}
static void* AllocNear(uintptr_t target, size_t size);     // fwd decl (defined below)
// Rewrite a 6-byte `FF 90 disp32` (call [rax+disp]) site into `E8 rel32; NOP` -> near stub -> hookFn.
static bool installVtblCallHook(uintptr_t siteRVA, uint32_t expectDisp, void* hookFn) {
    uintptr_t site = g_base + siteRVA;
    BYTE* p = (BYTE*)site;
    if (p[0] != 0xFF || p[1] != 0x90 || *(uint32_t*)(p + 2) != expectDisp) return false;  // bytes must match exactly
    BYTE* stub = (BYTE*)AllocNear(site, 12);
    if (!stub) return false;
    stub[0]=0x48; stub[1]=0xB8; WriteU64(stub+2, (uint64_t)hookFn); stub[10]=0xFF; stub[11]=0xE0;
    FlushInstructionCache(GetCurrentProcess(), stub, 12);
    int64_t nrel = (int64_t)((uintptr_t)stub - (site + 5));
    if (nrel < INT32_MIN || nrel > INT32_MAX) return false;
    DWORD op;
    if (!VirtualProtect((void*)site, 6, PAGE_EXECUTE_READWRITE, &op)) return false;
    p[0] = 0xE8; WriteU32(p + 1, (uint32_t)(int32_t)nrel); p[5] = 0x90;
    VirtualProtect((void*)site, 6, op, &op);
    FlushInstructionCache(GetCurrentProcess(), (void*)site, 6);
    return true;
}

// UpdateFirstPerson wrapper: correct timing for body-node manipulation.
extern "C" void Hook_UpdateFirstPerson(void* node, void* upd) {
    if (g_origFP) g_origFP(node, upd);
    if (g_lastId != 0) return;
    applyBodyRide(node);                                    // node = first-person skeleton
    void* player = *(void**)(g_relPlayer);
    void* thirdNode = player ? g_get3D(player) : 0;
    if (isPlayerBody(thirdNode)) {                          // only touch a stable BSFadeNode root (not mid-cast rebuild)
        applyAnchor(thirdNode);                             // EC body-to-camera anchor (Scroll Lock toggles)
        // Force-refresh the third-person skeleton every frame. The engine stops updating it while in first
        // person (it's normally hidden), so an un-refreshed skeleton falls out of the render — the "body only
        // shows while an animation is driving it (jumping)" bug. g_origFP IS NiAVObject::Update (0xD1BF70);
        // reusing the engine's own NiUpdateData (upd). = IC's Helper::UpdateNode(thirdpersonNode).
        if (g_origFP) g_origFP(thirdNode, upd);
        inflateBounds(thirdNode, 0);                        // AFTER Update: huge bounds -> never frustum/portal culled
        // snapshot the settled local transform so the Tween menu (below) can hold the body here, flicker-free
        float* src = (float*)((BYTE*)thirdNode + 0x48);
        for (int i = 0; i < 12; ++i) g_frozen[i] = src[i];
        g_frozenValid = true;
    }
}

static void writeIni();                                     // fwd decls (defined near SKSEPlugin_Load)
static void readIni();
static uint64_t iniMtimeNow();
// UpdateCamera wrapper: per-frame state tracking + exit cleanup.
extern "C" void Hook_UpdateCamera(void* cam) {
    if (g_orig) g_orig(cam);
    if (!cam) return;
    { // live config reload: if Embody.ini changed on disk (manual edit or MCM write), re-read it (~1/sec)
        static int rc = 0;
        if ((++rc % 60) == 0) { uint64_t m = iniMtimeNow(); if (m && m != g_iniMtime) { readIni(); g_iniMtime = m; LogLine("[Embody] config reloaded\r\n"); } }
    }
    if (g_hotkeysEnabled) {                                 // master gate: tuning hotkeys OFF by default (conflict-safe)
    { // Anchor-mode key cycles it live (off -> pin-head -> pin-spine2 -> off), edge-detected. Default Scroll Lock
      // (unbound by Skyrim/CS/Steam; F10 collides with the Community Shaders menu). Rebindable via [Controls].
        static bool prev = false;
        bool now = (GetAsyncKeyState(dxToVk(g_keyAnchorMode)) & 0x8000) != 0;
        if (now && !prev) {
            g_anchorMode = (g_anchorMode + 1) % 3;
            LOGF("[Embody] anchorMode=%d cam=(%d,%d,%d)\r\n", g_anchorMode,
                 (int)g_camDbg[0], (int)g_camDbg[1], (int)g_camDbg[2]);
        }
        prev = now;
    }
    { // Anchor trim live-tuning (user layout): ArrowUp/Down = body fwd/back, ArrowLeft/Right = body left/right,
      // PgUp/PgDn = body up/down. Keys edit the trim set of the CURRENT pose state (stand/sneak x sheathed/drawn).
        static bool pk[6] = {0,0,0,0,0,0};
        const int vk[6]   = { dxToVk(g_keyFwdPlus), dxToVk(g_keyFwdMinus), dxToVk(g_keyLatMinus),
                               dxToVk(g_keyLatPlus), dxToVk(g_keyUpPlus),  dxToVk(g_keyUpMinus) };
        static const int axis[6] = { 0, 0, 1, 1, 2, 2 };                    // fwd, fwd, lat, lat, up, up
        static const float dv[6] = { 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f };
        bool ch = false;
        for (int k = 0; k < 6; ++k) {
            bool now = (GetAsyncKeyState(vk[k]) & 0x8000) != 0;
            if (now && !pk[k]) { g_trim[g_lastState][axis[k]] += dv[k]; ch = true; }
            pk[k] = now;
        }
        if (ch) LOGF("[Embody] trim[%s] fwd=%d lat=%d up=%d\r\n", g_trimName[g_lastState],
                     (int)g_trim[g_lastState][0], (int)g_trim[g_lastState][1], (int)g_trim[g_lastState][2]);
        // Insert/Delete: body scale +/- 1% (feet-clip compensation for lowered-body trims)
        static bool pi2 = false, px2 = false;
        bool ni2 = (GetAsyncKeyState(dxToVk(g_keyScalePlus))  & 0x8000) != 0;  // scale up
        bool nx2 = (GetAsyncKeyState(dxToVk(g_keyScaleMinus)) & 0x8000) != 0;  // scale down
        bool sch = false;
        if (ni2 && !pi2) { g_bodyScale += 0.01f; sch = true; }
        if (nx2 && !px2) { g_bodyScale -= 0.01f; sch = true; }
        if (sch) {
            if (g_bodyScale > 1.5f) g_bodyScale = 1.5f;
            if (g_bodyScale < 0.5f) g_bodyScale = 0.5f;
            LOGF("[Embody] bodyScale=%d/100\r\n", (int)(g_bodyScale * 100.0f + 0.5f));
        }
        pi2 = ni2; px2 = nx2;
        // Home = save current live-tuned values to Embody.ini (persists across launches)
        static bool ph = false;
        bool nh = (GetAsyncKeyState(dxToVk(g_keySave)) & 0x8000) != 0;
        if (nh && !ph) writeIni();
        ph = nh;
    }
    }                                                      // end master hotkey gate
    void* state = *(void**)((BYTE*)cam + 0x28);
    if (!state) return;
    uint32_t id = *(uint32_t*)((BYTE*)state + 0x18);
    // "Body-visible" camera states = true first person (0) AND the Tween menu (7 = kTween, opened by TAB). The
    // tween pauses the world but keeps rendering it WITH the body, and it FREEZES UpdateFirstPerson — so our
    // body-ride stops and the body snaps to its physics position (the "TAB body-lunge"). We keep the body ridden
    // across both states, and re-pin it from HERE (UpdateCamera keeps ticking during the tween).
    const bool bodyState = (id == 0 || id == 7);
    const bool wasBody   = (g_lastId == 0 || g_lastId == 7);
    if (id != g_lastId) LOGF("[Embody] STATE %u -> %u\r\n", g_lastId, id);   // TEMP: state map
    if (id != g_lastId) {                                   // camera state transition
        if (id == 0) patchFade(true);                       // entered first person -> suppress the body fade
        if (wasBody && !bodyState) { restoreThirdNode(); patchFade(false); }  // left the body states -> restore
        g_lastId = id;
    }
    if (id == 0) {                                          // per-frame kHidden clear (re-hide race on stairs/movement)
        void* player = *(void**)(g_relPlayer);
        void* tn = player ? g_get3D(player) : 0;
        if (tn) *(uint32_t*)((BYTE*)tn + 0xF4) &= ~1u;
    }
    if (id == 7 && g_frozenValid) {                        // TWEEN: hold the body at its last first-person transform
        void* player = *(void**)(g_relPlayer);              // (UpdateFirstPerson is frozen here; re-asserting the
        void* tn = player ? g_get3D(player) : 0;            //  cached transform avoids reading the mid-zoom camera)
        if (isPlayerBody(tn)) {
            float* dst = (float*)((BYTE*)tn + 0x48);
            for (int i = 0; i < 12; ++i) dst[i] = g_frozen[i];   // restore rotate + translate
            void* head = FindNode(tn, "NPC Head [Head]", 0);
            if (head) *(float*)((BYTE*)head + 0x78) = 0.002f;    // keep the head hidden
            struct { float t; uint32_t f; } ud = { 0.0f, 0u };
            if (g_origFP) g_origFP(tn, &ud);                     // refresh world transform from the restored local
        }
    }
}

static void* AllocNear(uintptr_t target, size_t size) {
    const uintptr_t G = 0x10000;
    for (uintptr_t off = G; off < 0x70000000; off += G) {
        void* p = VirtualAlloc((void*)((target - off) & ~(G-1)), size, MEM_RESERVE|MEM_COMMIT, PAGE_EXECUTE_READWRITE);
        if (p) return p;
        p = VirtualAlloc((void*)((target + off) & ~(G-1)), size, MEM_RESERVE|MEM_COMMIT, PAGE_EXECUTE_READWRITE);
        if (p) return p;
    }
    return 0;
}

// Redirect the `call` at g_base+siteRVA to hookFn (via a near mov-rax/jmp stub); recover orig target.
static bool installCallHook(uintptr_t siteRVA, void* hookFn, void** origOut) {
    uintptr_t site = g_base + siteRVA;
    if (*(BYTE*)site != 0xE8) return false;
    int32_t rel = *(int32_t*)(site + 1);
    *origOut = (void*)(site + 5 + rel);
    BYTE* stub = (BYTE*)AllocNear(site, 12);
    if (!stub) return false;
    stub[0]=0x48; stub[1]=0xB8; WriteU64(stub+2, (uint64_t)hookFn); stub[10]=0xFF; stub[11]=0xE0;
    FlushInstructionCache(GetCurrentProcess(), stub, 12);
    int64_t nrel = (int64_t)((uintptr_t)stub - (site + 5));
    if (nrel < INT32_MIN || nrel > INT32_MAX) return false;
    DWORD op;
    if (!VirtualProtect((void*)(site+1), 4, PAGE_EXECUTE_READWRITE, &op)) return false;
    WriteU32((BYTE*)(site+1), (uint32_t)(int32_t)nrel);
    VirtualProtect((void*)(site+1), 4, op, &op);
    FlushInstructionCache(GetCurrentProcess(), (void*)site, 5);
    return true;
}

// ---- Address Library versionlib (format 2) reader — the version-independence core ----
// The Address Library ships a per-game-version DB (`versionlib-<maj>-<min>-<build>-0.bin`) in Data/SKSE/Plugins/,
// mapping stable RelocationIDs -> that version's RVAs. We resolve our IDs at LOAD instead of hardcoding RVAs, so a
// Bethesda update that only shifts addresses works the moment the Address Library ships its new bin — no recompile.
// (Verified byte-for-byte against the reference python parser + the live 1.6.1170 bin before shipping.)
static int vlibResolve(const unsigned char* buf, uint32_t sz, const uint64_t* ids, uint64_t* out, int n) {
    for (int i = 0; i < n; ++i) out[i] = 0;
    const unsigned char* p = buf; const unsigned char* end = buf + sz;
    #define VL_R8()  (p < end ? *p++ : (p++, 0))
    // little-endian reads (x86 native); guarded against overrun
    uint32_t fmt; if (p + 4 > end) return -1; fmt = *(const uint32_t*)p; p += 4;
    if (fmt != 2) return -1;                                 // Address Library SSE format 2 only
    p += 16;                                                 // skip version maj/min/pat/sub (4x u32)
    if (p + 4 > end) return -1; uint32_t nlen = *(const uint32_t*)p; p += 4; p += nlen;   // module name
    if (p + 8 > end) return -1; uint32_t ptrsize = *(const uint32_t*)p; p += 4;
    uint32_t count = *(const uint32_t*)p; p += 4;
    uint64_t pvid = 0, poff = 0; int resolved = 0;
    for (uint32_t k = 0; k < count && p < end; ++k) {
        uint8_t t = VL_R8(); int lo = t & 0xF, hi = t >> 4;
        uint64_t vid = 0;
        switch (lo) {
            case 0: if (p+8<=end){vid=*(const uint64_t*)p;} p+=8; break;
            case 1: vid = pvid + 1; break;
            case 2: vid = pvid + VL_R8(); break;
            case 3: vid = pvid - VL_R8(); break;
            case 4: if (p+2<=end){vid=pvid+*(const uint16_t*)p;} p+=2; break;
            case 5: if (p+2<=end){vid=pvid-*(const uint16_t*)p;} p+=2; break;
            case 6: if (p+2<=end){vid=*(const uint16_t*)p;} p+=2; break;
            default:if (p+4<=end){vid=*(const uint32_t*)p;} p+=4; break;   // 7
        }
        uint64_t tp = (hi & 8) ? (ptrsize ? poff / ptrsize : 0) : poff;
        int h = hi & 7; uint64_t off = 0;
        switch (h) {
            case 0: if (p+8<=end){off=*(const uint64_t*)p;} p+=8; break;
            case 1: off = tp + 1; break;
            case 2: off = tp + VL_R8(); break;
            case 3: off = tp - VL_R8(); break;
            case 4: if (p+2<=end){off=tp+*(const uint16_t*)p;} p+=2; break;
            case 5: if (p+2<=end){off=tp-*(const uint16_t*)p;} p+=2; break;
            case 6: if (p+2<=end){off=*(const uint16_t*)p;} p+=2; break;
            default:if (p+4<=end){off=*(const uint32_t*)p;} p+=4; break;   // 7
        }
        if (hi & 8) off *= ptrsize;
        for (int i = 0; i < n; ++i) if (vid == ids[i] && !out[i]) { out[i] = off; ++resolved; }
        pvid = vid; poff = off;
    }
    #undef VL_R8
    return resolved;
}
// Locate + read the versionlib bin. Tries the exact filename from the SKSE-reported runtime version first, then
// falls back to a glob (Address Library normally installs exactly one bin = the user's game version). Returns a
// VirtualAlloc'd buffer (caller frees) + size, or false.
static bool loadVersionLib(uint32_t runtimeVersion, unsigned char** bufOut, uint32_t* szOut) {
    char dir[MAX_PATH]; DWORD n = GetModuleFileNameA(NULL, dir, MAX_PATH);
    if (!n || n >= MAX_PATH) return false;
    while (n > 0 && dir[n-1] != '\\' && dir[n-1] != '/') --n;    // strip exe filename -> game dir
    dir[n] = 0;
    char pluginsDir[MAX_PATH]; lstrcpynA(pluginsDir, dir, MAX_PATH);
    lstrcatA(pluginsDir, "Data\\SKSE\\Plugins\\");
    char binName[64] = {0};
    if (runtimeVersion) {   // SKSE MAKE_EXE_VERSION: major<<24 | minor<<16 | build<<4
        int maj = (runtimeVersion >> 24) & 0xFF, min = (runtimeVersion >> 16) & 0xFF, bld = (runtimeVersion >> 4) & 0xFFF;
        wsprintfA(binName, "versionlib-%d-%d-%d-0.bin", maj, min, bld);
        char exact[MAX_PATH]; lstrcpynA(exact, pluginsDir, MAX_PATH); lstrcatA(exact, binName);
        if (GetFileAttributesA(exact) == INVALID_FILE_ATTRIBUTES) binName[0] = 0;   // not there -> glob fallback
    }
    if (!binName[0]) {      // glob fallback: first versionlib-*.bin present
        char glob[MAX_PATH]; lstrcpynA(glob, pluginsDir, MAX_PATH); lstrcatA(glob, "versionlib-*.bin");
        WIN32_FIND_DATAA fd; HANDLE fh = FindFirstFileA(glob, &fd);
        if (fh == INVALID_HANDLE_VALUE) return false;
        lstrcpynA(binName, fd.cFileName, sizeof(binName)); FindClose(fh);
    }
    char binPath[MAX_PATH]; lstrcpynA(binPath, pluginsDir, MAX_PATH); lstrcatA(binPath, binName);
    HANDLE f = CreateFileA(binPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD sz = GetFileSize(f, NULL);
    if (sz == INVALID_FILE_SIZE || sz < 32) { CloseHandle(f); return false; }
    unsigned char* buf = (unsigned char*)VirtualAlloc(NULL, sz, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!buf) { CloseHandle(f); return false; }
    DWORD rd = 0; BOOL ok = ReadFile(f, buf, sz, &rd, NULL); CloseHandle(f);
    if (!ok || rd != sz) { VirtualFree(buf, 0, MEM_RELEASE); return false; }
    *bufOut = buf; *szOut = sz;
    return true;
}

// ---- INI config (Data/MCM/Settings/Embody.ini) ----------------------------------------------------------------
// Foundation for user configuration (and for the future MCM Helper front-end, which reads/writes the same file).
// Compiled defaults load first; the INI overrides any keys present. The "save" hotkey (Home) writes the current
// live-tuned values back so tuning persists across launches. Ints only (all our tunables are whole units or %) —
// we read via GetPrivateProfileStringA + a hand-rolled parse because GetPrivateProfileIntA mishandles negatives.
static int ecAtoi(const char* s) {
    while (*s == ' ' || *s == '\t') ++s;
    int sign = 1; if (*s == '-') { sign = -1; ++s; } else if (*s == '+') ++s;
    int v = 0; while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); ++s; }
    return sign * v;
}
// Config lives at Data\MCM\Settings\Embody.ini — MCM Helper's own user-settings path — so the in-game MCM and this
// plugin read/write ONE file (no divergence). Prefixed keys (iMode, bHotkeysEnabled...) match MCM Helper's typed store.
// Without SkyUI/MCM Helper installed the plugin still owns this file: it self-seeds it (SKSEPlugin_Load) and hand
// edits / the Home-save hotkey write here too. ensureIniDir() creates the folder when MCM Helper isn't there to.
static void iniPath(char* out) {                            // <gamedir>\Data\MCM\Settings\Embody.ini
    char dir[MAX_PATH]; DWORD n = GetModuleFileNameA(NULL, dir, MAX_PATH);
    if (!n || n >= MAX_PATH) { out[0] = 0; return; }
    while (n > 0 && dir[n-1] != '\\' && dir[n-1] != '/') --n; dir[n] = 0;
    lstrcpynA(out, dir, MAX_PATH); lstrcatA(out, "Data\\MCM\\Settings\\Embody.ini");
}
static void ensureIniDir() {                                // create Data\MCM\Settings\ if it doesn't exist yet
    char dir[MAX_PATH]; DWORD n = GetModuleFileNameA(NULL, dir, MAX_PATH);
    if (!n || n >= MAX_PATH) return;
    while (n > 0 && dir[n-1] != '\\' && dir[n-1] != '/') --n; dir[n] = 0;
    char p[MAX_PATH];
    lstrcpynA(p, dir, MAX_PATH); lstrcatA(p, "Data\\MCM");           CreateDirectoryA(p, NULL);
    lstrcpynA(p, dir, MAX_PATH); lstrcatA(p, "Data\\MCM\\Settings"); CreateDirectoryA(p, NULL);
}
static int iniGetInt(const char* sec, const char* key, int def) {
    char path[MAX_PATH]; iniPath(path); if (!path[0]) return def;
    char buf[32], ds[16]; wsprintfA(ds, "%d", def);
    GetPrivateProfileStringA(sec, key, ds, buf, sizeof(buf), path);
    return ecAtoi(buf);
}
static void iniSetInt(const char* sec, const char* key, int val) {
    char path[MAX_PATH]; iniPath(path); if (!path[0]) return;
    char buf[16]; wsprintfA(buf, "%d", val);
    WritePrivateProfileStringA(sec, key, buf, path);
}
static uint64_t iniMtimeNow() {                             // 0 if the file doesn't exist
    char path[MAX_PATH]; iniPath(path); if (!path[0]) return 0;
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &d)) return 0;
    return ((uint64_t)d.ftLastWriteTime.dwHighDateTime << 32) | d.ftLastWriteTime.dwLowDateTime;
}
static const char* const g_stateKey[4] = { "StandSheathed", "StandDrawn", "SneakSheathed", "SneakDrawn" };
static const char* const g_axisKey[3]  = { "Fwd", "Lat", "Up" };
// MCM Helper key = type-prefix + name; trims are ints -> "i" + <state> + <axis>  (e.g. iStandSheathedFwd)
static void trimKey(char* out, int st, int ax) { lstrcpynA(out, "i", 40); lstrcatA(out, g_stateKey[st]); lstrcatA(out, g_axisKey[ax]); }
static void readIni() {                                     // INI overrides compiled defaults (keys = MCM-typed)
    g_anchorMode = iniGetInt("Anchor", "iMode", g_anchorMode);
    g_bodyScale  = (float)iniGetInt("Anchor", "iBodyScalePct", (int)(g_bodyScale * 100.0f + 0.5f)) / 100.0f;
    for (int s = 0; s < 4; ++s) for (int a = 0; a < 3; ++a) {
        char key[40]; trimKey(key, s, a);
        g_trim[s][a] = (float)iniGetInt("Trims", key, (int)g_trim[s][a]);
    }
    // [Controls]: remappable hotkeys as DIK scan codes (MCM keymap writes these; dxToVk bridges to GetAsyncKeyState).
    g_keySave       = iniGetInt("Controls", "iSave",       g_keySave);
    g_keyAnchorMode = iniGetInt("Controls", "iAnchorMode", g_keyAnchorMode);
    g_keyFwdPlus    = iniGetInt("Controls", "iFwdPlus",    g_keyFwdPlus);
    g_keyFwdMinus   = iniGetInt("Controls", "iFwdMinus",   g_keyFwdMinus);
    g_keyLatMinus   = iniGetInt("Controls", "iLatMinus",   g_keyLatMinus);
    g_keyLatPlus    = iniGetInt("Controls", "iLatPlus",    g_keyLatPlus);
    g_keyUpPlus     = iniGetInt("Controls", "iUpPlus",     g_keyUpPlus);
    g_keyUpMinus    = iniGetInt("Controls", "iUpMinus",    g_keyUpMinus);
    g_keyScalePlus  = iniGetInt("Controls", "iScalePlus",  g_keyScalePlus);
    g_keyScaleMinus = iniGetInt("Controls", "iScaleMinus", g_keyScaleMinus);
    g_hotkeysEnabled = iniGetInt("Controls", "bHotkeysEnabled", g_hotkeysEnabled);
}
static void writeIni() {                                    // persist current live values (keys = MCM-typed)
    ensureIniDir();                                         // make Data\MCM\Settings\ if MCM Helper isn't installed
    iniSetInt("Anchor", "iMode", g_anchorMode);
    iniSetInt("Anchor", "iBodyScalePct", (int)(g_bodyScale * 100.0f + 0.5f));
    for (int s = 0; s < 4; ++s) for (int a = 0; a < 3; ++a) {
        char key[40]; trimKey(key, s, a);
        iniSetInt("Trims", key, (int)g_trim[s][a]);
    }
    // [Controls]: written so the MCM/INI round-trips cleanly and the file self-documents current binds.
    iniSetInt("Controls", "iSave",       g_keySave);
    iniSetInt("Controls", "iAnchorMode", g_keyAnchorMode);
    iniSetInt("Controls", "iFwdPlus",    g_keyFwdPlus);
    iniSetInt("Controls", "iFwdMinus",   g_keyFwdMinus);
    iniSetInt("Controls", "iLatMinus",   g_keyLatMinus);
    iniSetInt("Controls", "iLatPlus",    g_keyLatPlus);
    iniSetInt("Controls", "iUpPlus",     g_keyUpPlus);
    iniSetInt("Controls", "iUpMinus",    g_keyUpMinus);
    iniSetInt("Controls", "iScalePlus",  g_keyScalePlus);
    iniSetInt("Controls", "iScaleMinus", g_keyScaleMinus);
    iniSetInt("Controls", "bHotkeysEnabled", g_hotkeysEnabled);
    g_iniMtime = iniMtimeNow();                             // adopt our own write so the poll doesn't reload it back
    LogLine("[Embody] saved config to Embody.ini\r\n");
}

extern "C" __declspec(dllexport) bool SKSEPlugin_Load(const void* skse) {
    g_base = (uintptr_t)GetModuleHandleA(NULL);
    // The SKSE load interface's 2nd u32 is the runtime version (SKSEInterface{ u32 skseVersion; u32 runtimeVersion; ... }).
    uint32_t rtv = skse ? *(const uint32_t*)((const BYTE*)skse + 4) : 0;
    unsigned char* vbuf = 0; uint32_t vsz = 0;
    if (!loadVersionLib(rtv, &vbuf, &vsz)) {
        LogLine("[Embody] FATAL: Address Library versionlib not found (Data/SKSE/Plugins/versionlib-*.bin). Is Address Library installed?\r\n");
        return false;
    }
    // RelocationIDs, order MUST match the enum below. See START-HERE.md for the full name<->id table. To add a
    // feature that needs a new engine address: add its id here + a matching enum entry, bump the count — never
    // hardcode an RVA. (This is the version-independence discipline: everything flows through the Address Library.)
    static const uint64_t IDS[10] = { 19735,50832,50784,40522,34913,403521,400802,390951,252901,254646 };
    enum { I_GET3D, I_FADE, I_UC, I_UFP, I_SRE, I_PLR, I_CAM, I_SS, I_VF, I_VS, I_COUNT };
    uint64_t R[I_COUNT];
    int got = vlibResolve(vbuf, vsz, IDS, R, I_COUNT);
    VirtualFree(vbuf, 0, MEM_RELEASE);
    if (got != I_COUNT) {
        LOGF("[Embody] FATAL: resolved only %d/%d addresses from versionlib (unsupported game version?)\r\n", got, I_COUNT);
        return false;
    }
    // assign resolved absolutes
    g_get3D      = (void*(*)(void*))(g_base + R[I_GET3D]);
    g_fadeAddr       = g_base + R[I_FADE] + 0x4DD;           // ThirdpersonFade fn + interior offset to the movss
    g_relPlayer      = g_base + R[I_PLR];
    g_relCamera      = g_base + R[I_CAM];
    g_relShaderState = g_base + R[I_SS];
    g_vtblFade = (void*)(g_base + R[I_VF]);
    g_vtblSSN  = (void*)(g_base + R[I_VS]);
    g_fadeOrig[0] = ((BYTE*)g_fadeAddr)[0]; g_fadeOrig[1] = ((BYTE*)g_fadeAddr)[1];
    // install hooks at function-entry + interior call-site offsets (interior offsets are version-specific but stable
    // for address-shift updates; a function-restructuring update would need these re-checked — hooks bail safely).
    bool a  = installCallHook(R[I_UC]  + 0x1A6, (void*)&Hook_UpdateCamera,      (void**)&g_orig);
    bool b  = installCallHook(R[I_UFP] + 0xD7,  (void*)&Hook_UpdateFirstPerson, (void**)&g_origFP);
    bool e1 = installCallHook(R[I_SRE] + 0xE1,  (void*)&Hook_SRE1,              (void**)&g_origSRE1);   // E8 site
    bool e2 = installVtblCallHook(R[I_SRE] + 0x1F5, 0x1D8, (void*)&Hook_SRE2);                          // FF90 site
    readIni();                                              // apply user config (overrides compiled defaults)
    if (!iniMtimeNow()) writeIni();                         // self-seed the shared INI if it doesn't exist yet
    g_iniMtime = iniMtimeNow();                             // baseline for the live-reload poll
    LOGF("[Embody] load base=%p rtv=%08X vlib=OK UC=%d UFP=%d SRE1=%d SRE2=%d fadeOrig=%02X %02X\r\n",
         (void*)g_base, rtv, a?1:0, b?1:0, e1?1:0, e2?1:0, g_fadeOrig[0], g_fadeOrig[1]);
    return true;
}
extern "C" BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }
