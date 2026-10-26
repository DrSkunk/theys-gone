// Reverse-engineering aids behind DebugTools=1, including opt-in native-network experiments.
#pragma once
#include "ue.hpp"

#include <string>
#include <cstddef>

namespace coop::research {

namespace detail {

struct LifetimeProperty {
    uint16_t index;
    uint16_t padding;
    int32_t condition;
    int32_t notifyCondition;
};
static_assert(sizeof(LifetimeProperty) == 12);
static_assert(offsetof(LifetimeProperty, condition) == 4);
static_assert(offsetof(LifetimeProperty, notifyCondition) == 8);

struct LifetimeProperties {
    LifetimeProperty* data;
    int32_t num, max;
};
static_assert(sizeof(LifetimeProperties) == 16);

enum class LifetimeRegistration { added, present, invalid, allocationFailed };
using Reallocate = void* (*)(void*, size_t, uint32_t);

inline LifetimeRegistration RegisterLifetimeProperty(LifetimeProperties& properties, uint16_t index,
                                                      Reallocate reallocate) {
    if (properties.num < 0 || properties.max < 0 || properties.num > properties.max ||
        properties.max > 4096 || (properties.max && !properties.data))
        return LifetimeRegistration::invalid;
    for (int i = 0; i < properties.num; ++i)
        if (properties.data[i].index == index) return LifetimeRegistration::present;
    if (properties.num == properties.max) {
        if (properties.max == 4096) return LifetimeRegistration::invalid;
        int32_t capacity = properties.num + 1;
        auto data = reallocate ? static_cast<LifetimeProperty*>(
            reallocate(properties.data, capacity * sizeof(LifetimeProperty), 0)) : nullptr;
        if (!data) return LifetimeRegistration::allocationFailed;
        properties.data = data;
        properties.max = capacity;
    }
    properties.data[properties.num++] = {index, 0, 0, 0};
    return LifetimeRegistration::added;
}

} // namespace detail

bool DumpReflection(const std::wstring& path, std::string& summary);
bool DumpObjects(const std::wstring& path, std::string& summary);
// Reports the engine's built-in networking state: net drivers, roles, replicated actors and properties.
bool ProbeNetwork(const std::wstring& path, ue::UObject* localPawn, std::string& summary);
// Phase 1 listen-server experiments: run the engine's own `open` console command.
// controllerClass/pawnClass: optional class names for joining players (empty keeps the game's classes).
bool NetListen(const std::string& controllerClass, const std::string& pawnClass, bool spectators, std::string& summary);
// True once a research listen server runs: the Steam transport then accepts P2P session requests from
// anyone, because the engine's net driver connects through Steam P2P as well.
bool AcceptAnyP2PSession();
// True once net-listen or net-connect ran: the engine's Steam net driver reads P2P channel 0 itself, so the
// mod's Steam transport must not drain it.
bool EngineOwnsP2P();
// Called first in the ProcessEvent hook on the game thread. True drops the call: while the engine net driver
// runs, the level-streaming status RPCs are not sent in either direction (each PC runs its own world).
bool DropProcessEvent(ue::UObject* obj, ue::UObject* fn);
// Which level-streaming RPCs DropProcessEvent drops: the host's per-level status burst
// (ClientUpdateLevelStreamingStatus, ClientFlushLevelStreaming) and the guest's ServerUpdateLevelVisibility.
void SetStreamingRpcDrops(bool hostStatus, bool guestVisibility);
// Guest join: after MovePendingLevel clears World.NavigationSystem, enable bAllowClientSideNavigation on the
// navigation-system class and rerun the engine's InitializeForWorld, as LoadMap would on a navigating client.
void SetClientNavigation(bool enabled);
// Both sides, before net-listen/net-connect: replace empty Bend character overrides with stock ACharacter
// registration so engine-base fields replicate. Bend-specific CPF_Net fields may need separate registration.
void SetPawnReplication(bool enabled);
// Both sides, before the driver builds layouts: also register BendPawn.Health (COND_None).
// Frozen when PawnReplication installs its vtable wrapper; restart to change it.
void SetBendHealthReplication(bool enabled);
// Guest net-takeover: also run Bend's PlayerController Possess override locally for its client-side setup.
void SetClientPossess(bool enabled);
// Host: let the guest's character movement be client-authoritative (AGameNetworkManager::ClientAuthorativePosition).
void SetClientAuthoritativePosition(bool enabled);
// Host: spawn a pawn of pawnClass (empty: BendDefaultPlayerPawn_C) next to hostPawn and let the joined
// player's controller possess it.
bool NetPossess(ue::UObject* hostPawn, const std::string& pawnClass, std::string& summary);
// Guest: make the replicated controller the local player's controller, so this PC drives the pawn the host
// owns (thin-client test). Running it again gives the local player back its own controller.
bool NetTakeover(std::string& summary);
// Log roles, ownership (GetNetConnection), input and tick state of every player controller and player pawn.
bool NetDiag(std::string& summary);
// Guest: toggle the local controller's pawn between SimulatedProxy and a locally forced AutonomousProxy.
bool NetAutonomous(std::string& summary);
// Guest: call AddYawInput on the local controller, to test the input path without the viewport.
bool NetNudge(std::string& summary);
// First run: wrap APlayerController::InputKey/InputAxis to count viewport input per controller. Later runs log
// and reset the counts.
bool NetInput(std::string& summary);
// Feed AddMovementInput to the local controller's pawn every frame for 3 s and log how far it moved.
bool NetWalk(bool remote, bool consume, std::string& summary);
bool NetConnect(const std::string& host, std::string& summary);
void TraceStart();
bool TraceStop(const std::wstring& path, std::string& summary);
bool Tracing();
// Called from the ProcessEvent hook on the game thread.
void TraceEvent(ue::UObject* obj, ue::UObject* fn, void* parms);

// Mission lab. The ProcessEvent hook only sees events and native-to-script calls, not Blueprint-internal
// calls, so the watch also samples every job's completion/activity/state twice a second and logs diffs.
void MissionWatch(bool on);
bool MissionWatching();
void MissionWatchEvent(ue::UObject* obj, ue::UObject* fn, void* parms); // ProcessEvent hook, before the call
void MissionWatchTick(double now);
// Writes the job/mission managers' functions, every job's state, the active jobs' properties and the live
// job-script objects to path.
bool MissionState(const std::wstring& path, std::string& summary);
// Invokes one function of BendJobManager/BendGameMission (or "<Class>.<Function>" on a live instance of that
// class): spec = "<Function> [ObjectName|-] [Param=Value ...]". The first object parameter takes ObjectName;
// Bool/Byte/Int/Float/Object parameters can be named. Logs the return value and the immediate state diff.
bool MissionAct(const std::string& spec, bool allowed, std::string& summary);

} // namespace coop::research
