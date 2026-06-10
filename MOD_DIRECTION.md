# MOD_DIRECTION.md

**Author:** Linus (acting maintainer, per Tony's verbatim "He's in charge")
**Date:** 2026-05-06
**Status:** Prescription. Tony reads, executes. Course-correct only on
new technical evidence, not on opinion.

**One-line direction:** Stop architecting. Strip the plugin to its
known-working core, ship a hot-fix that removes the shipping UB, then
**measure** what the stripped baseline actually delivers in user-visible
behavior before deciding whether any further hooks are needed.

This is **option (d)** in the dispatcher's framing — measure before
architecting — and it dominates (a)/(b)/(c) for reasons in §2.

---

## §1. Decision

**Adopt option (d): "Measure-the-baseline-first."**

- (a) Send back to architect for redesign — **rejected.** The architect's
  evidence-handling discipline (sloppy disasm transcription per §B-2/B-3
  of the critique, undone RE work labeled as confirmed per §A-1, the
  whole `ENGINE_GAMEPAD_ACTIVE` keystone hypothesis being invented per
  §B-1) means iteration produces a more-polished version of the same
  wishful thinking. The redesigned doc would still be predictions about
  user-visible behavior with no measurements behind them. We've been
  here once. We don't need a second draft, we need **evidence**.

- (b) Pivot to a new strategy I write — **rejected.** I have one
  reviewer's worth of binary knowledge, not an engineer's. Writing a
  competing architecture from this position would put Tony in the same
  hole he's in now: trusting one person's prediction of how 1.16.236
  responds to a hook, with no measurement to check it. The same trap
  the v2.0 doc fell into.

- (c) Back-burner the mod — **rejected.** Hook 1 (LookHandler vtable
  shim) verifiably installs on 1.16.236. Whether it delivers user value
  has not been measured. Quitting before the experiment runs is
  premature.

- (d) **Measure the baseline first** — **adopted.** Strip the plugin to
  hook 1 + hook 2 (both byte sites). Ship that. Let Tony actually play
  on anthony-gaming with the stripped build. **What he experiences
  determines what we build next.** This dominates (a)/(b)/(c) because:

  1. It **removes the shipping UB** in v1.3.0 immediately (§5).
  2. It **produces falsifiable evidence** about what 1.16.236 actually
     does — replacing the v2.0 doc's "predicted from architecture
     model" with "observed in gameplay."
  3. Each follow-up hook from the baseline test is **scoped and earned**
     — "user reports glyph flips → find the writer" not "hypothesize a
     flag and patch around it."
  4. It dodges the months-of-RE rabbit hole the v2.0 plan implied for
     R1-R4 (§8.5 of the architecture doc), 75% of which is now known to
     be predicated on a wrong hypothesis (§B-1 of critique).
  5. **If hook 1+2 deliver the user value, the project ends.** This is
     the cheapest possible win and we owe it to ourselves to find out
     whether the win is already in hand.

The rest of this doc is the spec for executing (d).

---

## §2. Why (d) and not (a)/(b)/(c) — the technical case

The v2.0 architecture doc made one structural error and the entire
strategy inherited it: **it never separated "what the working hook
already delivers" from "what additional hooks would add."** §2.3 of the
architecture doc says v1.3.0 ships "with no working sensitivity /
quadrant / reticle / cursor-confine fixes, which matches Tony's lived
experience on the gaming machine." That's a guess about which user-
visible failures map to which broken hooks — and it's a guess made
without a controlled test where only hook 1 is in play.

The known facts about 1.16.236 today:

| Fact | Source | Implication |
|---|---|---|
| Hook 1 (LookHandler vtable slot 1 shim) installs cleanly | [Offset.Ext.h:117](src/RE/Offset.Ext.h:117) verified against libxse | Hook 1 IS running on anthony-gaming. We don't know what it delivers because we never measured. |
| Hook 2 patches first `C6 43 08 01` site only, missing second site at +0x5DC | [SFSEPlugin.cpp:275](src/export/SFSEPlugin.cpp:275) verified, second anchor confirmed in binary | Left-stick still flips active-device through the second site. Trivially fixable. |
| Hooks 3-7 redirect E8 calls to `IsUsingThumbstickLook` (a function returning a static bool) | [SFSEPlugin.cpp:306-330](src/export/SFSEPlugin.cpp:306) | Safe (no UB), but redirecting unknown call sites to return a fixed bool has unknown user-visible effect. |
| Hooks 8/9 install on 1.16.236 (E8 byte present at +0xA1 and +0x4CE) and call `IsGamepadCursor` → `IsUsingGamepad(BSInputDeviceManager*)` → AL 139340 with wrong argument type | Verified by direct disasm — RVAs `0x37d3291` and `0x37c3fce` both contain `E8` calls to `0x2c4b50` | **Shipping UB.** Cursor visibility/style decisions made on a meaningless boolean every time stick-look is active. |
| `0x05f67820` ("ENGINE_GAMEPAD_ACTIVE") has 37 readers, 0 writers, byte = 0x10, sits inside a function-pointer table | Capstone scan during critique | The keystone hypothesis of v2.0 is wrong. Strategy (b) is undefined. |
| `BSInputDeviceManager` vtable[0] slot 1 returns true only for `eventType ∈ {kDeviceConnect, kKinect}` | Verified: 12-byte function at RVA `0x22da380` | The manager listens for device-connect events, but the **writer to whatever flag flips device-active** is not on this slot. The writer location is unknown. |
| `LookHandler::OnThumbstickEvent` (slot 4) and `OnMouseMoveEvent` (slot 6) exist at RVAs `0x12bcc30` and `0x12bcce0` | libxse vtable AL 433589, verified | These already receive both stick and mouse events — the engine's per-device-class handlers are intact. Whether they're being invoked depends on whether the engine's gating allows them through. |

Everything we know that's empirically reliable about 1.16.236 says: **the
LookHandler vtable shim alone may already deliver invariant 2 (mouse +
stick coexist at the look-event layer)**, because `OnThumbstickEvent` and
`OnMouseMoveEvent` both exist as separate vtable slots and the shim
unconditionally returns true from `ShouldHandleEvent`, which means the
engine routes both event types through their respective slots.

Whether glyphs ALSO stay stable, and whether the cursor stays in the
window, is what the baseline test will answer. Speculating about it is
exactly the trap the v2.0 doc fell into.

---

## §3. The minimal v1.4.0 spec ("baseline build")

This is the build to ship to anthony-gaming for the baseline measurement.

### 3.1 What's in

- **Hook 1**: LookHandler vtable[0] slot 1 shim, exactly as
  [SFSEPlugin.cpp:231-257](src/export/SFSEPlugin.cpp:231) implements it
  today. **One change required:** replace the captureless lambda with a
  shim that **first calls through to the original slot 1 implementation,
  then applies the latch**. Rationale: §F-4 of the critique. The
  original `ShouldHandleEvent` may have non-trivial side effects we are
  silently disabling.
- **Hook 2**: `BSPCGamepadDevice::Poll` byte patch, but **patch every
  `C6 43 08 01` match in the bounded scan**, not just the first.
  Rationale: §B-7 of critique confirms two anchor sites at +0x51D and
  +0x5DC; current code stops at the first.

### 3.2 What's out (deleted, not commented out)

- Hook 3 (`LookHandler::Func10`)
- Hook 4 (`Manager::ProcessLookInput`)
- Hook 5 (already deleted, stays out)
- Hook 6 (`ShipHudDataModel::PerformInputProcessing` +0x2C7)
- Hook 7 (`ShipHudDataModel::PerformInputProcessing` +0x2E4)
- Hook 8 (`IMenu::ShowCursor` +0xA1) — **removes the shipping UB**
- Hook 9 (`UI::SetCursorStyle` +0x4CE) — **removes the shipping UB**
- The `IsUsingGamepad`, `IsUsingThumbstickLook`, and `IsGamepadCursor`
  free functions in [SFSEPlugin.cpp:84-116](src/export/SFSEPlugin.cpp:84)
  — none are needed once hooks 3-9 are gone.
- The `RE::Offset::BSInputDeviceManager::IsUsingGamepad` constant at
  [Offset.Ext.h:42](src/RE/Offset.Ext.h:42) — the shipping
  type-mismatch source. Delete.
- All `RE::Offset` namespaces referenced only by deleted hooks:
  `Main::Run_WindowsMessageLoop`, `PlayerControls::LookHandler::Func10`,
  `PlayerControls::Manager::ProcessLookInput`,
  `ShipHudDataModel::PerformInputProcessing`, `IMenu::ShowCursor`,
  `UI::SetCursorStyle`. Keep `LookHandler::Vtbl` and
  `BSPCGamepadDevice::Poll` only.
- Trampoline budget: drop `trampolineSize = 32` to whatever the SFSE
  default is, or drop `.trampoline = true` entirely (vtable shim and
  byte patch don't need the SFSE trampoline allocator).

### 3.3 What's added

- **PE-integrity gate** at plugin load. Compute SHA-256 of the running
  Starfield.exe main module's first MB (or whatever's cheap and stable),
  compare to a hardcoded list of approved hashes for 1.16.236 builds. On
  mismatch: refuse to install hooks, log loud WARN, return true from
  `SFSEPlugin_Load` so the user still gets logs.
- **AL DB version gate**: query the AL DB game version (already parsed
  in the existing plumbing) and refuse to install if it ≠ runtime version.
- **Hook-1 invocation counter**: atomic `uint64_t` incremented inside
  the shim; logged on `SFSEPlugin_Load` exit and (if SFSE provides a
  shutdown callback) on shutdown. This lets a "did the hook actually
  fire?" question be answered from the log alone.
- **Atomic `UsingThumbstickLook`**: replace the static bool with
  `std::atomic<bool>` using `memory_order_relaxed`. Per §F-3 of critique.

### 3.4 Code budget

This is ~50 lines of source changes plus the SHA-256/AL-DB-version
plumbing. It is **not** a rewrite. The current SFSEPlugin.cpp can be
edited in-place to delete the dead hooks and adjust hook 2's loop.

### 3.5 Branding / versioning

Bump to **v1.4.0**, NOT v2.0. v2.0 is reserved for "we've measured the
baseline, identified the actual gaps, designed a real fix, and verified
it." v1.4.0 is "we removed the shipping bug and shipped the
known-working core." Honest versioning matters when future-Tony reads
this in 2027.

---

## §4. The baseline test (the actual experiment)

This replaces §9 of the v2.0 doc, which was 12 rows of subjective
observations. The baseline test has THREE parts:

### 4.1 The 60-second canonical golden-path

Tony runs this on anthony-gaming after each install. Pass = all four
conditions hold. Fail at any step = log the observation, do NOT proceed
to the next-phase work.

1. Load a fresh save in a quiet area (no combat, no menu open).
2. Holding controller in left hand AND with right hand on mouse, look
   around for 30 seconds using **both inputs continuously**. Move
   right-stick AND mouse simultaneously. Tony reports: did the camera
   respond to **both**? Did the on-screen prompt glyph flip even once?
3. Press B on controller. Tony reports: did the ✕/B prompt appear?
4. Move OS cursor toward the second monitor (anthony-gaming has one).
   Tony reports: did the cursor escape the window?

This takes 60 seconds. Record it (OBS, phone camera, anything). The
recording is the artifact.

### 4.2 The quantitative additive-look measurement

This addresses §C-2 of critique ("camera nudges" is not falsifiable).
Stricter test:

1. Load same save. Note the look-yaw direction (looking due-X, e.g. at
   a wall edge).
2. Hold right-stick fully right for 5 seconds. Tony reports yaw delta
   (degrees) by visually estimating against landmarks. Call this
   `yaw_stick`.
3. Reset to original yaw. Move mouse right at a constant rate for 5
   seconds (mouse over a ruler, e.g. 10 cm). Tony reports `yaw_mouse`.
4. Reset to original yaw. Hold stick fully right AND move mouse right
   at the same rate for 5 seconds simultaneously. Tony reports
   `yaw_both`.
5. **PASS criterion: `yaw_both ≈ yaw_stick + yaw_mouse`** (within 30%
   tolerance — eyeballed, this is not a physics lab). FAIL: `yaw_both
   ≈ max(yaw_stick, yaw_mouse)` (substitution, not addition) or
   `yaw_both ≈ yaw_stick` (mouse ignored) or `yaw_both ≈ yaw_mouse`
   (stick ignored).

This is the test that distinguishes "hook 1 actually delivers invariant
2" from "the player saw the camera move and assumed it worked."

### 4.3 The cockpit reticle test

1. Enter ship cockpit, undock if needed, head into open space.
2. Hold the trigger to fire weapon. Note reticle position relative to
   crosshair center.
3. While still firing, move mouse right 5 cm. Tony reports: did the
   reticle drift right? By roughly how much?
4. PASS: reticle responds to mouse. FAIL: reticle locked to stick input
   only.

### 4.4 What the test outcome means

| Test result | Conclusion | Phase 2 action |
|---|---|---|
| 4.1 all PASS, 4.2 PASS, 4.3 PASS | Hook 1+2 deliver everything Tony needs. | **Ship as v2.0 final. Project complete.** |
| 4.1 #2 FAILS (glyphs flip) | Engine writes to a glyph-driving flag from a path hook 2 doesn't catch. | Phase 2-A: targeted RE to find the writer. Use **runtime instrumentation**, not static-disasm hypothesis. |
| 4.1 #4 FAILS (cursor escapes) | The ClipCursor confine logic at `0x189fadf` is taken on a code path our patches don't gate. | Phase 2-B: implement the IAT `ClipCursor` hook in isolation (per critique §E-2 with the open issues addressed). |
| 4.2 FAILS (substitution, not addition) | The engine's per-device handlers are merging at a layer we haven't owned. | Phase 2-C: targeted look-pipeline RE; this is the hardest of the three to fix. |
| 4.3 FAILS (reticle stuck on stick) | ShipHud reticle code reads device state from somewhere our hooks don't touch. | Phase 2-D: targeted ShipHud RE. |

Each Phase 2-X is independent of the others. Each is scoped to a
specific user-observable failure with a specific RE target. None
requires the grand unified `ENGINE_GAMEPAD_ACTIVE` story the v2.0 doc
sold.

**This is the key shift:** the v2.0 doc tried to deliver every invariant
through one architectural move. The baseline-first plan delivers
invariants **independently as needed**, which is what the engine's
1.16.236 shape (per the critique) actually rewards.

---

## §5. The shipped v1.3.0 bug — what to do right now

**Diagnosis (verified during this direction-setting):** v1.3.0 deployed
to anthony-gaming has hooks 8 and 9 actually installed. Both
`IMenu::ShowCursor +0xA1` (RVA `0x37d3291`) and `UI::SetCursorStyle
+0x4CE` (RVA `0x37c3fce`) contain `E8` calls in the binary, so the
TryWriteCall sanity check passes and the redirect installs. Each redirect
points at `IsGamepadCursor`, which when `UsingThumbstickLook == true`
calls `IsUsingGamepad(BSInputDeviceManager*)` which calls AL 139340
with a `BSInputDeviceManager*` argument. AL 139340 (per critique §B-1
and the v2.0 doc's own §2.3) reads `[arg + 0x10]` as if it were
`InputEvent::eventType`. With a `BSInputDeviceManager*` it reads
whatever happens to live at offset 0x10 of the singleton.

**Severity:** the call doesn't crash the game (the singleton has SOME
byte at +0x10), but every cursor-visibility decision and every
cursor-style decision while stick-look is active is made on garbage.
The user-visible symptom is "cursor visibility/style is sometimes
wrong" — exactly the kind of thing Tony has been seeing.

**Recommendation: hot-fix to v1.4.0 (the baseline build) immediately.**

Don't roll back to v1.2.0. v1.2.0 didn't have the new derived offsets
either; it was just broken differently. Don't ship a v1.3.1 that *only*
fixes the UB and leaves hooks 3-7 in their current "redirect to a
constant bool" state. The baseline build (§3) is the right destination
because:

1. v1.4.0 is small enough to land in days, not weeks.
2. The same drop that removes the UB also removes the unmeasured-effect
   hooks 3-7 — giving Tony a clean baseline test.
3. Shipping a v1.3.1 that just deletes hooks 8/9 leaves Tony with the
   "what does hook 3-7 actually do?" question and no plan to answer it.
   We'd just have to ship v1.4.0 a week later anyway.

**Operational rule:** anthony-gaming should not run v1.3.0 in a
playthrough that matters. If Tony plays Starfield this week and v1.4.0
isn't ready, **uninstall the plugin** rather than run with the UB. The
default Bethesda behavior (cursor flips, glyphs flap) is annoying but
deterministic; the v1.3.0 behavior is annoying AND non-deterministic.
Determinism beats non-determinism for live gameplay.

---

## §6. Verification gate going forward — "Linus says ship it"

Before any DLL is built and deployed to anthony-gaming, the following
must hold. This is not negotiable; the whole point of this exercise is
to stop shipping on hope.

1. **Every cited RVA in any new architecture or change brief is backed
   by a capstone disassembly excerpt I can re-run in 30 seconds.** Doc
   says "RVA 0xABCDEF is `BSInputDeviceManager::Foo`"? Doc gives me a
   one-liner: `python3 /private/tmp/sf_re/probe.py disasm 0xABCDEF 32`,
   I run it, the output matches what the doc claims. If it doesn't
   match, the change goes back.

2. **Every hook documents its IF-WRONG failure mode** in one of:
   *no-op*, *crash*, *silently-corrupted-state*, *user-visible-glitch*.
   No "should work" / "likely fine" / "expected to". If you can't
   classify the failure mode, you don't understand the hook well enough
   to ship it.

3. **No undefined behavior shipping.** No type-mismatched function
   calls, no out-of-bounds reads, no static-bool reads from random
   threads, no vtable rewrites that drop the original implementation
   without a documented justification.

4. **PE-integrity check is in place** (§3.3). Plugin refuses to install
   on a Starfield.exe whose SHA-256 doesn't match the approved list.

5. **AL DB version check is in place** (§3.3). Plugin refuses to install
   if `versionlib-{X}-0.bin` game-version field ≠ Starfield.exe runtime
   version.

6. **At least one quantitative test in the test plan**, not all
   subjective observations. (For v1.4.0, that's §4.2.)

7. **The 60-second canonical golden-path test (§4.1) has been run
   on anthony-gaming and a recording exists.** "Tony said it worked" is
   not a recording. The recording is the artifact that proves the test
   ran.

8. **The plugin log alone is sufficient to triage a user report.** Hook
   install/skip with reasons, hook invocation counters, version info
   (Starfield, SFSE, AL DB, plugin, plugin SHA), PE/DB integrity check
   results. No "attach a debugger to find out" answers.

9. **For any hook that lives across patch versions, an explicit
   "version-N+1 will require re-derivation of X, Y, Z" note is present**
   so future-Tony or future-Linus knows what re-RE work the next runtime
   patch will demand.

If a change satisfies all 9, "Linus says ship it" — the patch goes to
anthony-gaming. If it doesn't, send back with the unmet item(s) named.

---

## §7. What I personally commit to verify before approving each ship

For v1.4.0:
- I will independently verify the Hook 1 vtable shim install path is
  byte-correct against the actual `LookHandler` vtable[0] in
  `Starfield.exe` at the time of the build (re-run the slot-resolution
  probe).
- I will independently verify Hook 2 patches BOTH `C6 43 08 01` sites
  by reading the patched binary post-install (or by inspecting the
  plugin's log line for "patched 2 of 2 anchor sites").
- I will independently verify the SHA-256 / AL DB version gates fire
  when given a deliberately-wrong PE.
- I will read the proposed code change in its entirety before approving.
  No "looks fine" without reading.

For each subsequent Phase 2-X follow-up:
- I will independently verify the disasm evidence supporting the new
  hook (whether the new hook target's bytes match what the change brief
  claims).
- I will independently verify the IF-WRONG failure mode by simulating
  it (e.g. forcing the predicate to return the wrong bool, observing
  what breaks).
- I will demand and review the recording from the §4.x test that
  motivated the new hook.

If a future change comes in that I can't verify against the binary
(because the binary or AL DB has moved), I will say so, and the change
does not ship until the verification path is clear. **No "trust me, I
checked" between the architect and the binary.**

---

## §8. What this means for Tony, in plain English

- **Stop using v1.3.0 for real play.** It has a bug that makes cursor
  behavior random when you're using a controller for camera. If you're
  going to play this week, uninstall the plugin until v1.4.0 ships.
- **v1.4.0 lands soon.** It's a small change. Maybe 50 lines of code,
  plus the integrity-check plumbing. A working SFSE-side dev could do
  it in an afternoon.
- **After v1.4.0 lands, you do the §4 tests** — golden-path, additive-
  look, cockpit reticle. Record them. Send the recordings.
- **Based on those recordings**, we either declare victory and ship as
  v2.0, or we open ONE Phase 2 follow-up that's targeted at the
  specific thing that didn't work. Not five. One at a time.
- **There is no "grand v2.0 architecture rewrite."** That plan is dead.
  The keystone hypothesis was wrong and chasing it would burn months.
  We do small, evidence-driven steps now.
- **If after Phase 2-A, 2-B, etc. the engine still won't behave**, we
  have option (c) waiting: tell users "play 1.8.86 with Parapets's
  original mod, or accept Starfield's default broken behavior on
  current runtime." That's a perfectly honest answer if the engine
  genuinely can't be coerced. But we don't know that yet, because we
  haven't measured.

The whole exercise is: **stop predicting, start measuring**. Predictions
without measurements are how v1.3.0 ended up shipping a UB bug while
its log said "all hooks installed."

---

## §9. Open questions I am NOT closing in this prescription

These are honest unknowns. Surfaced so they don't become silent assumptions.

- **Whether anthony-gaming's specific hardware setup (Steam Deck-class
  controller? Wooting? plain XInput pad?) affects baseline behavior.**
  The §4 tests should be run with whatever Tony actually plays with.
- **Whether SFSE plugin load order matters** with whatever else Tony has
  installed (any community input mods, gameplay mods that hook input).
  v1.4.0's PE-integrity-check should also list "loaded SFSE plugins" in
  the log so we can see if there are collisions.
- **Whether `ClipCursor` is the only Win32 cursor-confine mechanism in
  play.** Per critique §E-2. If §4.1 #4 fails AFTER an IAT ClipCursor
  hook is in place, `SetCapture` / raw-input is the next suspect.
- **Whether `LookHandler::ShouldHandleEvent`'s original implementation
  has side effects we'll lose by replacing-not-chaining.** §3.1 says
  "chain through the original" but until we look at what the original
  does on 1.16.236, we don't know what behavior depends on it. Risk:
  low (the per-device handlers in slots 4 and 6 do the actual work);
  worth verifying.

These are not blockers for v1.4.0 — they're things to watch for in the
test results.

---

**End of prescription. Tony reads, executes. Linus stays on the
critique side of the wall going forward — every patch from here meets
the §6 gates or doesn't ship.**
