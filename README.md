# Embody — Enhanced Camera Emulated in SSE

*A from-scratch, framework-light SKSE plugin that gives Skyrim SE Anniversary Edition a visible first-person
body — the [Enhanced Camera](https://www.nexusmods.com/skyrim/mods/57859) (Oldrim, by LogicDragon) experience,
re-derived for the 64-bit engine. **Embody** is what the mod does — it gives your first-person view a body;
**Enhanced Camera Emulated** is the goal it's chasing (parity with the original), not yet fully reached.*

> **Version 0.54 — a small, working, deliberately unfinished mod, released as open source.** This repository *is* the
> whole thing: the plugin source, the documentation on how it works, and a ready-to-build mod. If you're a **player**,
> [Improved Camera SE](https://www.nexusmods.com/skyrimspecialedition/mods/93962) is the mature, polished,
> feature-complete choice and almost certainly what you want. Embody is here mostly for the **curious and the
> tinkerers** — anyone who wants to see how a first-person body is built from scratch on a modern engine, and maybe
> carry the idea somewhere better. See the [Roadmap](#roadmap) for what a 1.0 would take.

---

## Why another first-person body mod? (Embody vs. Improved Camera)

There's already an excellent one — [Improved Camera SE](https://www.nexusmods.com/skyrimspecialedition/mods/93962).
Embody is not a replacement so much as a **different approach with a different feel**, and it's fair to say up front
that Improved Camera is the more mature, more feature-complete mod today (it handles horses, dragons, werewolves,
killmoves, and scripted scenes that Embody does not — yet).

The one-line difference:

> **Improved Camera couples the *camera* to the body. Embody couples the *body* to the camera.**

Improved Camera repositions the camera each frame to ride your animated body — robust and full-featured, but because
the camera is driven *by* the body, it inherits the body's motion (head-bob) and can feel "attached to" the character.

Embody **never touches Skyrim's first-person camera at all.** The vanilla camera — same pivot, same smoothing
Bethesda ships — stays exactly as-is, and the *body* is moved to meet it each frame. Because the camera is never
coupled to the body, it keeps stock first-person feel while you gain a body. That's Enhanced Camera's design
philosophy — body-to-camera, not camera-to-body — re-derived for SSE's very different engine internals.

If you love vanilla first-person camera feel and just want a body under it, Embody is for you. If you want the
broadest state coverage today, Improved Camera is more complete. They are mutually exclusive — pick one.

---

## Features

- **Visible first-person body** — look down and see your chest, arms, legs, and feet, planted on the ground.
- **Vanilla camera, untouched** — no head-bob added, no camera reinvention. Stock first-person feel.
- **Weapon-aware arms** — hi-fidelity first-person arms when your weapon is drawn; your body's own arms when sheathed.
- **Fully tunable body position** — per-stance body offsets (standing/sneaking × sheathed/drawn), body scale, and
  anchor mode, set through an in-game **MCM menu** or a plain **INI** (hot-reloaded live), with optional hotkeys.
  Dial it to your character, animations, and FOV.
- **Magic effect shaders on the body** — Stoneflesh, Muffle, etc. render on the visible body.
- **Rock-solid rendering** — the body does not vanish at room transitions, on stairs, mid-cast, or during POV
  switches. (This was the hard part; see `ARCHITECTURE.md`.)
- **Non-breaking everywhere else** — in states Embody doesn't handle (third person, VATS killmoves, transforms,
  mounted), it steps aside and you get stock Skyrim. It never steals the camera from other mods.
- **Update-resilient** — resolves all engine addresses at load through the Address Library, so it survives the
  address shifts of most Bethesda updates automatically (see Compatibility).

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

**In-game menu (recommended)** — with SkyUI + MCM Helper installed: pause → Mod Configuration → **Embody**. Four pages:
- **Body Tuning** — anchor mode, body scale, and per-stance offset sliders. *The settings you'll actually use.*
- **Feature Toggles** — planned first-person states (placeholders for now; see [Roadmap](#roadmap)).
- **Transform Tuning** — Werewolf / Vampire Lord offsets (for a future feature).
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

## Known limitations

- **1-frame flicker** when opening/closing the **Tween menu** (TAB) — an engine frame-ordering artifact at a menu
  boundary. Minor; the body is otherwise stable while the menu is open.
- **No forced first-person in third-person states yet** — sitting, crafting, mounted riding, killmoves, and form
  transforms (Vampire Lord / Werewolf) currently play in vanilla third person. First-person versions of these
  (opt-in, per-state) are planned. It does *not* misbehave in these states; it just doesn't force first person.
- **Head/arm hiding uses bone scaling**, so the body's shadow is currently headless. Being reworked.

---

## Roadmap

**1.0 = full Enhanced Camera parity.** Embody currently shows the body in the states that are *already* first-person
(standing, sneaking). The rest of the work is bringing first person to states that are normally third-person, roughly
in order:

1. Sitting / furniture
2. Crafting stations (forge, alchemy, enchanting, …)
3. Mounted (horseback)
4. Killmoves (off by default — Violens-safe)
5. Werewolf / Vampire Lord

Placeholder toggles for these already exist in the MCM, so the plumbing is in place; each switches on as it lands.
Feedback on what to prioritize is welcome — open an issue.

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

Deep-dive on how it works: `ARCHITECTURE.md`. Developer/agent orientation: `START-HERE.md`.

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
