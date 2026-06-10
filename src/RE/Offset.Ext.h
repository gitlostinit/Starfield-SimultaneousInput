#pragma once

#include "REL/Relocation.h"

// Address Library IDs used by the v1.4.0 baseline measurement build.
//
// Per MOD_DIRECTION.md §3 (Linus, 2026-05-06): everything except the
// LookHandler vtable shim and the BSPCGamepadDevice::Poll byte patch is
// retired. Hooks 3-9 either depended on the demolished
// `ENGINE_GAMEPAD_ACTIVE` keystone hypothesis (§B-1 of critique) or, in the
// case of hooks 8/9, shipped a type-mismatched `IsUsingGamepad` call that
// fed garbage to a cursor-decision predicate every time stick-look was
// active. None of those return until the §4 baseline test produces
// evidence that justifies adding a specific one back.
namespace RE
{
	namespace Offset
	{
		// BSPCGamepadDevice::Poll. Inside this function the engine writes
		// `mov byte ptr [rbx+8], 1` whenever the left stick crosses its
		// activation threshold, latching the active-device flag to the
		// gamepad. We NOP every `C6 43 08 01` match within the bounded
		// scan so the latch never fires from the stick.
		//
		// AL ID 124384 resolves to RVA 0x2302bc0 on Starfield 1.16.236.
		// Two anchor sites are confirmed at +0x51D and +0x5DC (see §B-7
		// of MOD_ARCHITECTURE_CRITIQUE.md). The runtime scanner walks the
		// first 0x800 bytes of the function body and patches every match,
		// so this hook survives further minor refactors without a code
		// change.
		namespace BSPCGamepadDevice
		{
			constexpr REL::ID Poll{ 124384 };
		}

		// UserEvents::QLook — getter returning the interned "Look"
		// `BSFixedString*` the engine compares user-event tags against.
		//
		// AL 74548 resolves to RVA 0xf9bd60 on Starfield 1.16.242. Verified
		// by direct disasm (2026-06-09): the original
		// `LookHandler::ShouldHandleEvent` (RVA 0x12bcd80, AL 82236) begins
		// with `call 0xf9bd60`, then compares the result's first qword
		// against `[event->vfunc[2](event)]` — i.e. the engine itself uses
		// this getter plus an interned-data-pointer compare to classify an
		// event as a Look event. The v1.5.0 force path replicates exactly
		// that recipe. Re-verify with:
		//   python3 tools/al_db_parser.py --db versionlib-1-16-242-0.bin \
		//     --exe Starfield.exe  (then disasm RVA 0x12bcd80)
		namespace UserEvents
		{
			constexpr REL::ID QLook{ 74548 };
		}

		// PlayerControls::LookHandler vtable.
		//
		// Slot 1 is `ShouldHandleEvent(const InputEvent*)`. The original
		// implementation gates the look pipeline on whatever the engine
		// considers the current active device; a captureless shim
		// chains through the original, latches `UsingThumbstickLook`
		// based on the event's `eventType`, and logs the event to a
		// CSV. The chain-through is deliberate per §3.1: on 1.16.236
		// the per-device-class handlers (slots 4 and 6) still exist as
		// separate entry points, so the original may already let both
		// stick and mouse events through. We measure first.
		//
		// AL 433589 is the libxse-canonical 1.16.236 ID for this vtable.
		// The 1.8.86 fork's 407288 resolves to a non-vtable region on
		// 1.16.236 and would silently overwrite eight bytes of unrelated
		// data; do not regress to 407288 without re-verifying in
		// `external/CommonLibSF/include/RE/IDs_VTABLE.h`.
		namespace PlayerControls
		{
			namespace LookHandler
			{
				constexpr REL::ID Vtbl{ 433589 };
			}
		}
	}
}
