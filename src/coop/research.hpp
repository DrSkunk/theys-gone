// Read-only reverse-engineering aids behind DebugTools=1: reflection dumps, live object
// census and a ProcessEvent call trace. Nothing here changes game state.
#pragma once
#include "ue.hpp"

#include <string>

namespace coop::research {

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
// Host: spawn a pawn of pawnClass (empty: BendDefaultPlayerPawn_C) next to hostPawn and let the joined
// player's controller possess it.
bool NetPossess(ue::UObject* hostPawn, const std::string& pawnClass, std::string& summary);
// Guest: make the replicated controller the local player's controller, so this PC drives the pawn the host
// owns (thin-client test). Running it again gives the local player back its own controller.
bool NetTakeover(std::string& summary);
bool NetConnect(const std::string& host, std::string& summary);
void TraceStart();
bool TraceStop(const std::wstring& path, std::string& summary);
bool Tracing();
// Called from the ProcessEvent hook on the game thread.
void TraceEvent(ue::UObject* obj, ue::UObject* fn, void* parms);

} // namespace coop::research
