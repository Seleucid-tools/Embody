# Embody architecture — how the first-person body works

This is the deep explanation of *how* and *why* Embody works, for anyone maintaining or forking it. It assumes you
can read C and have a rough idea of SKSE plugins and the Skyrim (Creation Engine / Gamebryo-descended) scene graph.
If you just want to build and use the mod, see `README.md`. If you're an agent picking up development, read
`START-HERE.md` first for the fast orientation, then this.

Target: **Steam Anniversary Edition 1.6.1170** (and, via the Address Library, other 1.6.x runtimes including GOG).

---

## 1. The problem, and why it was "impossible" on SSE

Skyrim's first-person view is just floating arms — there's no body. Oldrim had **Enhanced Camera** (LogicDragon),
which added a real body by a clever trick built on the Oldrim engine's camera internals. It was never ported to SSE,
and the reason is real: **Bethesda changed how the camera works between Oldrim and SSE.**

Oldrim kept a per-camera-node "POV flag" (a bit at `node+0x98`) and a sync function that let the engine render a
first-person camera *and* a third-person body simultaneously. Enhanced Camera flipped that flag. **SSE deleted that
mechanism** and replaced it with an explicit camera *state machine* (`kFirstPerson`, `kThirdPerson`, `kFurniture`,
`kTween`, …). There is no flag to flip. That's why the straightforward port doesn't exist, and why the one successful
SSE mod, Improved Camera, took a different route: it drives the *third-person* camera into a first-person position.
The engine stays genuinely in third person — the state where the body already renders normally — while the view
looks and feels first-person. That elegantly sidesteps the body-culling problem (below): by never leaving third
person, the body is always in a state the engine draws.

Embody takes the opposite tack: it stays in the engine's actual first-person camera state and instead does two
things: **show the third-person body under it, and stop the engine from culling that body.** The second half is the
hard part and took most of the development effort.

---

## 2. Two hooks, no framework

Embody is deliberately framework-light: raw SKSE ABI, `-nostdlib`, links only `kernel32`/`user32`, no CommonLibSSE.
Everything is hand-rolled. It installs exactly **two call-site hooks** (plus two for effect shaders), by rewriting a
5-byte `E8 rel32` call to jump through a near-allocated `mov rax,<hook>; jmp rax` stub into our C wrapper, which calls
the original and then does our per-frame work. We hook *call sites inside* functions (not function entries) partly
because SKSE itself hooks some of the same entries — hooking an interior call site coexists cleanly.

- **`UpdateCamera` call @ `UpdateCamera+0x1A6`** → `Hook_UpdateCamera`. Runs every frame in every camera state.
  Does per-frame state tracking (reads the current camera `stateId`), the live tuning hotkeys, the "leave first
  person" cleanup, and the Tween-menu body-hold.
- **`UpdateFirstPerson` call @ `UpdateFirstPerson+0xD7`** → `Hook_UpdateFirstPerson`. This is the correctly-timed
  hook for body work — it fires late in the frame, after the engine has updated the skeleton, right before render.
  It runs the **body-ride**. (Bonus: the function this call targets *is* `NiAVObject::Update` (`0xD1BF70`), which we
  reuse to force-refresh the skeleton — see §4.)

The camera state id lives at `[[TESCamera + 0x28] + 0x18]` (current state → id). The states we care about:
`0 = kFirstPerson`, `5 = kFurniture`, `7 = kTween`, `9 = kThirdPerson` (full enum in the code and `START-HERE.md`).

---

## 3. The body-ride (`applyBodyRide`)

Every frame in first person, on the correctly-timed `UpdateFirstPerson` hook:

1. Get the player's **third-person body** — `Get3D(player)`, a `BSFadeNode`. (We validate its vtable so we never
   operate on a half-built skeleton mid-cast/equip/POV-swap — that was an early instant-CTD source.)
2. **Show it** — the engine hides it in first person; we clear its `kHidden` flag.
3. **Home it in the render graph** — the crown jewel, see §4.
4. **Hide the head** — scale the `NPC Head [Head]` bone to ~0 so you don't see the inside of your own skull.
5. **Weapon-gated arms** — if the weapon is *drawn*, show the hi-fidelity first-person arms and hide the body's
   own upper-arm bones; if *sheathed*, do the reverse (so the body isn't running around armless). Read from
   `ActorState.actorState2.weaponState`.
6. **Body scale** — multiply the engine's own root scale by the user's tunable factor (with rewrite-detection so
   it composes with RaceMenu/`setscale` and never compounds).
7. **Anchor** — move the body so it sits correctly under the camera. See §5.
8. **Refresh** — call `NiAVObject::Update` on the body so its transforms/bounds recompute this frame (the engine
   stops updating a "hidden" first-person body, so without this it goes stale and drops from the render — the
   original "body only shows while an animation is driving it, e.g. jumping" bug).

On leaving first person, `restoreThirdNode` undoes the head/arm/scale changes so third person is normal.

---

## 4. The render fix — why the body stopped vanishing (the crown jewel)

For a long time the body would **vanish** — on stairs, at room boundaries, mid-cast, on POV switches. Every
node-level lever we tried failed: clearing `kHidden`, forcing fade opaque, setting `kAlwaysDraw`, inflating the world
bound, un-hiding ancestors, per-frame re-registration. The body's node was, by every measurable property, "please
draw me" — and the engine refused.

The reason: **the renderer draws through the `ShadowSceneNode`, not the raw scene graph.** In an interior, its
`OnVisible` runs the portal/room culling: it hides every room node, then draws only the rooms visible through the
portals in the camera's frustum. **Room/portal culling applies only to nodes parented *inside* a room node** — and
the player's third-person body is parented to exactly such a room node (its parent tracks which room you're in, which
we confirmed with logging: the parent changes as you walk between rooms, not when you change POV). When you're on
stairs, the body's origin sits in a different room than the camera; turn away from the connecting portal and that room
drops from the visible set — and the body with it. None of our node flags could reach this, because the decision is
made *above* them, on room membership.

The fix came from decompiling `ShadowSceneNode::OnVisible` and the portal graph. There's a container the engine
itself keeps for dynamic objects that must always draw — the **`DynamicNode`**, a child of the portal graph that
`OnVisible` draws in its end-loop, *bypassing room culling entirely*. (The engine even sets `kAlwaysDraw` + forces
fade on a sibling special node before attaching it — i.e. Bethesda uses the exact trick we'd independently derived.)

So **Embody re-parents the body to `DynamicNode` every frame while in first person** (find the portal-graph list
entry named `"DynamicNode"`, `DetachChild` the body from its room, `AttachChild` it to `DynamicNode`). Now the body
faces only the per-node gates a `DynamicNode` child still sees — `kHidden`, the world-bound frustum test, and fade —
*all of which we already force every frame* (clear kHidden, inflate the bound to 1e9, set kAlwaysDraw, force fade
opaque). The result: the body **cannot** be room/portal-culled, with the true first-person camera fully intact.

This is the SSE-native expression of Enhanced Camera's idea, reached through the engine's real mechanism rather than
Oldrim's deleted POV flag. It is original to this port — Improved Camera solves the same problem a different way: by
keeping the engine in third person (its repositioned third-person camera), the body stays in a state the engine
already renders, so portal culling never comes into play. Two valid routes to the same result.

---

## 5. The anchor — putting the body in the right place, and keeping it there

The camera sits at the eyes; the body's head node is *behind and below* the eyes. So we can't just leave the body at
its physics position (it would appear too far forward, and you'd look down the inside of your own chest). The anchor
moves the body so a chosen node (the head, by default) sits at the camera, then applies a **per-stance trim** to nudge
it into a natural spot.

Key design points, all learned the hard way:

- **Body-to-camera, not camera-to-body.** We write the *body's* local transform; we never move the camera — so
  Embody adds no camera motion of its own, and the first-person feel stays exactly vanilla.
- **Per-stance trims.** The right offset differs by stance, so there's a 4-entry trim table:
  {standing, sneaking} × {sheathed, drawn}, each with (forward, lateral, up). Tunable live (see README controls).
- **Gimbal-free horizontal math.** The forward/lateral axes are built from the camera's **right vector** (the X
  column of the camera rotation). The right vector's horizontal component stays a stable unit vector at *every*
  pitch — pitch rotates *around* it — so there is no singularity when you look straight down, and no trig or
  normalization hacks. (An earlier version built the axes from the forward vector and hit a gimbal singularity at
  full pitch: yawing while staring at the floor made the body swing around the screen. The right-vector formulation
  removed the whole class of bug.)
- **Yaw-clamp.** Skyrim reuses the forward run animation for strafing by *rotating the model* to face movement — fine
  in third person, but in first person the body visibly swings sideways under the camera. We pin the body's yaw to a
  pure-yaw upright matrix built from the camera basis, so the body faces where you face (the legs then do the normal
  first-person-shooter "slide sideways while running forward," same as every FPS and as the vanilla arms). It's
  written as a *clamp* (deviation allowed up to a cap; cap 0 = hard pin) so a future MCM knob can expose it.

---

## 6. Version independence — surviving Bethesda updates

Embody hardcodes **no** engine addresses. At load it reads the **Address Library** versionlib
(`Data/SKSE/Plugins/versionlib-<maj>-<min>-<build>-0.bin`, format 2), and resolves a small table of stable
**RelocationIDs** to that game version's current RVAs. RelocationIDs are constant across game versions by design; only
the RVA behind each one moves. So when Bethesda ships an update that merely shifts addresses, Embody keeps working the
moment the Address Library publishes its new database — no recompile.

The parser (`vlibResolve`) and the file locator (`loadVersionLib`, which builds the exact filename from the SKSE
runtime version with a glob fallback) live in `Embody.cpp`. A standalone test of the parser is in
`scratchpad/test_vlib.c` — if a game update lands, verify the parser against the new bin there first.

**What is *not* covered by the Address Library:** (a) **struct offsets** (`+0xF4` flags, `+0x100` fade, etc.) — these
are stable across minor versions but a major update could change them; (b) **interior call-site offsets** — we hook a
`call` at, e.g., `UpdateCamera+0x1A6`, and if a future build restructures that function the offset could move. Both
are the manual-verification surface on a major update. Crucially, the hooks **verify their target bytes and bail
safely on a mismatch** (an `E8` where we expect an `E8`, the exact `FF 90 <disp>` for the vtable-call site), so a
breaking update degrades to "the body doesn't appear" — never a crash. This is also why GOG *probably* just works.

To add a feature that needs a new engine address: add its RelocationID to the `IDS[]` table in `SKSEPlugin_Load` and a
matching enum entry — never write a raw RVA.

---

## 7. Camera states — what's handled, what steps aside

Embody only *acts* in states where the camera is genuinely first-person: `kFirstPerson (0)` and the `kTween (7)` menu
(TAB — see §8). In every other state — `kThirdPerson (9)`, `kVATS (2)` killmoves, `kFurniture (5)` sitting/crafting,
mounted, transforms — it does nothing, `restoreThirdNode` puts the body back to normal, and you get **stock Skyrim.**
This is deliberate and important: the mod is *non-breaking* everywhere it doesn't help, and it **never steals the
camera from another mod** (killmoves stay third-person, so Violens/VATS-cam setups are untouched by default).

"Enhanced Camera parity plus" — *forcing* first person in some of those states (first-person sitting, crafting,
mounted riding) — is planned as an **additive, opt-in** feature set exposed through an MCM, each toggle mapping to one
camera state. Because the current behavior in those states is safe vanilla, these can be layered on without
destabilizing the base.

---

## 8. The Tween menu (TAB) special case

TAB opens the **Tween menu** (the quick radial). Uniquely among menus, it (a) keeps the game world rendering with the
body visible, and (b) enters its own camera state (`kTween = 7`) which *freezes* `UpdateFirstPerson`. So our body-ride
stops and the body snaps to its physics position — a visible forward "lunge" that no other menu shows (the others
fully occlude the world). Embody treats `kTween` as a body-visible state: it does not restore the body on entering it,
and re-pins the body from the still-ticking `UpdateCamera` hook using a **frozen snapshot** of the last first-person
transform (so it doesn't read the tween's mid-zoom camera). The body holds correctly for the whole menu. A residual
1-frame flicker at the open/close boundary remains — the engine repositions the body on the single transition frame
at a point after our hook, and the correctly-timed hook is frozen there — and is documented as an accepted minor.

---

## 9. Effect shaders on the body

Magic hit-shaders (Stoneflesh's teal, Muffle's shimmer) attach to whichever model the engine considers "active." The
engine picks by reading `PlayerCharacter.playerFlags.isInThirdPersonMode` (`+0xBE3` bit 0). We hook the two call sites
inside `ShaderReferenceEffect::Update` and, while sheathed in first person, briefly raise that flag so the shader
lands on the visible body; drawn, we leave it so the visible hi-fi arms get it (Improved Camera parity). One shader
instance targets one model, so it's arms-or-body per weapon state, not both — an engine limitation, not a bug.

---

## 10. Where the bodies are buried (gotchas for maintainers)

- **SKSE loads plugins once, at launch.** You must fully quit and relaunch to test a new build. Confirm the running
  build from the log's load line / process start time vs. dll mtime. (This cost hours during development.)
- **CommonLibSSE offset annotations are SSE-vs-VR in places, not SE-vs-AE.** The AE offset is not always the "second
  number." We were burned by this on `ActorState` (0xC4 vs 0xCC) and `BSFadeNode.currentFade` (the live gate is
  `+0x130`, not the `+0x158` a naive read suggests). Verify offsets against the decompiled function that *uses* them.
- **The DynamicNode steal runs on the `UpdateFirstPerson` hook, which fires on a worker thread** (log interleaving
  revealed this). It's been stable, but scene-graph surgery on a worker thread is a latent hazard; a planned hardening
  moves it onto the main-thread `UpdateCamera` hook.
- **`isNode()` gates every recursive tree walk** (via the `AsNode()` vtable slot). This is what stopped the
  instant-CTD-on-spellcast: spell cast-art and particle nodes aren't `NiNode`s, and reading their memory as a
  children array crashes. Never recurse a scene subtree without it.

Full night-by-night development history, including every approach that *didn't* work and why, is in the project
memory file referenced by `START-HERE.md`. The raw Ghidra decompilations that cracked the render fix are
`ghidra_render.txt`, `ghidra_cull.txt`, `ghidra_gate.txt`, and `RENDER-mechanism.md`.
