# MOD_ARCHITECTURE.md — Adversarial Critique

**Reviewer persona:** kernel-maintainer-grade RE/code review, in the spirit
of LKML responses to undercooked patch series. The author of
`MOD_ARCHITECTURE.md` (hereinafter "the doc") asked for it. They get it.

**Methodology:** every claim in this critique is grounded in either (a) the
contents of the doc itself, with §-numbered citations; (b) the contents of
the live source tree on this branch, with `file:line` citations; or (c)
independent disassembly of `Starfield.exe` 1.16.236 performed during this
review using the on-disk PE at `/private/tmp/sf_re/Starfield.exe`, capstone,
and lief. Where I disagree with the doc on a binary-evidence question, I
ran the bytes myself and report the result.

**Verdict line up front (so the bottom of §H is not a surprise):**
**SEND BACK FOR REDESIGN.** The §7.2/§8.2 keystone hypothesis (the byte at
RVA `0x05f67820` is `ENGINE_GAMEPAD_ACTIVE` and "owning one byte" delivers
controller-glyph stability) is **demonstrably wrong** under direct
disassembly. The entire v2.0 strategy (b) is built on a misidentified
constant. Detail in §B-1 and §E-1.

---

## §A. Hand-waves and unsupported claims

The doc opens by promising every claim is grounded in (a) disassembly with
RVA cited, (b) AL DB resolution, or (c) libxse with file:line. It then
breaks that promise repeatedly. Each item below is a place where the doc
makes a load-bearing claim and provides no evidence behind it, or evidence
that is one observation extrapolated to a population.

**A-1. "Hypothesized" is doing the work of "verified" throughout §7.**
§7.2: *"This global is the most likely candidate for `ENGINE_GAMEPAD_ACTIVE`
— a single byte that LookHandler reads to gate its slot 10 logic. (Pending
confirmation in v2.0 RE: cross-reference all xrefs to this byte; expect to
find writes from `BSInputDeviceManager::Poll` or its event handlers, and
reads from many UI/cursor consumers.)"* Then §8.5 uses that hypothesis as
the foundation of the entire recommended strategy. **Pending RE work
should not appear in a recommendation, full stop.** Either do the xref
work first and put the verified result in §7.2, or mark §8.2 as
**SPECULATIVE PENDING R1** in big letters at the top of §8.5. The current
doc structure invites future-Tony to read §8.5 as the plan and miss the
"hypothesized" qualifier 200 lines back. I ran the xref work the doc
deferred — see §B-1. The hypothesis does not survive contact with the
binary.

**A-2. "Bethesda inlined `IsUsingGamepad` everywhere it was called"
(§7.3).** This is presented as the load-bearing explanation for why
hooks 3-9 broke. It is offered with three pieces of evidence: AL 178879 is
a logging stub, LookHandler slot 10 reads a global byte, IMenu cursor
logic is now expressed via flag bits. None of these *prove* inlining.
They are all consistent with **rewrite**, **deletion**, **replacement
with a different mechanism**, or **the original predicate never existed
under the name Parapets gave it on 1.8.86**. Inlining is one hypothesis
among several; the doc presents it as the conclusion. Linus would write:
"You found three things that are consistent with X. Did you check whether
they're also consistent with Y, Z, and W?"

**A-3. "Likely works on 1.16.236"** in §5.5.4's transferability table.
The "Direct global-byte memory write" row says *"Likely works on 1.16.236
— LookHandler's slot 10 reads a global byte, indicating Bethesda still
uses a single device-active flag."* This conflates *one* read of *one*
byte at *one* location with *the existence of a single device-active
flag*. As §B-1 shows, the static evidence does not support the claim that
the byte at `0x05f67820` is a flag at all.

**A-4. "Should port"** in §5.5.4. The "Menu RefreshPlatform after flip"
row asserts Skyrim's mechanism *should port*. The doc does not cite a
1.16.236 disassembly of any menu's RefreshPlatform-equivalent. R3 in §8.5
admits this is undone work. **A "should port" with the actual port not
yet attempted is not evidence; it is optimism.**

**A-5. "Plausible"** for the constructor-time NOP equivalent. §5.5.4 row
4: *"BSInputDeviceManager::Ctor likely has equivalent disable-others
logic. RE work needed."* Same structural defect: an explicit
"RE work needed" tagged onto a row in a table whose presence in the doc
implies it has been considered as a v2.0 ingredient.

**A-6. CP77 architectural claim is unsupported (§6.3).** *"each device's
contribution is summed into the per-frame look delta vector, with each
contribution scaled by its own sensitivity. We will architect the same
way for Starfield (§8)."* The doc *correctly* notes "We did not find a
primary CDPR engineering doc explaining whether CP77 merges mouse + gamepad
+ gyro additively at the input event layer or at the look command layer"
— and then commits to a specific architecture anyway. Either find an
authoritative CP77 source (decompilation, post-mortem, dev tweet, anything
beyond a forum thread) or stop calling this "Cyberpunk-equivalent." It is
not "Cyberpunk-equivalent" if it is not what Cyberpunk does; it is *what
the author imagines Cyberpunk does*.

**A-7. "Most likely cause" of the AL DB resolution discrepancy
(`tools/derive_function_ids.py` docstring).** The script's own preamble
says: *"The most likely cause is a stale named memory map (libxse uses
OpenFileMappingA before falling back to file mapping; if a prior process
created `COMMONLIB_IDDB_OFFSETS_1_16_236_0` with different data, libxse
attaches to that)."* This is offered as the explanation but the script
explicitly says "Until this is reproduced cleanly, the --log path is the
only reliable source of ground-truth RVAs in 1.16.236." So the *primary
input* to the doc's RVA derivation is admitted to be unreliable, and the
doc never returns to the problem. If the AL DB resolution mechanism is
not understood, every doc claim of "AL ID X resolves to RVA Y" inherits
the same uncertainty.

**A-8. "Bethesda's own support page treats cursor misbehavior with
controller plugged in as a known issue — the official remedy is 'unplug
any peripheral devices such as gamepads'. That is Bethesda admitting the
cursor-confine logic is broken when both device classes are present"
(§1.3).** Verified that the page says that (I fetched it during this
review). But the doc's *interpretation* — "Bethesda admitting the
cursor-confine logic is broken" — is one possible reading. It is also
consistent with: "any non-mouse HID can hijack focus and Win32 cursor
ownership at the OS layer". The doc's interpretation is plausible but is
presented as fact. Acceptable; flag for tightening.

---

## §B. Unverified RVAs / struct layouts / vtable indices

I verified the doc's binary claims against the actual on-disk
`Starfield.exe` 1.16.236. Most of the small claims hold up. The largest
claim does not.

**B-1. CRITICAL — `ENGINE_GAMEPAD_ACTIVE` at RVA `0x05f67820` is not a
flag.** This is the load-bearing claim of the entire v2.0 spec. I ran the
RE work the doc deferred to "R1" in §8.5.

What I did:
- Walked every instruction in `.text` (5.9M instructions) using capstone,
  recording every `[rip + disp32]` memory operand whose effective address
  resolved to RVA `0x05f67820`.
- Counted reads vs writes by inspecting the instruction's first operand
  type.
- Examined the byte value and surrounding bytes in the on-disk `.data`
  section.

Findings:
- **37 readers** in `.text` (more than the doc's "expect ≥10"). All but
  one are `cmp byte ptr [rip + disp], 0` or `cmp byte ptr [rip + disp],
  <reg>` style. The doc's quoted reader at `0x12bcdc0` (LookHandler slot
  10) is among them.
- **0 writers** of any kind that the static disassembly catches.
- **The byte's static value at file offset is `0x10`**, not 0 or 1. A flag
  used to gate "is the controller active" would be 0 or 1. `0x10` is
  consistent with this byte being a **byte of a multi-byte structure**,
  not a standalone flag.
- **Surrounding bytes are a structured table of (32-bit) RVA pairs**
  alternating between `.text` (function pointers) and `.rdata` (likely
  format strings or RTTI). Layout dump (verified):
  ```
   rva 0x05f67800: 0x03169440  -> .text
   rva 0x05f67804: 0x03169620  -> .text
   rva 0x05f67808: 0x0542bf0c  -> .rdata
   rva 0x05f6780c: 0x03169620  -> .text
   rva 0x05f67810: 0x031696dd  -> .text
   rva 0x05f67814: 0x057ebae0  -> .rdata
   rva 0x05f67818: 0x031696dd  -> .text
   rva 0x05f6781c: 0x031696e6  -> .text
   rva 0x05f67820: 0x057ebb10  -> .rdata   <-- this is the "byte"
   rva 0x05f67824: 0x031696e6  -> .text
   rva 0x05f67828: 0x031699ba  -> .text
   rva 0x05f6782c: 0x057ebb24  -> .rdata
  ```
  This is a function-pointer / metadata table, almost certainly compiler-
  generated (RTTI, SEH, an indirection table for some dispatch
  mechanism). The "byte" at `0x05f67820` is the **low byte of a 4-byte
  RVA pointer** whose value is `0x057ebb10`. The 37 `cmp [...], 0`
  readers are NULL-pointer-low-byte checks on slots in this table — a
  common compiler idiom for "is this slot initialized." Because the slot
  is initialized to a non-zero pointer in this build, every one of those
  branches is **deterministic at runtime** (in the absence of writers).

What this means:
- The hypothesis "0x05f67820 = `ENGINE_GAMEPAD_ACTIVE` and pinning it to
  1 makes glyphs stable" is **wrong**. Pinning the low byte of a
  function-pointer slot to 1 does nothing useful; it would, if the byte
  were ever overwritten by something the engine does, *corrupt a
  pointer*.
- The doc's analogy from FO4's `g_gamepadEnabled` (§5.5.2) does not
  transfer to this address.
- The "37 readers all consult the same flag" pattern that would justify
  Strategy (b) **does not exist in this binary at this address**.

What the doc should have done before §7.2: any one of:
1. xref scan for writers (the same pattern I used) — would have shown 0
   writers and stopped this hypothesis cold;
2. dump 32 bytes around the address and notice the pointer-table shape;
3. resolve `0x057ebb10` (the qword the "flag" actually starts) and notice
   it points into `.rdata` — **flags don't point at `.rdata`**.

Each of those steps takes minutes. The doc deferred all three to "R1
pending v2.0 RE." That is not acceptable when the byte's identity is the
foundation of the entire recommended strategy. **§7.2 is the keystone of
the v2.0 plan and the keystone is invented.**

**B-2. Doc address arithmetic is sloppy in spots.** Independent disasm of
`BSInputDeviceManager` vtable slot 1 at RVA `0x22da380` shows:
```
022da380: 0f b6 42 10                 movzx eax, byte ptr [rdx + 0x10]   ; 4 bytes
022da384: 2c 05                       sub al, 5                          ; 2 bytes
022da386: 3c 01                       cmp al, 1                          ; 2 bytes
022da388: 0f 96 c0                    setbe al                           ; 3 bytes
022da38b: c3                          ret                                ; 1 byte
```
Function size: 12 bytes (`0x22da380 .. 0x22da38b` inclusive). The doc's
§4.1 listing has the same instructions but with **wrong addresses** for
each line after the first (`022da387` for sub, `022da389` for cmp,
`022da38b` for setbe, `022da38e` for ret). Those addresses cannot be
correct: `movzx eax, byte ptr [rdx + 0x10]` is 4 bytes, not 7. The doc
author hand-incremented addresses by either 2 or 3 bytes per instruction
without correlating to the real opcodes. This is a small thing on its
own. Combined with §B-1, it tells me the doc's transcription discipline
is unreliable and any disassembly listing in the doc should be re-checked
before trust.

**B-3. LookHandler slot 10 prelude transcription is also slightly off.**
§7.2 says: *"012bcdc0: cmp byte ptr [rip + 0x4caaa59], 0 / 012bcdc7: jne
0x12bcddb"*. Independent disasm:
```
012bcdc0: 80 3d 59 aa ca 04 00       cmp byte ptr [rip + 0x4caaa59], 0
012bcdc7: 75 11                       jne 0x12bcdda
```
The branch target is `0x12bcdda`, not `0x12bcddb` (off by one). Not
operationally fatal — `jne` rel8 with displacement `0x11` from RIP
`0x12bcdc9` lands at `0x12bcdda` per actual encoding. But it is exactly
the kind of off-by-one that would burn an implementer who copy-pastes
addresses from the doc into a probe script.

**B-4. AL ID file:line citation in §2.1 is wrong.** Doc says
*"`LookHandler` vtable is AL `433589` (libxse canonical,
[CommonLibSF/include/RE/IDs_VTABLE.h:5145])"*. Line 5145 of
`IDs_VTABLE.h` actually contains `BSInputDeviceManager` (with IDs 469745,
469743, 469747). `PlayerControls__LookHandler` lives at **line 14651**
of the same file. AL ID `433589` itself is correct (libxse-canonical and
matches `src/RE/Offset.Ext.h:117`); the file:line cross-reference is
wrong by ~9500 lines. **Two implications:** (1) sloppy citations make the
doc harder to audit; (2) other file:line cites in the doc inherit
suspicion.

**B-5. The doc's chain of inference for "Hook 1's vtable slot 1 IS
ShouldHandleEvent" is correct.** Verified: `BSInputEventUser`
([external/CommonLibSF/include/RE/B/BSInputEventUser.h:127](external/CommonLibSF/include/RE/B/BSInputEventUser.h)) declares
`virtual ~BSInputEventUser() = default;  // 00` followed by 9
add-virtuals 01-09 (ShouldHandleEvent, OnKinectEvent, OnDeviceConnectEvent,
OnThumbstickEvent, OnCursorMoveEvent, OnMouseMoveEvent, OnCharacterEvent,
OnButtonEvent, Unk09). Slot 1 is `ShouldHandleEvent`. Good.

**B-6. The doc's "slot 10 of LookHandler vtable[0]" is plausible but
unconfirmed.** `BSInputEventUser`'s ABI gives 10 slots (0..9). Slot 10
must be a `LookHandler`-specific add-virtual. The doc has not
disassembled the vtable to confirm slot 10 is exclusive to `LookHandler`
(vs. coming from an intermediate base class with its own add-virtuals
that bump LookHandler-specific slots later). For comparison, libxse's
`PlayerControls__LookHandler` array has length 1 (one vtable[0]) so the
direct-derivation should be straightforward — but the doc does not show
the calculation. Acceptable; flag for adding the explicit slot-numbering
derivation.

**B-7. Real `BSPCGamepadDevice::Poll` body and two anchor sites at +0x51D
and +0x5DC are confirmed.** I scanned the first 0x800 bytes of the body
at RVA `0x2302bc0` for `C6 43 08 01` and found exactly two hits, at
offsets `0x51d` and `0x5dc`. Doc's claim verified. (Implementation
implication for §F-2.)

**B-8. ClipCursor call site at `0x189fadf` is confirmed.** Disassembled
the area at `0x189fad8` and verified: `eb 02` (jmp short to +0x189fadf)
at `0x189fadb`, `33 c9` (xor ecx, ecx) at `0x189fadd`, `ff 15 3b 41 1a 02`
(call qword ptr [rip + 0x21a413b]) at `0x189fadf`. The doc's structural
claim is correct.

**B-9. Win32 message loop body at `0x18a0387` is confirmed.** Sequence
PeekMessageA / TranslateMessage / DispatchMessageA verified by the
canonical IAT-call-shape `FF 15 disp32` at the offsets the doc claims.
Good.

**B-10. AL 178879 → `0x3552490` logging-stub claim is confirmed.**
Disassembly matches the doc's transcription functionally; the doc omits
the four-instruction prologue (`mov [rsp+0x10], rdx; mov [rsp+0x18], r8;
mov [rsp+0x20], r9; sub rsp, 0x38`) before the `cmp byte ptr [rip +
disp32], 0` log-enabled gate. Cosmetic.

**B-11. AL 433589 / 124384 / 469745 / 475515 (vtable IDs) match libxse.**
Cross-checked:
- `BSInputDeviceManager` array contains `REL::ID(469745)` —
  [external/CommonLibSF/include/RE/IDs_VTABLE.h:5145](external/CommonLibSF/include/RE/IDs_VTABLE.h);
- `IMenu` array contains `REL::ID(475515)` —
  [external/CommonLibSF/include/RE/IDs_VTABLE.h:13455](external/CommonLibSF/include/RE/IDs_VTABLE.h);
- `PlayerControls__LookHandler` contains `REL::ID(433589)` —
  [external/CommonLibSF/include/RE/IDs_VTABLE.h:14651](external/CommonLibSF/include/RE/IDs_VTABLE.h);
- AL `124384` is not in `IDs_VTABLE.h` (it's the Poll function ID, not a
  vtable). Doc's `tools/README.md` derived it via vtable[470133][1] read.
  Reasonable.

---

## §C. Test plan quality

**C-1. §9 is mostly checklist-by-proxy, not behavioral.** The doc claims
in its preamble that "predictions T1-T10 derive from the architecture
model in §1-§4, not from running the current build." Then §9 (the v2.0
acceptance gate) repeats the same shape: each row is "if X, then Y is
the cause." Most rows describe an **observation** (camera looks, cursor
visible, glyph stays). A few do not actually test the claimed invariant.

**C-2. C2 + C3 is the only row that even attempts to test additive look,
and it does so qualitatively.** Doc text: *"Camera *additively* nudges
via mouse delta on top of any residual stick input."* What does
"additively" verify under observation? If the engine multiplies the
deltas, or substitutes the larger of the two, the camera will *also*
"nudge" — and Tony cannot tell from gameplay whether mouse delta was
added or substituted, because both produce camera motion. **A real
additive test would be: hold stick at known constant deflection (e.g.
fully right), measure look-yaw rate; introduce mouse delta at known
constant rate; measure new look-yaw rate; assert delta == rate1 + rate2.**
Without a measurement, "C3 PASSED" is "the player saw the camera move."
That is not a test; it is a vibe check.

**C-3. The "Hard invariant 1" tests (C8, C11, C12) test glyph stability
under different conditions but cannot distinguish PASS-because-glyph-was-
stable from PASS-because-glyph-was-already-controller-and-no-event-flipped-
it.** A robust glyph-stability test demands the test sequence FORCE a
flip-attempt. C6 ("Press E on keyboard") attempts this. C5 also does it
(*"Press B on controller"*). But neither test verifies that the flip-
attempt was *seen by the engine* — only that the rendered glyph didn't
change. A controller event swallowed by the OS or by SteamInput before
reaching the engine would also produce "no glyph flip." **Add a precondition
check: log device-event arrival in the plugin, assert the event arrived,
THEN assert glyph did not flip.**

**C-4. C9 (cursor confinement) is the cleanest test in the matrix.** It
makes a falsifiable claim under a simple observable. Use this row's
discipline as a model for rewriting the others.

**C-5. There is no test for the failure mode the v1.3.0 build actually
exhibits.** Tony's lived experience (per §2.3) is that v1.3.0 ships with
"no working sensitivity / quadrant / reticle / cursor-confine fixes."
None of C1-C12 measures sensitivity-curve behavior, quadrant-fix
behavior, or reticle behavior independently. C10 mentions ship reticle
under controller fire + mouse aim, but does not isolate the reticle from
the camera (both move when both inputs are active). **Add C10a: in
cockpit, disable mouse aim, hold stick stationary, record reticle
position; enable mouse, move it constant rate, record reticle position.
Assert the reticle responded to mouse delta.**

**C-6. The "discipline" sub-bullet at the bottom of §9 ("C1, C5, C7, C9
each test a single hook in isolation. The build is not shippable until
they all pass.") is the right idea but only enumerates 4 of 12 rows. The
other 8 are claimed to test invariants but their per-hook isolation is
not stated. State it.**

**C-7. The 60-second user-acceptance test that future-Tony can run after
each rebuild is missing.** §G-3 elaborates. The current §9 takes
roughly 5-10 minutes per row × 12 rows = an hour-plus. After 4 hours of
RE work, future-Tony will not run a full hour-long test matrix; he will
spot-check, see "looks fine," and ship a regression. Provide a 60-second
canonical golden-path test (e.g. "load save X, controller in hand, mouse
on the desk, look around using both for 30 seconds, glyphs should never
flip; alt-tab, glyph stays controller; press B, ✕ prompt appears; done").

**C-8. Test plan does not specify ANY pass/fail mechanism for "glyph
stability under fast input."** If the engine flips on the leading edge of
mouse motion and re-flips on the trailing edge, a sufficiently slow human
test will see "controller glyph" both before and after the test and
conclude PASS, while in fact the glyph flickered to KBM for ~30ms during
the test. **Either record the test, or write the plugin to log glyph-
flip-attempts that the user can later grep, or both.**

---

## §D. Prior-art mapping

**D-1. AutoInputSwitch claims are accurate.** Verified by fetching
[github.com/Exit-9B/AutoInputSwitch/blob/master/src/Hooks.cpp](https://github.com/Exit-9B/AutoInputSwitch/blob/master/src/Hooks.cpp)
and `src/InputEventHandler.cpp`. The five hooks in §5.5.1 match. The
"glyph-stability secret" — register an event sink on
`BSInputDeviceManager`, walk `ui->menuStack`, call
`menu->RefreshPlatform()` — is precisely what
`InputEventHandler::DoRefreshMenus` does. Citation-check passes.

**D-2. The mechanism mapped to Starfield is hand-waved.** §5.5.4 row 3
says the RefreshPlatform pattern *"should port — IMenu still has a vtable
+ flags member at +0xC0 and slot 16/17 set/clear flag bits. Hooking the
equivalent of RefreshPlatform requires identifying the right vtable slot
or function."* This is exactly the hand-wave Linus would catch:
- The doc asserts "should port" without identifying the equivalent.
- AutoInputSwitch's `RefreshPlatform()` is a method on Skyrim's IMenu
  base. Starfield's `IMenu` ([external/CommonLibSF/include/RE/I/IMenu.h](external/CommonLibSF/include/RE/I/IMenu.h))
  exposes `SetFlags(u32)`, `RemoveFlags(u32)`, and a `flags` field — but
  no method named `RefreshPlatform`. The doc's "R3" task acknowledges
  this. Why is the row in the table then? **A row in a transferability
  table that depends on undone RE work is not a transferability claim;
  it is a wish.**

**D-3. The FO4 reg2k mapping (§5.5.2) is structurally appealing but the
doc does not actually verify the analogy on Starfield.** *"FO4 has a
single global byte that every consumer reads. Skyrim has a method
(`QUsingGamepad`) backed by similar state. Starfield 1.16.236's evidence
(§4.2 above) points to a global byte flag at `[rip + 0x4caaa59]` read by
LookHandler's slot-10 virtual."* The mechanistic explanation for why
Skyrim's `QUsingGamepad` works (it's a single canonical predicate read by
many UI consumers) is sound. The mapping to Starfield assumes that the
Starfield byte is the analog. As §B-1 demonstrates, **it is not.** The
prior-art reasoning is not wrong; the binding to the Starfield artifact
is.

**D-4. The "What does NOT port" list (§5.5.3) is rigorously honest.**
Hybrid Controls / controlmap.txt edits, Skyrim Souls RE — both correctly
reasoned out. This is the standard the rest of §5.5 should meet.

**D-5. Skyrim's `IsGamepadConnected + 0xD` hook (AutoInputSwitch
"InstallGamepadCursorHook") is mis-described in §5.5.1.** Doc says
*"hook 4 — `BSInputDeviceManager::IsGamepadConnected + 0xD` — same
trick; cursor visibility tracks 'active device', not 'connected
device'."* The actual function being patched IS `IsGamepadConnected`,
and the redirect IS to `IsUsingGamepad`. Description is *functionally*
correct but elides that the function being hooked is named for "is
connected" and the redirect is to "is currently in use." Minor; flag.

**D-6. The "BSPCGamepadDeviceHandler vtable slot 0x7" hook description
omits its purpose.** Doc summarizes it as: *"replaces
`IsGamepadDeviceEnabled` so SkyUI MCM's StartRemapMode lets the user bind
from any device."* That's correct, but it is a Skyrim/SkyUI-specific
behavior with no obvious Starfield analog. Why is it cited? If the answer
is "it is not load-bearing for our Starfield port," strike it from §5.5.1
or annotate that it does not feature in the transferability discussion.

---

## §E. Strategy soundness

**E-1. Strategy (b) is built on the §B-1 misidentification. Without a
verified `ENGINE_GAMEPAD_ACTIVE`, Strategy (b) is undefined.** The
recommended hooks #3 and #4 in §8.5 ("Pin or NOP the writer on
`ENGINE_GAMEPAD_ACTIVE` global at RVA `0x05f67820`" and "Walk + refresh
`UI::menuStack` on each device flip") are operations on a flag whose
existence is not established. Any patch you write here will, at best, do
nothing; at worst, corrupt the function-pointer slot at `0x05f67820` and
crash the game on the next dispatch through that slot.

The doc must do one of:
- find the *real* ENGINE_GAMEPAD_ACTIVE (if it exists). Practical
  approach: instrument LookHandler vtable slot 4 (OnThumbstickEvent) and
  vtable slot 6 (OnMouseMoveEvent) at runtime, log "was the device-active
  flag flipped between successive events," locate the writer by single-
  step or by intercept-and-stack-walk;
- accept that the engine no longer has a single canonical flag and re-
  architect Strategy (b) accordingly (e.g. find the actual writers in
  EVERY consumer that flips glyphs, NOP each one);
- abandon Strategy (b) entirely.

**E-2. Strategy (c) (IAT hook on USER32!ClipCursor) is mostly sound but
under-specified.** The hook is to install a thunk and use a caller-RVA
filter to decide whether to call through. Open issues the doc does not
address:
- **Anti-cheat / DRM compatibility.** Steam's overlay DLL hooks
  `ClipCursor` itself; so does NVIDIA's GeForce Experience DLL. Order of
  IAT hook installation determines who wins. The doc does not specify
  IAT hook ordering or whether to defer to the previous IAT entry as the
  "next in chain."
- **Caller-RVA filter brittleness.** §3.3 strategy (iii) proposes
  *"Find the function containing 0x189fadf at its entry"* by walking
  backward from the call site for a function prologue. Backward walks
  for function starts are unreliable — they fail on tail-call padding,
  on functions whose prologue is non-canonical (no `push rbp`), and on
  functions split by the optimizer into unconnected basic blocks. The
  doc handwaves this as "scan backward for function prologue or previous
  ret/int3 padding"; that is the standard approach and it works most of
  the time but it has a 5-10% failure rate on real binaries. The doc
  should specify what to do on filter mismatch (default-allow or
  default-deny — and explain the consequence of each).
- **`ClipCursor` is NOT the only confine mechanism.** Win32 also has
  `SetCapture` / `ReleaseCapture`, and Starfield uses raw input which has
  its own focus rules. The doc treats `ClipCursor` as canonical without
  evidence that it is the only mechanism in play. If `SetCapture` is
  *also* used to confine the cursor, the doc's IAT hook does nothing for
  that path.

**E-3. The §8.4 Strategy (d) dismissal is correct on the merits, wrong
in framing.** *"Strategy (d): Build a fresh input merger as our own
plugin. Verdict: OVER-ENGINEERED."* The verdict is reasonable. But the
framing — that Strategies (b) and (c) achieve "the same end with 2-3
surgical patches" — is true *only if Strategy (b) works*. Per §E-1,
Strategy (b) does not work as currently designed. So the alternative
landscape is not "small surgical patches" vs "rewrite everything"; it is
"strategy that is wrong" vs "more research needed" vs "rewrite
everything." The doc's framing assumes (b) is a known-good baseline.

**E-4. Failure mode analysis is missing for every recommended hook in
§8.5.** Each row should answer: "if this hypothesis is wrong, what do
users see, and what is the rollback?" Rollback for an IAT-hook is "remove
the IAT entry and we're back to vanilla behavior." Rollback for a NOP
patch is "restore the original 4 bytes." The doc names neither.

**E-5. There is no patch-version targeting strategy.** §8.5 lists
specific RVAs for 1.16.236 (`0x4c45f48`, `0x2302bc0`, `0x05f67820`,
`0x189fadf`). When Bethesda ships 1.16.237 and the AL DB ships
`versionlib-1-16-237-0.bin`, what runs? The current code's
`v.UsesAddressLibrary(true)` and empty `compatibleVersions[]` means SFSE
will load the plugin against any AL-DB-covered runtime. Will the byte at
`0x05f67820` *also* have moved? Likely yes; but the doc has no plan for
detecting "I'm running on a runtime where my hypothesized constants no
longer hold." **A version-bump-detection strategy is not optional for an
RE-based plugin; it is the difference between "minor patch breaks the
mod loudly" and "minor patch breaks the mod silently and corrupts the
game."**

**E-6. The §8.5 recommendation drops hooks 3-7 entirely on the assumption
that Strategy (b) subsumes them.** §C-1 in the test plan shares the same
assumption: "ShipHud reticle path reads `ENGINE_GAMEPAD_ACTIVE` and adapts
on its own once the flag is pinned (Strategy (b) subsumes hooks 6/7)."
This subsumption is unverified. Even if Strategy (b) were valid, the doc
provides no evidence that the ShipHud reticle code reads `ENGINE_GAMEPAD_
ACTIVE` rather than (e.g.) querying `BSPCGamepadDevice::Poll`-derived state
through some other path. **Subsumption claims need disassembly evidence —
"this consumer reads the flag we will own, here is the disasm" — not
hand-wavy "should adapt on its own."**

---

## §F. Specific code-review issues

I read the actual repo source. Several real issues, separate from the
doc's analysis quality.

**F-1. The shipped v1.3.0 plugin calls `IsUsingGamepad(BSInputDeviceManager*)`
against AL 139340 — which the doc itself says is NOT a predicate.**
[src/export/SFSEPlugin.cpp:86-91](src/export/SFSEPlugin.cpp:86):
```cpp
bool IsUsingGamepad(RE::BSInputDeviceManager* a_inputDeviceManager)
{
    using func_t = decltype(IsUsingGamepad);
    REL::Relocation<func_t> func{ RE::Offset::BSInputDeviceManager::IsUsingGamepad };
    return func(a_inputDeviceManager);
}
```
And [src/RE/Offset.Ext.h:42](src/RE/Offset.Ext.h:42):
```cpp
constexpr REL::ID IsUsingGamepad{ 139340 };
```
And the doc, §2.3:
> *"`0x28cef30` (4-host candidate, AL 139340): 0x200+ bytes, sets up a
> stack frame, dereferences arg1 as `*(this)`, reads `[rsi + 0x10]` (=
> `InputEvent::eventType`), compares to 1 (kMouseMove). It is reading
> event state, not returning a device-active bool."*

So:
- The function the code is calling reads its argument as if it were an
  `InputEvent*` (loads `[arg + 0x10]` for `eventType`).
- The code passes a `BSInputDeviceManager*`.
- The two pointer types have completely different layouts. `[BSInputDeviceManager*
  + 0x10]` is whatever happens to live at offset 0x10 of the singleton.
- The function returns true exactly when that random byte equals
  `kMouseMove` (1).

**This is undefined behavior shipping to users.** The function is being
called with the wrong argument type. The return value is meaningless. The
six call sites in `SFSEPlugin.cpp` that invoke this through
`IsUsingThumbstickLook` (which doesn't actually call it) and
`IsGamepadCursor` (which DOES call `IsUsingGamepad` on every cursor
visibility / style query — see [SFSEPlugin.cpp:113-116](src/export/SFSEPlugin.cpp:113))
are all consuming a meaningless boolean.

The doc both diagnoses this problem AND silently leaves it shipping in
the live code. Either the doc is right and `Offset.Ext.h:42` should be
removed, or the doc is wrong and 139340 actually IS a predicate. It
cannot be both.

**F-2. Byte patch loop breaks on first match — misses the second
`C6 43 08 01` site at +0x5DC.**
[src/export/SFSEPlugin.cpp:275-280](src/export/SFSEPlugin.cpp:275):
```cpp
for (std::size_t i = 0; i + 4 <= kScanLimit; ++i) {
    if (p[i] == 0xC6 && p[i + 1] == 0x43 && p[i + 2] == 0x08 && p[i + 3] == 0x01) {
        match_off = static_cast<std::ptrdiff_t>(i);
        break;
    }
}
```
The doc §2.2 explicitly states two anchor sites exist (verified in §B-7).
The current loop patches only the first. The second device-active flip
site at +0x5DC remains live. Predicted user-visible consequence: left-
stick movement still occasionally flips the active-device flag through
the second site. Tony's bug stays.

**F-3. `UsingThumbstickLook` is a non-atomic `static bool` written from the
input thread, read from any thread.**
[src/export/SFSEPlugin.cpp:97](src/export/SFSEPlugin.cpp:97):
```cpp
static bool UsingThumbstickLook = false;
```
Writes happen inside the LookHandler vtable shim ([SFSEPlugin.cpp:235-251](src/export/SFSEPlugin.cpp:235)),
which is invoked on whatever thread the engine dispatches input events.
Reads happen from `IsUsingThumbstickLook` and `IsGamepadCursor`, which
are called from any thread that hits a hooked call site (UI thread,
render thread, ship-HUD thread, etc.). On x86-64 a byte-aligned bool
write is atomic at the machine level, but **C++ does not guarantee that**
— reorderings, dead-store elimination, and load tearing are all in
play. Consequences are minor (occasional 1-frame flicker) but the bug is
the kind of thing that breeds unrepeatable Steam Deck reports. Use
`std::atomic<bool>` with `memory_order_relaxed` for both load and store.

**F-4. `vtbl.write_vfunc(1, ...)` silently overwrites whatever lives at
slot 1; no fallback to original.**
[src/export/SFSEPlugin.cpp:233-251](src/export/SFSEPlugin.cpp:233): the
shim returns `true` for "Look" events and `false` for everything else.
The original `LookHandler::ShouldHandleEvent` (per the doc's §2.1
description) returns true for events whose `QUserEvent() == "Look"` BUT
ONLY for one device class at a time. The shim drops the device-class
gate. **It also drops anything else the original implementation did** —
debouncing, history, dirty-flag updates, error logging, you name it.
There is no chain-call to the original ShouldHandleEvent. If the original
had non-trivial side effects (state updates the rest of LookHandler
relies on), the shim has silently disabled them.

The fix is well-known: capture the original vtable slot before patching,
call through to it after the shim's pre/post work, then return the
combined result. The current code doesn't.

**F-5. No multi-plugin coexistence story.** Vtable patches and IAT hooks
collide silently when multiple SFSE plugins target the same slot. There
is no guard rail in the current code that says "if slot 1 of LookHandler
vtable is not the address libxse expects, refuse to patch — another
plugin already owns this." A second mod doing the same vtable rewrite
will install on top of ours, and ours will install on top of theirs at
arbitrary load order, with no diagnostic to either user.

**F-6. The `vtable shim installed: LookHandler slot 1` log line at
[SFSEPlugin.cpp:252](src/export/SFSEPlugin.cpp:252) increments the
installed counter but provides zero verification that the shim is
actually being invoked once Starfield starts dispatching events.** A user
report of "mod doesn't work" cannot be triaged from this log: the line
will print whether or not the shim is on the engine's hot path. Add
either: a one-shot "shim invoked" log (rate-limited), or a periodic
diagnostic counter that surfaces in the log on shutdown.

**F-7. Trampoline budget of 32 bytes is sized for hooks the doc says do
not work.** [SFSEPlugin.cpp:218-223](src/export/SFSEPlugin.cpp:218):
```cpp
SFSE::Init(a_sfse, SFSE::InitInfo{
    .trampoline = true,
    .trampolineSize = 32,
});
```
Comment says "covers the six trampoline calls we install on 1.16.236
(LookHandler::Func10, ProcessLookInput, ShipHud x2, IMenu::ShowCursor,
UI::SetCursorStyle)." Those are the very hooks the doc declares broken
(§2.3, §2.5) and recommends dropping (§8.2 step 6). If §8 is adopted,
the trampoline budget should drop to 0 (or be removed entirely), but
the comment will then be stale and load-bearing-misleading. Pick a lane:
either ship the hooks or drop them, and update the trampoline sizing AND
its comment when you do.

**F-8. `TryWriteCall` silently masks "skipped" as success at the plugin
load level.** The function returns false on skip and true on install,
both `g_hooksInstalled` and `g_hooksSkipped` get incremented separately,
but the plugin always returns `true` from `SFSEPlugin_Load`
([SFSEPlugin.cpp:360](src/export/SFSEPlugin.cpp:360)). So a build where
all 8 trampoline calls skip and only the vtable shim works will load
"successfully." The user will see no error. This is a deliberate choice
(comment at line 357-359 explains it) but it should at minimum print a
one-line summary at WARN level that names which hooks were skipped, not
just count them. Currently the WARN at line 350-354 says `"6/9 hooks
installed, 3 skipped. plugin will run with reduced behavior."` — the
counts are useful but the *names* are buried in a bunch of separate
WARN lines elsewhere in the log.

**F-9. The `match_or_fail`-style "match by E8 first byte" check in
`TryWriteCall` is a single-byte sanity check, not a real anchor
verification.** [SFSEPlugin.cpp:136](src/export/SFSEPlugin.cpp:136):
```cpp
if (!REL::Pattern<"E8">().match(hook.address())) { ... skip ... }
```
A `0xE8` first byte means "5-byte rel32 call." It does NOT mean "5-byte
rel32 call to the function we expect." If a refactor moves the original
predicate but leaves an E8 call to *some other* function at the same
offset, the hook will install a redirect to `IsUsingThumbstickLook` and
silently change the behavior of an unrelated call site. **Anchor
verification should resolve the call destination and verify it matches
the expected predicate, OR verify the surrounding bytes match a known
pattern.** Single-byte `E8` is necessary, not sufficient.

**F-10. The exception handlers around each hook attempt
([SFSEPlugin.cpp:158-167](src/export/SFSEPlugin.cpp:158)) catch
`std::exception` but say nothing about `SEH` exceptions raised by access
violations during AL ID resolution.** If an AL ID resolves to an address
that is not currently mapped (e.g. paged-out section), the read in
`REL::Pattern<>().match` will raise an SEH exception that
`std::exception` catch will not handle. On MSVC `/EHa` semantics, SEH
exceptions can be caught by `catch (...)` but not by typed catch.
Process termination is the resulting behavior. Acceptable on a manual
review pass; flag for adding a `__try`/`__except` wrapper or `/EHa`
discipline.

---

## §G. What the doc is missing

**G-1. No 60-second canonical user-acceptance test.** Per §C-7. Add a
single procedure future-Tony can run in under a minute that verifies the
mod is doing the load-bearing thing. Suggested shape: "load save X,
controller in left hand, mouse on the desk, look around using both
simultaneously for 30 seconds. Glyphs must never flip from controller to
KBM. Press B once — ✕ prompt appears. Done." Failure means re-RE.

**G-2. No anti-cheat / DRM-vendor compatibility note.** Starfield ships
with Denuvo / VMP / Steamworks DRM. SFSE plugins generally work because
SFSE is itself a Steam-tolerated injection. But IAT-hooking `USER32!
ClipCursor` is the kind of operation that triggers behavioral signatures
in some anti-cheat / overlay systems. The doc should at minimum note
"this hook may be incompatible with Steam Big Picture mode's cursor
handling, NVIDIA GeForce Experience overlays, and Discord overlay" and
have a plan for detecting / disabling under those.

**G-3. No rollback / disable strategy.** If a hook crashes the game on
some user's machine, what's the user supposed to do? Currently: rename
the DLL or delete the plugin. That works but it's coarse. A INI-driven
per-hook enable/disable would let Tony triage user reports as "disable
hook 3 and try again." The doc lists 5 hooks in §8.5; that's a small
enough list that per-hook config is cheap.

**G-4. No PE-integrity check at plugin load.** The doc commits to
specific RVAs in `Starfield.exe` 1.16.236. If the user has a modified
Starfield.exe (cracked binary, patched anti-cheat shim, region-locked
build), the RVAs may resolve to garbage. Add a load-time SHA-256 / PE
timestamp check on the running process's main module against a known-
good 1.16.236 hash, and refuse to install hooks (with a loud log) if
the check fails.

**G-5. No threading-model documentation.** Engine input dispatch can
happen on multiple threads. The doc describes hook surfaces but does not
identify which thread invokes each hook target. Without that, the
plugin's shared state (`UsingThumbstickLook` etc.) cannot be reasoned
about for races. §F-3 is one symptom; the underlying problem is that the
doc never sat down with `Starfield.exe` and asked "what threads call
LookHandler::ShouldHandleEvent vs OnThumbstickEvent?" That belongs in §4
or §7.

**G-6. No "what to do when the AL DB is missing or stale" plan.**
SFSE's AL DB shipped via `versionlib-{ver}-0.bin` is a separate file
the user must install. If the user has v1.16.236 game but a v1.16.230
AL DB (because they didn't update versionlib), AL ID resolution
silently returns wrong RVAs and every hook either skips (best case) or
patches the wrong byte (worst case). The current plugin does not check
the AL DB version against the runtime version — it should refuse to
install hooks if the AL DB game version != runtime game version.

**G-7. No "future Starfield runtime patch" playbook.** When Bethesda
ships 1.16.237, what happens? The doc's §1 promises "Read it cold and
you should be able to rebuild the mod from scratch on a new patch
without going back to Nexus, the prior-art mods, or this commit
history." But the §8.5 hook list is hardcoded to 1.16.236 RVAs and AL
IDs. Future-Tony reading this doc on 1.16.237 has to re-derive
everything — the doc gives no procedure for doing so beyond "run
tools/derive_function_ids.py" which the doc itself notes is unreliable
(§A-7). Either commit to "this doc is for 1.16.236 only" and remove the
"rebuild from scratch on a new patch" promise, or add a re-derivation
procedure that produces verifiable evidence.

**G-8. No Stress / chaos test for the LookHandler vtable shim.** If
SteamInput floods the process with rapid-alternation events
(MouseMove/Thumbstick/MouseMove at 1kHz), does the shim's per-event
latch flip cleanly, or does the latched value race with consumer reads?
§F-3 calls out the atomic issue but the test plan does not exercise the
worst-case path.

**G-9. No integration test for "what happens when the plugin is loaded
mid-game via SFSE's `/sfse load`-equivalent."** Late loading is a real
SFSE workflow. Vtable patching is order-of-load-sensitive in ways that
load-time-only patching is not. Either commit to "load-only-at-startup"
explicitly or document the late-load behavior.

**G-10. No mention of which Starfield game features may interact
adversely.** Photo Mode, the in-game console (if SFSE provides one), the
ship-builder, the inventory grid — each has its own input handling
state. Does the plugin do the right thing in each? §9 covers some menus
(C8, C11, C12) but not photo mode, ship builder, console, or any modal
overlay. The doc should at minimum enumerate "modes we have / haven't
verified."

---

## §H. Verdict

**Send back for redesign.**

The doc is well-organized, has the right *shape* (intent → per-hook
analysis → vtable evidence → strategy → test plan), and demonstrates a
serious effort at understanding the engine. It is also load-bearing on
exactly one hypothesis — that the byte at RVA `0x05f67820` is
`ENGINE_GAMEPAD_ACTIVE` — and that hypothesis is **wrong** (§B-1, §E-1).
Until that hypothesis is replaced with verified evidence about what
*actually* drives the active-device state on 1.16.236, Strategy (b) is
not implementable, and the entire §8.5 recommended hook list collapses
to: keep hook 1 (verified, working), patch both byte sites in hook 2
(verified, well-defined), do an IAT hook on `ClipCursor` (under-
specified but tractable). That is a much smaller v2.0 than the doc
promises.

**Specific actions before this doc returns for re-review:**

1. **Find the actual `ENGINE_GAMEPAD_ACTIVE`, or prove it doesn't exist.**
   If the engine no longer has a single canonical flag — i.e. each
   consumer maintains its own state derived from event sinks — then
   accept that and rewrite §7 / §8 around per-consumer hooks. If the
   flag exists, find it via *runtime* instrumentation: log values at
   LookHandler entry, force a controller-to-mouse transition at the OS
   layer, observe which addresses change, work back to the writer. This
   work is a few hours; the static-disasm-only approach the doc took
   does not have the resolution to find a flag whose writer is reached
   via indirect store.

2. **Stop calling AL 139340 as a `BSInputDeviceManager*` predicate.**
   The current shipped `IsGamepadCursor()` consults a function with a
   mismatched signature on every cursor query. Either remove that
   indirection entirely (since hooks 8/9 are scheduled for deletion in
   §8.5 anyway) or, if those hooks are kept transiently, replace the
   call with a guaranteed-safe stub that returns a known constant.

3. **Patch the second `C6 43 08 01` byte site.** Drop the `break` in
   the byte-scan loop, patch every match in the bounded window. Doc
   §2.2 already calls for this; the code hasn't been updated.

4. **Tighten the doc's transcription discipline.** §B-2, B-3, B-4 are
   all instances where doc-rendered disassembly does not match the
   actual binary. Rerun every disasm listing through capstone before
   ship; add a footer to each listing that says "verified against
   Starfield.exe SHA-256 X on date Y."

5. **Rewrite §9 to include at least one *quantitative* test per
   invariant.** "Camera nudges" is not falsifiable. "Mouse delta of N
   produces yaw delta of M, with stick held at K" is.

6. **Add §G's missing pieces.** A 60-second user-acceptance test, a
   per-hook disable mechanism, a PE/AL-DB integrity check, and a
   future-runtime-patch playbook. None of these are large; their
   absence is the difference between a document that survives Tony's
   next 4-month gap from this project and one that doesn't.

The doc should NOT ship a v2.0 DLL until items 1-3 are resolved at
minimum. Items 4-6 are merge-with-fixes quality. The rest of the
critique items in §A-G are smaller and can be addressed in follow-up
revisions.

Once items 1-3 are addressed, the v2.0 strategy should be re-derived
from the new evidence — not patched on top of the current Strategy (b)
text. Re-deriving forces the author to confront whether the new evidence
actually supports the simpler "own one byte + RefreshPlatform"
architecture, or whether it requires the harder "own per-consumer
state" architecture. **Both are legitimate outcomes; the doc must arrive
at one of them honestly, not assume the easier one.**

---

## Three load-bearing critiques

If only three things from this critique are addressed, these are the
ones that determine whether v2.0 ships behavior or ships bugs:

1. **§B-1 / §E-1: The `ENGINE_GAMEPAD_ACTIVE` byte at `0x05f67820` is not
   a flag.** The hypothesis at the foundation of Strategy (b) is
   demonstrably wrong under disassembly. Without a correct identification
   of how the engine tracks active device on 1.16.236, no flag-pinning
   strategy can succeed. Find the real mechanism before recommending hooks
   that act on it.

2. **§F-1: The shipping v1.3.0 plugin invokes AL 139340 as a
   `BSInputDeviceManager*` predicate, but AL 139340 (per the doc itself
   in §2.3) is a function that reads its argument as an `InputEvent*`.**
   Every cursor visibility / cursor style query in the current build
   consumes a meaningless boolean derived from a type-mismatched call.
   This is a real, present, shipping bug — independent of the v2.0 plan
   — and must be fixed regardless of which v2.0 strategy is adopted.

3. **§C-1 / §C-2 / §C-7: The §9 acceptance test is a list of subjective
   observations, not a list of falsifiable measurements.** "Camera
   nudges via mouse delta" can be read as PASS by a tester regardless of
   whether the mouse delta was *added*, *substituted*, or *ignored
   while the stick was active*. Without a quantitative test, "v2.0
   passes acceptance" can mean "the tester saw motion" — which is
   exactly the failure mode that produced v1.3.0's situation, where the
   plugin loads, logs install-success, and does the wrong thing.
