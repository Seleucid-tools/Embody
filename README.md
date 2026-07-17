# Embody — Enhanced Camera Emulated in SSE

*A from-scratch, framework-light SKSE plugin that gives Skyrim SE / Anniversary Edition a permanent first-person
body: look down and see your own chest, arms, and legs. It uses the [Enhanced Camera](https://www.nexusmods.com/skyrim/mods/57859)
(Oldrim, by LogicDragon) approach — move the body to meet the camera rather than the camera to the body —
implemented from scratch for the 64-bit engine. This repository is both a working mod and a documented reference
for how that's done.*

> **What this is.** A permanent first-person body for Skyrim SE / AE, built from scratch as a compact, documented
> resource. It does one thing — puts your body in first person while you're on foot — using an approach that's
> architecturally different from Improved Camera. If you're a **player**, [Improved Camera SE](https://www.nexusmods.com/skyrimspecialedition/mods/93962)
> is the mature, feature-complete choice and what you want. Embody is here for the **curious and the tinkerers**:
> read how it works, build on it, take the idea somewhere new.

---

## Requirements

**Required:**
- **SKSE64** — matching your game version (Steam or GOG build).
- **[Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444)** — Embody resolves
  every engine address through it at load; without it the plugin logs a fatal error and does not load.

**Optional (for the config menu only):**
- **SkyUI** + **MCM Helper** — adds the in-game menu described under [Configuration](#configuration). Entirely
  optional: Embody reads `Data/MCM/Settings/Embody.ini` and hot-reloads it live regardless, so without SkyUI you
  lose the menu, not any functionality. The MCM is just a front-end over the same INI the plugin reads.

## Installation

Install with your mod manager (MO2 / Vortex) like any SKSE plugin, or drop `Embody.dll` into
`Data/SKSE/Plugins/`. Make sure SKSE64 and Address Library are installed. Launch through SKSE.

---

## Configuration

All settings live in `Data/MCM/Settings/Embody.ini`, which the plugin **hot-reloads live** — edit and save, and the
body updates within about a second, no relaunch. The file is created for you on first launch. Configure it three ways:

**In-game menu (recommended)** — with SkyUI + MCM Helper installed: pause → Mod Configuration → **Embody**. Two pages:
- **Body Tuning** — anchor mode, body scale, and per-stance offset sliders. *The settings you'll actually use.*
- **Controls** — enable and rebind the optional tuning hotkeys.

**Hand-edit the INI** — every key is documented in the `settings.ini` that ships with the mod. Needs no SkyUI.

**Live hotkeys (optional, OFF by default)** — off so they can't clash with other mods or vanilla UI. Turn them on with
the **Enable Tuning Hotkeys** toggle on the MCM Controls page (or `bHotkeysEnabled=1` in the INI). Once enabled — all
keys rebindable in the MCM — the defaults are:

| Key | Action |
|---|---|
| **↑ / ↓** | body forward / back |
| **← / →** | body left / right |
| **Page Up / Page Down** | body up / down |
| **Insert / Delete** | body scale +1% / −1% (feet-clip correction) |
| **Scroll Lock** | cycle anchor mode (head-pin / root-pin / off) |
| **Home** | save current values to the INI |

---

## Compatibility

- **Improved Camera SE** — mutually exclusive (same job). Use one.
- **Community Shaders / ENB** — compatible. ENB users may want its near-clip fix for extreme look-down.
- **Killmove / VATS-cam mods (e.g. Violens)** — **fully compatible.** Embody does nothing during killmoves by
  default, so your cinematic third-person kills are untouched.
- **Body / skeleton / animation mods (XPMSSE, etc.)** — compatible. The body shown *is* your third-person model,
  so whatever body/skeleton you run is what you see. Note: because the body position is tuned to a character's
  proportions and your animations, you may want to adjust the offsets (via the MCM or INI) for your setup.
- **GOG version (1.6.1179) — UNTESTED, reports welcome.** In theory it works: Embody resolves addresses through the
  Address Library, which ships a 1.6.1179 database, and the plugin declares itself version-independent so SKSE will
  load it. The one unknown is whether GOG's build has the same interior code layout as Steam's at our hook sites.
  **If it's wrong, the failure is graceful — the body won't appear, it will not crash** (the hooks verify their
  target bytes and bail safely on a mismatch). If you're on GOG, please report whether it works.

## Scope & known limitations

**Scope:** Embody puts your body in first person while you're **on foot** (standing and sneaking, weapon drawn or
sheathed). States that are normally third person — sitting, crafting, mounted, killmoves, Vampire Lord / Werewolf —
are left as stock third person; Embody steps aside there rather than forcing first person. That's the defined scope,
not a bug. (It's also where the architecture could be extended, if you're reading this to build on it.)

Within that scope, the known rough edges:

- **1-frame flicker** when opening/closing the **Tween menu** (TAB) — an engine frame-ordering artifact at a menu
  boundary. Minor; the body is otherwise stable while the menu is open.
- **Head/arm hiding uses bone scaling**, so the body's shadow is headless.

---

## Building from source

Embody is one C++ file, one command, **zero fetched dependencies** — no MSVC, no vcpkg, no CommonLibSSE submodule.
It cross-compiles to a native Windows DLL with MinGW-w64.

### Linux — tested ✅

This is how Embody is developed and built; every release DLL comes from here.

```sh
# Arch: sudo pacman -S mingw-w64-gcc   |   Debian/Ubuntu: sudo apt install g++-mingw-w64-x86-64
x86_64-w64-mingw32-g++ -shared -O2 -nostdlib -Wl,--entry,DllMain \
    -o Embody.dll Embody.cpp -lkernel32 -luser32
```

Or use the bundled **`package.sh`**, which builds the DLL, assembles the mod archive (DLL + ESP + MCM config), and
can deploy to a local Mod Organizer 2 instance:

```sh
./package.sh            # dev build (logs to C:\Embody.log) + zip + deploy to MO2
./package.sh --release  # silent build (-DEMBODY_LOG=0, no log file) + zip only — this is what gets published
```

### Windows — should work, but UNTESTED ⚠️

The same MinGW-w64 toolchain exists for Windows via [MSYS2](https://www.msys2.org/), and there's no reason the build
wouldn't work — but it has **not** been verified, so treat it as *"should be fine, but beware."* In the MinGW64 shell:

```sh
pacman -S mingw-w64-x86_64-gcc
x86_64-w64-mingw32-g++ -shared -O2 -nostdlib -Wl,--entry,DllMain -o Embody.dll Embody.cpp -lkernel32 -luser32
```

> **You don't actually need a Windows build.** The shipped DLL *is* a native Windows PE32+ (kernel32/user32 only) and
> runs identically whether the game is on Windows or on Linux/Proton — only the *source build on a Windows toolchain*
> is unverified. If you do try it and hit (or don't hit) snags, a note in the issues would be appreciated.

**Logging:** the default build writes diagnostics to `C:\Embody.log`. Release builds pass `-DEMBODY_LOG=0`
(via `package.sh --release`) to compile logging out entirely, so the published DLL is silent and writes no files.

Deep-dive on how it works: `ARCHITECTURE.md`.

---

## How this was made

Full transparency: Embody was built with heavy use of **Claude** (Anthropic's AI) — what's come to be called "vibe
coding." The direction, the in-game testing, and the design decisions were the author's; a great deal of the actual
code was written in collaboration with the AI. The source is open precisely so anyone can read it, verify it, and
build on it.

---

## License & credits

**GPLv3.** If people build on this work, their improvements should be free too — the same way this was built on the
work of others. See `LICENSE`.

- **Embody** by **Seleucid** — [GitHub: Seleucid-Tools](https://github.com/Seleucid-Tools) ·
  [Nexus: SeleucidTools](https://www.nexusmods.com/users/SeleucidTools).
- Original **Enhanced Camera** technique and design: **LogicDragon** ([Oldrim mod 57859](https://www.nexusmods.com/skyrim/mods/57859)).
  This mod exists because that one did.
- **Improved Camera SE** by **ArranzCNL** ([GitHub](https://github.com/ArranzCNL/ImprovedCameraSE) ·
  [Nexus](https://www.nexusmods.com/skyrimspecialedition/mods/93962)) — its open-source code (MPL 2.0) was an
  invaluable reference for how the Special Edition engine handles first person. That groundwork made this port far
  more approachable; sincere thanks for keeping it open.
- Embody's own code is independently written (framework-light C, no shared code with Improved Camera); the render
  fix that keeps the body visible through the portal-culling system (see `ARCHITECTURE.md`) is original to this port.
