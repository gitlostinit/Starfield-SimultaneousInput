# Maintaining SimultaneousInput across Starfield patches

Whenever Bethesda ships a Starfield patch, the typical failure mode is one of:

1. SFSE itself isn't compatible with the new runtime yet. Wait for SFSE.
2. SFSE updated, but the plugin's declared layout bit doesn't match what
   SFSE expects. Symptom: SFSE Plugin Loader logs
   `SimultaneousInput.dll: disabled, incompatible with current version`.
3. SFSE accepts the plugin, but one or more Address Library IDs now point
   at a refactored function. Symptom: `hook ... skipped` lines in
   `SimultaneousInput.log`, and the corresponding behavior is broken
   in-game (look sensitivity wrong, cursor stuck, reticle off, etc.).

This document is the recipe for diagnosing and fixing each.

## 1. Check SFSE first

Open `sfse.log` after launching the game once. The header lines say which
runtime SFSE supports and which runtime it observed. If SFSE itself rejects
the runtime, there is nothing to do here; wait for an SFSE update.

## 2. Layout-bit mismatch (recompile only)

If `sfse.log` says "disabled, incompatible with current version" specifically
for `SimultaneousInput.dll`, the layout bit in the plugin's
`SFSEPluginVersionData` doesn't match what SFSE wants. The fix is almost
always: rebuild against the current `CommonLibSF` HEAD, which gets the new
bit definition automatically.

Steps:

```bash
cd external/CommonLibSF
git fetch origin
git checkout origin/main
cd ../..
git add external/CommonLibSF
git commit -m "Bump CommonLibSF to <short-sha>"
git push
```

Watch the `build` workflow in GitHub Actions. Download the artifact and
deploy. If SFSE still rejects, check whether CommonLibSF's
`SFSE/Interfaces.h` updated its `IsLayoutDependent` bit comment ("1 << N is
for runtime ..."); if N changed, the plugin metadata setter call already
follows it. No code change needed.

## 3. AL ID refactoring (the real work)

When `SimultaneousInput.log` shows `hook 'XYZ' skipped: AL id 12345 +0xNN
did not start with E8 (call). function may have been refactored on this
runtime.`, that hook is stale.

To investigate:

1. Get a copy of `versionlib-1-15-216.bin` (or whatever runtime applies)
   from the Address Library mod's release page on Nexus.
2. Convert it to a CSV with one of the public tools, or use the PDB for
   that runtime if you have access.
3. Locate the function the AL ID used to point at. Cross-reference its
   name in CommonLibSF source or Ghidra.
4. Find the new ID for the same function in the current runtime's AL
   database (search by demangled name or by signature).
5. Update the corresponding `constexpr REL::ID` in `src/RE/Offset.Ext.h`,
   leaving a comment explaining why the ID changed and what runtime that
   applies to.
6. Verify the byte-pattern guard (e.g. `REL::Pattern<"E8">`) still matches
   at the new offset. If not, the function may have been rewritten in a
   way that changes the call shape (inlined, devirtualized, etc.) and a
   different patch site is needed. That's a Ghidra job; out of scope for
   a routine maintenance pass.

Reverse-engineering steps requiring Ghidra are explicitly out of scope
for the per-patch loop. If a hook reaches that point, surface the affected
function name in an issue or commit message and let a human take it.

## 4. Adding new hooks

If a future patch breaks a behavior we don't currently hook (e.g. some new
input pipeline), the pattern is:

1. Add a `constexpr REL::ID` in `src/RE/Offset.Ext.h` with a header comment
   explaining what the function does and why we hook it.
2. Add a `TryWriteCall<5>(...)` (or `safe_fill` / `write_vfunc`) call in
   `src/export/SFSEPlugin.cpp` inside `SFSEPlugin_Load`. Each hook should
   be guarded so a single failure logs and skips, never crashes the host.
3. If the new hook is a trampoline call, bump `SFSE::AllocTrampoline(N)`.
   The current 28 bytes covers 7 hooks at 5 bytes each (with reuse).
4. Bump the plugin version in `CMakeLists.txt` (`project(... VERSION x.y.z)`).
   Patch bump for additive hook fixes; minor for new hook coverage; major
   for any change in metadata or interface.
5. Update the compatibility matrix row in `README.md`.

## 5. Triggering a CI build manually

The workflow runs on every push, so the normal flow is just `git push`.
For a manual rerun without a code change, use `workflow_dispatch` from
the Actions tab on GitHub. The artifact is named
`SimultaneousInput-<sha>` and contains the DLL + PDB.

## 6. Derivation tooling

`tools/al_db_parser.py` is a self-contained Python module that parses the
Address Library v5 binary (the same `versionlib-*.bin` SFSE loads at runtime)
and the Starfield PE. It supports forward and reverse RVA lookup, function
end heuristics anchored on neighboring AL IDs, simple thunk following, and
E8 call enumeration. Run it with `--probe` against a local copy of the runtime
to dump per-hook diagnostics:

```
pip install --break-system-packages lief
python3 tools/al_db_parser.py \
    --db /path/to/versionlib-1-16-236-0.bin \
    --exe /path/to/Starfield.exe \
    --probe
```

`tools/derive_function_ids.ps1` is the older PowerShell variant. Both target
format-5 ALDBs; v0/v1/v2 (legacy AE-style) are out of scope.

## 7. Status against Starfield 1.16.236

A full re-derivation pass against 1.16.236 produced the following finding:
of the 9 hooks, only the LookHandler vtable shim is cleanly portable.

| Hook | AL ID | 1.16.236 status |
|---|---|---|
| LookHandler vtable shim (slot 1)         | 433589 | works (vtable layout preserved, ID re-anchored from libxse `IDs_VTABLE.h`) |
| BSPCGamepadDevice::Poll byte patch +0x2A0 | 179249 | `C6 43 08 01` pattern absent; Poll body fully refactored |
| LookHandler::Func10 E8 +0x0E              | 129152 | function exists (0x250 bytes) but no E8 at +0x0E; 13 E8 sites within body, none with predicate-shaped target |
| Manager::ProcessLookInput E8 +0x68        | 129407 | thunk; real entry is alid 129441; nearest E8 at +0x6B targets a 0x1B0-byte vector function, not a predicate |
| Main::Run_WindowsMessageLoop E8 +0x39     | 149028 | function exists (0x250 bytes); 27 E8 sites; no clear predicate analog |
| ShipHudDataModel::PerformInputProcessing E8 +0x7AF / +0x82A | 137087 | function shrank to 0x50 bytes; uses `ff 50 40` indirect vfunc dispatch instead of E8 calls |
| IMenu::ShowCursor E8 +0x14                | 187256 | function exists (0x80 bytes); single E8 at +0x1E targets a struct-cleanup function, not IsGamepadCursor |
| UI::SetCursorStyle E8 +0x98               | 187051 | function exists (0x20 bytes); single E8 at +0x15 targets a 0x2a8-sized notifier, not a style chooser |
| BSInputDeviceManager::IsUsingGamepad      | 178879 | now resolves to a spdlog-style logging stub, not a callable predicate |

The 8 call-replacement hooks fail safely (the runtime probe checks for `E8`
at the documented offset before writing). `SimultaneousInput.log` will show
"hook ... skipped" for each.

Restoring the call-replacement hooks for 1.16.236 requires a Ghidra-grade
pass: identify the analogous code paths (Bethesda likely inlined or moved
the device-active check), then either choose a new patch site or restructure
the mod's hook strategy. That work is out of scope for a per-patch
maintenance pass and is tracked separately.

## 8. Starfield 1.16.242 — measured substitution point and the v1.5.0 force path

Context recovered 2026-06-09/10. SFSE never loaded on anthony-gaming before
2026-06-09 because `sfse_loader.exe` was a renamed copy of `Starfield.exe`
(found and repaired in the OpenClaw `#gaming` session; backups under
`MateoBackup-SFSE-20260609-172801`). Every prior "the mod does nothing"
observation predating that repair is void: the plugin was never in the
process.

### 8.1 What the v1.4.0 measurement proved on 1.16.242

The v1.4.0 baseline build (commit 5e2bae0, built by CI 2026-05-07) installs
both hooks cleanly on 1.16.242 — the vtable layout and both `C6 43 08 01`
Poll anchors (+0x51D, +0x5DC) survived the patch. A 17-minute session
(2026-06-09, 57,585 events, `measurements/20260609/`) showed:

- Both device classes flow through the shim simultaneously (382 of 550
  half-second windows contained both stick and mouse events).
- The original `ShouldHandleEvent` NEVER accepted both classes in the same
  engine tick (0 of 10,315 active ticks). Thumbstick acceptance was 15-18%
  overall; mouse ~51%. Substitution, not addition — MOD_DIRECTION §4.4
  row 4 ("Phase 2-C").
- The `userEvent` CSV column read "" for every row. Root cause: CommonLibSF
  declares `InputEvent::QUserEvent()` returning `BSFixedString` BY VALUE;
  the engine vfunc returns a `BSFixedString*` in RAX. ABI mismatch, garbage
  copy. Do not trust `QUserEvent()` on 1.16.x.

### 8.2 The disassembled gate (1.16.242)

`LookHandler::ShouldHandleEvent` (RVA 0x12bcd80, AL 82236) is, in full:

    accept = (event->vfunc[2](event) data ptr == QLook() data ptr)  // "Look" tag
             && event->deviceType == (mode ? kGamepad : kMouse)     // ONE class

where `mode` derives from a global byte at RVA 0x5f657e0 (no AL ID) and a
byte at +0x60 of the object at RVA 0x5fa1c10 (no AL ID). `QLook` is
AL 74548 (RVA 0xf9bd60), a TLS-guard-initialized singleton getter. The
vtable (AL 433589) slots: 1 = ShouldHandleEvent (AL 82236),
4 = OnThumbstick (AL 82237), 6 = OnMouseMove (AL 82238).

Re-verify with `tools/al_db_parser.py` against the 1.16.242 exe + AL DB,
then capstone-disasm RVA 0x12bcd80 (0x6b bytes). The whole §8 stands or
falls on that one function body.

### 8.3 v1.5.0

The shim chains through the original, and if the original rejected a
mouse/gamepad event that carries the interned "Look" tag (checked with the
engine's own recipe: vfunc index 2 + AL 74548 + data-pointer identity), it
accepts it. Buttons, Move-stick, and cursor traffic keep the original
verdict. The force path arms ONLY on runtimes listed in
`kForceVerifiedRuntimes` (currently 1.16.242.0 alone); anywhere else the
build degrades to v1.4.0 measurement-only behavior. CSV gains `isLook` and
`forced` columns; the `userEvent` column is now populated via the raw
recipe instead of the broken `QUserEvent()`.

Field motivation (2026-06-09 Steam Deck test): gyro-as-mouse required a
keyboard-hold "wake" workaround, which flips the mode gate to KBM — right
stick look dies and left-stick analog movement goes sluggish. With both
look classes force-accepted, no wake input is needed and the engine can
stay in gamepad mode: analog movement untouched, right stick native, gyro
mouse deltas accepted. That is the experiment v1.5.0 ships.

### 8.4 Do NOT redeploy old all-hooks DLLs

`SimultaneousInput.dll.v140-tmp-bak` (an allhooks-era build) was A/B-tested
on 1.16.242 on 2026-06-09 and crashed Starfield before SFSE produced logs.
The upstream v1.0.3 source ("fullhooks") was also rebuilt against the
1.16.242 AL DB on anthony-gaming (`Downloads\Starfield-SimultaneousInput-
build\...\build-fullhooks\Release\SimultaneousInput.dll`, built 21:03) but
was never deployed — its 1.8.86-era byte offsets (+0x2A0, +0x0E, +0x68, …)
do not exist on 1.16.242 (§7 table, unchanged by the 242 patch), and its
`match_or_fail` on `Run_WindowsMessageLoop` aborts the load. Leave it.

### 8.5 v1.5.1 — CursorMove double-feed fix (2026-06-10)

First v1.5.0 field test: additive look WORKED (1,205 of 3,346 frames had
stick + mouse look accepted together; engine stayed in gamepad mode, ~1
mode flip in 69s) but the camera "spazzed out" with motion. Cause in the
CSV: the mouse emits BOTH a kMouseMove and a Look-tagged kCursorMove per
motion and v1.5.0 forced both (2,143 frames double-fed). In vanilla KBM
mode the OS cursor is captured at screen center so the CursorMove path is
benign; in gamepad mode the cursor drifts freely, so forced CursorMove
events carry unbounded positions — violence scales with accumulated
motion, matching the report. v1.5.1 forces only kMouseMove (mouse) and
kThumbstick (gamepad); kCursorMove always keeps the original verdict.
Also: CSV `userEvent` now logs the tag qword as hex (the interned token is
not guaranteed printable; the 06-10 CSV contained raw binary).

### 8.6 v1.5.2 — no forced MouseMove diagnostic (2026-06-10)

Anthony's v1.5.1 field test still produced violent gyro/camera spaz. Fresh
log confirmed v1.5.1.0 build `5e5b26814c80` was loaded and force path ARMED.
The CSV showed:

- 1,621 forced accepts, all `(eventType=1 kMouseMove, deviceType=1 kMouse, origReturn=0)`
- 1,346 CursorMove rows, all rejected (`forced=0, origReturn=0`)
- 953 native accepted Thumbstick rows and 515 rejected Thumbstick rows

So the v1.5.1 CursorMove exclusion did what it said, but the symptom survived.
The remaining high-probability cause is not CursorMove drift; it is accepting
MouseMove through this `ShouldHandleEvent` gate while the engine is otherwise in
gamepad mode, or a lower-level mouse-delta path reached by that acceptance.

v1.5.2 is intentionally diagnostic and conservative: it disables forced
MouseMove entirely and only force-accepts rejected gamepad Thumbstick Look
traffic. If the spaz disappears, the next real fix must move below this gate
and merge/scale mouse deltas correctly instead of blindly accepting MouseMove
at `ShouldHandleEvent`. If the spaz remains, the fault is outside the force
path and the CSV/log should prove it by showing `forced=0` for mouse rows.

### 8.7 v1.5.3 — temporary vanilla mouse-mode spoof (2026-06-10)

Anthony tested v1.5.2 and reported "no gyro now." Fresh log confirmed
v1.5.2.0 build `e7f28248f22b` loaded and the CSV showed `forced=0` for all
2,250 rows. The engine still saw 662 MouseMove/Mouse/Look rows, but vanilla
rejected every one. This proves forced MouseMove was the only reason gyro moved
in v1.5.0/v1.5.1, and also the likely source of the spaz.

v1.5.3 stops blindly returning true for rejected MouseMove. For verified
1.16.242 MouseMove/Mouse/Look events only, it temporarily writes the two
verified mode-gate bytes (`RVA 0x5f657e0`, and `*RVA 0x5fa1c10 + 0x60`) to the
mouse-accepted branch (`0`) while calling the original
`LookHandler::ShouldHandleEvent`, then restores both bytes immediately. The
intent is to let vanilla's own accept path and side effects/scaling run for
mouse-look without leaving the game in KBM mode long enough to poison analog
left-stick movement.

If this restores gyro without spaz, the root cause was bypassing vanilla's
accept-side effects. If it still spazzes, the issue is downstream of
`ShouldHandleEvent` and the next step is payload logging / lower-level merge
work around `OnMouseMoveEvent` rather than this gate.

### 8.8 v1.5.4 — raw input payload measurement (2026-06-10)

Anthony tested v1.5.3 and reported no gyro. Fresh CSV showed the mode-spoof
attempt still left all MouseMove/Mouse/Look rows rejected, so the verified mode
bytes are not sufficient to make vanilla accept that path in the live call.

v1.5.4 keeps behavior conservative and adds raw payload columns for each event:
`q28,q30,q38,q40,f28,f2c,f30,f34`. These are deliberately dumb reads from the
first subclass payload bytes after `InputEvent` (base size 0x28). A short gyro
wiggle should show whether MouseMove and CursorMove carry sane deltas, absolute
positions, accumulated drift, or some other shape. Use this before attempting an
`OnMouseMoveEvent` hook, synthetic event, or payload normalization.

### 8.9 v1.5.5 — attenuated MouseMove force test (2026-06-10)

v1.5.4 payload data showed MouseMove and CursorMove both carry payload at
`+0x38`, but with different semantics:

- MouseMove: packed signed 32-bit deltas (low=x, high=y), e.g. `(-1,-2)`,
  `(-7,-4)`, `(-5,6)`.
- CursorMove: packed absolute cursor position, e.g. around `(1278,799)` and
  drifting.
- Thumbstick: packed floats in the same qword.

This confirms CursorMove should stay rejected and MouseMove is the only viable
gyro path seen by this hook. v1.5.5 force-accepts rejected MouseMove/Mouse/Look
again, but first divides its packed x/y deltas by 8 (preserving +/-1 for
nonzero tiny deltas). This tests whether v1.5.1's "works but spazzes" result
was magnitude/scaling/accumulation rather than event identity.

### 8.10 v1.5.6 — clamp attenuated MouseMove spikes (2026-06-10)

Anthony tested v1.5.5: much improved and generally stable while moving, but
occasional jarring snap/grab to floor or ceiling while panning. The v1.5.5 CSV
matched that: after /8 scaling, most forced MouseMove deltas were tiny
(p95 abs x=2, p95 abs y=1; p99 abs x/y=3), but a rare early spike survived as
`x=10,y=14` post-scale. That is exactly the kind of vertical yank reported.

v1.5.6 keeps the /8 attenuation but clamps post-scale MouseMove deltas to
`x = +/-4`, `y = +/-2`. CursorMove still remains rejected.

### 8.11 v1.5.7 — clamp vanilla MouseMove, suppress vanilla CursorMove, restore gamepad mode (2026-06-10)

Anthony tested v1.5.6: still getting yanked in some circumstances, and if gyro
starts before movement both sticks become slow/sluggish; if movement starts
first gyro feels slow. The CSV explains it: once Starfield slips into mouse
state, vanilla itself starts accepting raw MouseMove and CursorMove rows
(`origReturn=1`) that bypass the v1.5.6 forced-path clamp. Those raw accepted
MouseMove rows include huge y deltas (`y=173`, `-171`, etc.), and CursorMove is
accepted too. That matches both the yanks and the stick poisoning.

v1.5.7 applies the clamp to MouseMove regardless of whether vanilla accepted it
or the shim forced it; suppresses CursorMove even when vanilla accepts it; and
after mouse-look handling writes the verified mode-gate bytes back to gamepad
(`1`) to avoid leaving the game in a KBM/mouse state that degrades sticks.

### 8.12 v1.5.8 — low-gain mouse injection (2026-06-10)

Anthony tested v1.5.7 and reported gyro was still yanked badly, way too
sensitive, and fought right-stick aiming/fine tuning. The v1.5.7 CSV showed
CursorMove was suppressed (`origReturn=0`, `forced=0`) and MouseMove was clamped
(x +/-4, y +/-2), with no large raw spikes surviving in the logged payload.
Therefore the remaining issue is likely the forced mouse stream being too hot
and fighting the native stick path, not one-off unbounded CursorMove spikes.

v1.5.8 changes the MouseMove attenuation from /8 with +/-1 preservation to a
low-gain path: X is `/24` clamped to +/-2, Y is `/32` clamped to +/-1, and tiny
nonzero deltas are allowed to drop to zero. CursorMove remains suppressed.

### 8.13 v1.5.9 — fractional low/mid-gain mouse injection (2026-06-10)

Anthony tested v1.5.8: no yanking and overall better, but gyro became slow,
sticky, poor vertically, and bad diagonally. The v1.5.8 CSV confirmed why:
~42% of forced MouseMove packets became `(0,0)` after scaling, X was zeroed
~54% of the time, and Y was zeroed ~80% of the time. That destroyed Steam's
already-shaped gyro curve and made fine movement stick until a larger delta
arrived.

v1.5.9 keeps CursorMove suppressed and keeps gain lower than v1.5.7, but changes
MouseMove attenuation to fractional accumulation: X and Y use `/16` with residue
carried between packets and clamps of X +/-3, Y +/-2. This should preserve small
Steam-profile movements over time rather than discarding them, while staying
below the hot v1.5.7 gain that caused yanks.

### 8.14 v1.5.10 — native-first mouse pass-through (2026-06-10)

Anthony tested v1.5.9 and reported it still felt processed: horizontal panning
could rubber-band/360 snap, vertical gyro stopped tracking mid-sweep, and even
standing-still gyro no longer felt like the pre-plugin/native Steam gyro feel.
That is the architectural guardrail: Steam Controller/Steam Deck profile
settings must remain the source of truth for sensitivity, smoothing, activation,
and curve. The plugin must not become the gyro tuning layer.

v1.5.10 removes all MouseMove reshaping: no scale, clamp, fractional residue, or
post-mouse mode-byte writeback. It force-accepts relative MouseMove as-is when
vanilla rejects it, continues to suppress absolute CursorMove, and still permits
look thumbstick acceptance. If yanking returns, the next fix should be temporal
arbitration/handoff around right-stick/gamepad state, not mouse delta tuning.

### 8.15 v1.5.11 — disable gamepad byte patch diagnostic (2026-06-10)

Anthony tested v1.5.10: native pass-through MouseMove immediately spazzed when
capacitive gyro activated. The v1.5.10 CSV showed raw forced MouseMove deltas up
to about X +/-30 and Y +/-51. Starfield can tolerate those in normal mouse mode
(Anthony's pre-plugin standing-still gyro felt beautiful), but not when our
mixed-input force path/active-device state is involved.

v1.5.11 disables Hook 2 (`BSPCGamepadDevice::Poll` byte patch) entirely while
keeping the LookHandler shim. This tests whether the active-device byte patch is
what destroyed native standing-still gyro feel. Expected tradeoff: left stick may
again flip active device or degrade simultaneous input, but gyro-alone should be
closer to the true Steam mouse path. If gyro-alone is restored, reintroduce
simultaneous movement via temporal arbitration instead of global byte patching.

### 8.16 v1.5.12 — no forced MouseMove safe baseline (2026-06-10)

Anthony tested v1.5.11 and gyro still spazzed any time activated. The CSV showed
Hook 2 removal did not fix it: every MouseMove was still `origReturn=0` and
`forced=1`, with raw Steam gyro deltas up to about X +/-52 and Y +/-50. That
proves the unsafe behavior is force-accepting rejected MouseMove itself, not only
the gamepad byte patch or our post-processing.

v1.5.12 removes forced MouseMove acceptance entirely. It leaves MouseMove as
measurement-only, continues suppressing absolute CursorMove, keeps Hook 2
disabled, and still allows gamepad Look force acceptance. This is a safe baseline
intended to stop camera spazz while preserving logs for the next architectural
approach. If gyro no longer works, expected; the next solution must find a
vanilla/native mouse-mode path or a safer temporal handoff, not raw force-accept.
