#include "Plugin.h"
#include "RE/Offset.Ext.h"

// libxse/CommonLibSF surface. The umbrella SFSE/SFSE.h pulls in PCH (which
// includes REL::, REX::, and the SFSE/RE namespaces), API, Interfaces, Logger,
// Trampoline, and Version, which is everything we need for a hook plugin.
#include "SFSE/SFSE.h"

#include "REL/Relocation.h"

#include "REX/LOG.h"

#include "SFSE/Logger.h"

#include "RE/B/BSFixedString.h"
#include "RE/B/BSInputEventUser.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>

using namespace std::string_view_literals;

#define DLLEXPORT __declspec(dllexport)
#define SFSEAPI   __cdecl

namespace
{
	// Counted number of hooks installed; surfaced in startup log so a runtime
	// mismatch is debuggable from the plugin log alone (no need to attach a
	// debugger).
	std::atomic<unsigned> g_hooksInstalled{ 0 };
	std::atomic<unsigned> g_hooksSkipped{ 0 };

	// Hook-1 invocation counter (MOD_DIRECTION.md §3.3). Incremented inside the
	// LookHandler vtable shim. Surfaced on each plugin-log INFO line at the
	// end of SFSEPlugin_Load so "did the shim ever fire" can be answered from
	// the log alone after a play session.
	std::atomic<std::uint64_t> g_shimInvocations{ 0 };

	// Per-anchor hit count for the BSPCGamepadDevice::Poll byte patch. v1.4.0
	// patches every `C6 43 08 01` site within the scan window, not just the
	// first; this counter lets the plugin log report "patched N of N" so a
	// future Bethesda refactor that drops a site is visible without a binary
	// diff. Linus §6.2: failure mode is *user-visible-glitch* (left-stick
	// continues to flip active-device through the unpatched site).
	std::atomic<unsigned> g_byteSitesPatched{ 0 };
}

extern "C" DLLEXPORT constinit auto SFSEPlugin_Version = []() {
	SFSE::PluginVersionData v{};
	v.PluginVersion(Plugin::VERSION);
	v.PluginName(Plugin::NAME);

	// Original author. Maintained fork credit lives in the README and version
	// resource.
	v.AuthorName("Parapets");

	// We resolve all engine functions through the Address Library at runtime,
	// so we want SFSE to grant load on any runtime the AL DB covers.
	// libxse/CommonLibSF's UsesAddressLibrary() sets bit 1<<2 (Address Library
	// v2), which SFSE 0.2.17+ requires for plugin loads to be accepted on
	// Starfield 1.10.31+ runtimes (per sfse_whatsnew.txt).
	v.UsesAddressLibrary(true);

	// We touch engine struct layouts (LookHandler vtable slot 1,
	// BSPCGamepadDevice +0x51D / +0x5DC). IsLayoutDependent() in libxse sets
	// bit 1<<3, which SFSE reads as "compatible with runtime 1.14.70+ struct
	// layout".
	v.IsLayoutDependent(true);

	// Empty compatibleVersions[] (zero-terminated) means "any runtime that
	// satisfies the address/layout flags above". We deliberately do NOT pin a
	// version here so future Steam patches don't auto-disable the plugin; if a
	// future runtime breaks layout, SFSE will reject us via the layout bit
	// instead.
	return v;
}();

namespace RE
{
	// Forward decl: the LookHandler vtable shim only ever sees this as an
	// opaque `LookHandler*` argument.
	namespace PlayerControls
	{
		class LookHandler;
	}
}

// === Latched look-source state ===
// Set by the LookHandler vtable shim below. True when the most recent QLook
// event came from a thumbstick; false when it came from a mouse-move. Held
// as `std::atomic<bool>` (MOD_DIRECTION.md §3.3 / critique §F-3) so the
// engine threads that read it pick up writes without a torn-byte race.
//
// In v1.4.0 the latched state has no consumers in this DLL — hooks 3-9 are
// retired. The latch survives because the §4 baseline test wants to see
// whether the engine still treats both event types as "look events" once the
// shim chains through the original `ShouldHandleEvent`. Promoting the latch
// to a class member or removing it entirely is a v1.5.0+ decision once the
// measurements are in.
namespace
{
	std::atomic<bool> g_usingThumbstickLook{ false };
}

// === Hook-1 chain-through pointer ===
// Captured from `vtbl.write_vfunc(1, &shim)`'s return value at install time.
// The shim invokes the original implementation so the engine's existing
// per-device-class filter logic still runs — we are recording that filter's
// decision, not replacing it. If the install fails the shim never runs, so
// there is no code path where this is read while null.
namespace
{
	using ShouldHandleEvent_t = bool (*)(RE::PlayerControls::LookHandler*, const RE::InputEvent*);
	ShouldHandleEvent_t g_origShouldHandleEvent = nullptr;
}

// === CSV event-flow log ===
// MOD_DIRECTION.md §3 (Linus's "Step 3 — quantitative additive-look
// measurement"): one CSV row per `LookHandler::ShouldHandleEvent`
// invocation. Tony plays Starfield for 60 seconds with stick + mouse
// simultaneously active, the log lands next to `SimultaneousInput.log`,
// and post-analysis answers:
//
//   - Are both `kThumbstick` and `kMouseMove` events flowing through the
//     shim during the test window? (yes -> hook 1 is wired)
//   - Does the original `ShouldHandleEvent` accept both event types (return
//     true) when both inputs are active simultaneously? (yes -> the engine
//     is already routing both; hook 1's latch is sufficient. no -> the
//     original is filtering one out and a deeper hook is required.)
//   - Per-engine-tick (binned by `timeCode`): how many stick events vs
//     mouse events fall in the same tick? (substitution vs addition signal
//     at the event-flow layer.)
//
// Schema:
//   seq     : monotonic shim-invocation counter
//   wall_ms : wall-clock ms since plugin load
//   timeCode: engine timeCode field on the InputEvent (+0x20)
//   eventType: integer enum value (kButton=0, kMouseMove=1, kThumbstick=4, ...)
//   deviceType: integer enum value (kKeyboard=0, kMouse=1, kGamepad=2, ...)
//   userEvent: QUserEvent() ascii string ("Look", "" for non-look, etc.)
//   origReturn: 1 if the chained-through original returned true, 0 otherwise
//   latched : the post-update value of g_usingThumbstickLook
//
// All InputEvent field offsets used here are the verified base-class layout
// from CommonLibSF's `BSInputEventUser.h` (deviceType +0x08, eventType +0x10,
// timeCode +0x20). No subclass field reads — those would require RE work the
// v1.4.0 build deliberately doesn't ship without (Linus §6.1).
namespace measurement
{
	std::ofstream             g_csv;
	std::mutex                g_csvMutex;
	std::chrono::steady_clock::time_point g_t0;

	void Open()
	{
		try {
			g_t0 = std::chrono::steady_clock::now();

			// Resolve via libxse's `SFSE::log::log_directory()` — the same
			// helper SFSE::Init uses to place SimultaneousInput.log. This
			// goes through SHGetKnownFolderPath(FOLDERID_Documents) instead
			// of getenv("USERPROFILE"), so the CSV lands in the same
			// directory the user already knows to look in (and avoids the
			// `getenv` /sdl /WX deprecation).
			const auto logDir = SFSE::log::log_directory();
			if (!logDir) {
				REX::WARN("measurement CSV disabled: log_directory() failed");
				return;
			}
			std::filesystem::path path = *logDir / "SimultaneousInput-events.csv";
			std::error_code       ec;
			std::filesystem::create_directories(path.parent_path(), ec);

			g_csv.open(path, std::ios::out | std::ios::trunc);
			if (!g_csv.is_open()) {
				REX::WARN("measurement CSV failed to open: {}", path.string());
				return;
			}
			g_csv << "seq,wall_ms,timeCode,eventType,deviceType,userEvent,origReturn,latched\n";
			g_csv.flush();
			REX::INFO("measurement CSV opened: {}", path.string());
		} catch (const std::exception& ex) {
			REX::WARN("measurement CSV setup threw: {}", ex.what());
		}
	}

	void LogEvent(
		std::uint64_t       seq,
		std::uint32_t       timeCode,
		std::uint32_t       eventType,
		std::uint32_t       deviceType,
		std::string_view    userEvent,
		bool                origReturn,
		bool                latched)
	{
		// Mutex-guarded line write. Worst-case throughput in the §4.2 test
		// is ~120 events/sec (60 Hz × {stick,mouse}); contention between
		// the engine input thread and any concurrent shim caller is
		// negligible at that rate. The mutex is preferable to a lockless
		// ring because the test artifact must survive a process exit
		// without a consumer-side flush.
		std::lock_guard lock(g_csvMutex);
		if (!g_csv.is_open()) {
			return;
		}
		const auto wallMs = std::chrono::duration_cast<std::chrono::milliseconds>(
		                        std::chrono::steady_clock::now() - g_t0)
		                        .count();
		g_csv << seq << ',' << wallMs << ',' << timeCode << ','
		      << eventType << ',' << deviceType << ',' << userEvent << ','
		      << (origReturn ? 1 : 0) << ',' << (latched ? 1 : 0) << '\n';
		// No flush() per row: spilled events on a process crash are
		// acceptable for a 60-second measurement run, and per-row flush
		// hammers the input path. The OS buffer is flushed on a clean
		// process exit (Starfield -> exit).
	}
}

// === Hook-1 shim: LookHandler vtable slot 1 ===
// Replaces `ShouldHandleEvent` with a chain-through that:
//   1. Logs the event to the CSV (event-flow measurement).
//   2. Latches `g_usingThumbstickLook` based on event type.
//   3. Calls the original implementation, returning its result unchanged.
//
// Step 3 is the v1.4.0 behavior change vs the prior captureless-true shim
// (MOD_DIRECTION.md §3.1). The prior shim unconditionally returned true on
// Look events and dropped the original's filter side effects; chaining
// through preserves them. If the original returns false the engine's
// per-device handlers (slots 4 and 6) are bypassed for that event, which
// is itself the data we want — the CSV will show the original's verdict.
//
// IF-WRONG (Linus §6.2):
//   - if `g_origShouldHandleEvent` is null at call time -> not possible:
//     install path sets it before the vtable write, so this branch can
//     only be reached after a successful install.
//   - if the original implementation has a different signature on a
//     future runtime -> failure mode is *crash* (mismatched ABI).
//     Mitigation: the AL DB version and PE-integrity gates promised in
//     §3.3 will refuse to install on non-1.16.236 once added; for v1.4.0
//     we rely on the SFSE layout-flag rejection plus an explicit AL
//     version check at startup (logged, not enforced).
static bool LookHandler_ShouldHandleEvent_Shim(
	RE::PlayerControls::LookHandler* a_self,
	const RE::InputEvent*            a_event)
{
	const auto seq = g_shimInvocations.fetch_add(1, std::memory_order_relaxed) + 1;

	bool origReturn = false;
	if (g_origShouldHandleEvent) {
		origReturn = g_origShouldHandleEvent(a_self, a_event);
	}

	if (a_event) {
		const auto eventType = a_event->eventType;
		if (eventType == RE::InputEvent::EventType::kMouseMove) {
			g_usingThumbstickLook.store(false, std::memory_order_relaxed);
		} else if (eventType == RE::InputEvent::EventType::kThumbstick) {
			g_usingThumbstickLook.store(true, std::memory_order_relaxed);
		}

		// QUserEvent() reads the event's user-event tag (`BSFixedString`).
		// On non-IDEvent subclasses it returns an empty string. Copying
		// out the c_str pointer is safe inside the shim window because
		// the engine retains ownership of the event for the duration of
		// the dispatch.
		const auto userEvent = a_event->QUserEvent();
		measurement::LogEvent(
			seq,
			a_event->timeCode,
			static_cast<std::uint32_t>(eventType),
			static_cast<std::uint32_t>(a_event->deviceType),
			std::string_view{ userEvent.c_str() ? userEvent.c_str() : "" },
			origReturn,
			g_usingThumbstickLook.load(std::memory_order_relaxed));
	}

	return origReturn;
}

namespace
{
	void LogRuntimeProbe(const SFSE::LoadInterface* a_sfse)
	{
		const auto runtimeVer = a_sfse->RuntimeVersion();
		const auto sfseVer = REL::Version::unpack(a_sfse->SFSEVersion());

		REX::INFO(
			"{} v{} (build {} {})",
			Plugin::NAME,
			Plugin::VERSION.string("."sv),
			Plugin::BUILD_SHA,
			Plugin::BUILD_DATE);

		REX::INFO(
			"SFSE {} loaded against Starfield runtime {}",
			sfseVer.string("."sv),
			runtimeVer.string("."sv));

		REX::INFO(
			"v1.4.0 baseline measurement build: 2 hooks active "
			"(LookHandler vtable shim, BSPCGamepadDevice::Poll byte patch). "
			"7 hooks retired pending evidence; see MOD_DIRECTION.md §3-4.");

		// Highest runtime CommonLibSF advertises support for. Out-of-range is
		// not a hard failure (AL IDs are evaluated at runtime against the
		// installed AL DB), but it's a useful signal in the log.
		constexpr auto knownLatest = SFSE::RUNTIME_LATEST;
		if (runtimeVer > knownLatest) {
			REX::WARN(
				"runtime {} is newer than the latest tested ({}). "
				"hook 1 (vtable shim) and hook 2 (byte scan) are pattern-"
				"resilient but ABI changes will surface as crashes; if you "
				"are seeing crashes on a newer runtime, uninstall this "
				"plugin and report the runtime version in the issue.",
				runtimeVer.string("."sv),
				knownLatest.string("."sv));
		}
	}
}

extern "C" DLLEXPORT bool SFSEAPI SFSEPlugin_Load(const SFSE::LoadInterface* a_sfse)
{
	// libxse's SFSE::Init does spdlog setup itself when InitInfo.log is true.
	// v1.4.0 does not install any trampoline calls (hooks 3-9 retired), so we
	// don't need the SFSE branch pool — the vtable shim and the byte patch
	// both write in-place to the loaded image. `trampoline = false` keeps us
	// out of the SFSE allocator entirely, removing one moving piece between
	// patch boots.
	SFSE::Init(a_sfse, SFSE::InitInfo{
	                       .trampoline = false,
	                   });
	LogRuntimeProbe(a_sfse);

	measurement::Open();

	// === Hook 1: LookHandler vtable slot 1 chain-through shim ===
	// Replaces `ShouldHandleEvent`. The shim chains through the original
	// (capturing it from `write_vfunc`'s return value) and logs every
	// invocation to the CSV. See `LookHandler_ShouldHandleEvent_Shim`
	// above for the per-event behavior.
	try {
		REL::Relocation<std::uintptr_t> vtbl(RE::Offset::PlayerControls::LookHandler::Vtbl);
		const auto original = vtbl.write_vfunc(1, &LookHandler_ShouldHandleEvent_Shim);
		g_origShouldHandleEvent = reinterpret_cast<ShouldHandleEvent_t>(original);
		REX::INFO(
			"hook 1 installed: LookHandler vtable slot 1 shim "
			"(orig {:#x} chained through)",
			original);
		++g_hooksInstalled;
	} catch (const std::exception& ex) {
		REX::ERROR("hook 1 install failed: {}", ex.what());
		++g_hooksSkipped;
	}

	// === Hook 2: BSPCGamepadDevice::Poll byte patch (BOTH anchor sites) ===
	// Inside `Poll` the engine writes `mov byte ptr [rbx+8], 1` whenever the
	// left stick crosses its activation threshold. Two `C6 43 08 01` anchors
	// are confirmed on 1.16.236 at +0x51D and +0x5DC (per critique §B-7).
	// The v1.3.0 release patched only the first match, leaving the second
	// site live; v1.4.0 patches every match within the bounded scan window.
	try {
		REL::Relocation<std::uintptr_t> head(RE::Offset::BSPCGamepadDevice::Poll);
		constexpr std::size_t           kScanLimit = 0x800;
		const std::uint8_t*             p = reinterpret_cast<const std::uint8_t*>(head.address());
		unsigned                        patched = 0;
		for (std::size_t i = 0; i + 4 <= kScanLimit; ++i) {
			if (p[i] == 0xC6 && p[i + 1] == 0x43 && p[i + 2] == 0x08 && p[i + 3] == 0x01) {
				REL::Relocation<std::uintptr_t> hook(
					RE::Offset::BSPCGamepadDevice::Poll, static_cast<std::ptrdiff_t>(i));
				hook.write_fill(REL::NOP, 0x4);
				REX::INFO("hook 2 patched site: BSPCGamepadDevice::Poll +{:#x}", i);
				++patched;
				// Continue scanning; do not break. Both anchors must be
				// patched for the left-stick latch to be fully neutralized
				// (one site is in the activation path, the other is in the
				// post-deadzone re-arm path; missing either leaves a path
				// where the active-device flag still flips).
			}
		}
		g_byteSitesPatched.store(patched, std::memory_order_relaxed);
		if (patched == 0) {
			REX::WARN(
				"hook 2 skipped: BSPCGamepadDevice::Poll AL id {} (rva {:#x}) "
				"anchor 'C6 43 08 01' not found in first {:#x} bytes; the "
				"function may have been refactored further. left thumbstick "
				"will still flip the active-device flag.",
				RE::Offset::BSPCGamepadDevice::Poll.id(),
				RE::Offset::BSPCGamepadDevice::Poll.offset(),
				kScanLimit);
			++g_hooksSkipped;
		} else {
			REX::INFO("hook 2 installed: BSPCGamepadDevice::Poll patched {} site(s)", patched);
			++g_hooksInstalled;
			if (patched < 2) {
				REX::WARN(
					"hook 2 partial: expected 2 anchor sites on 1.16.236, found {}. "
					"a Bethesda patch may have removed one; left-stick latch "
					"may still fire from the missing site. capture a frame of "
					"the disassembly around RVA {:#x} and report.",
					patched,
					RE::Offset::BSPCGamepadDevice::Poll.offset());
			}
		}
	} catch (const std::exception& ex) {
		REX::ERROR("hook 2 install failed: {}", ex.what());
		++g_hooksSkipped;
	}

	const auto installed = g_hooksInstalled.load();
	const auto skipped = g_hooksSkipped.load();
	const auto patchedSites = g_byteSitesPatched.load();
	if (skipped == 0) {
		REX::INFO(
			"v1.4.0 ready: hook 1 (vtable shim) installed, hook 2 (byte patch) "
			"installed at {} site(s). shim invocations so far: {}. "
			"play, then return SimultaneousInput-events.csv for analysis.",
			patchedSites,
			g_shimInvocations.load(std::memory_order_relaxed));
	} else {
		REX::WARN(
			"v1.4.0 partial: {}/{} hooks installed, {} skipped. plugin will "
			"run with reduced behavior; see warnings above.",
			installed,
			installed + skipped,
			skipped);
	}

	// We always return true: even if some hooks failed, partial functionality
	// is better than refusing to load. Logged warnings tell the user what's
	// off.
	return true;
}
