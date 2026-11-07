// Experimental ([Research] PawnEnemies=1): enemies shown on the other machine as pawns of their own class,
// driven the way a native client drives an AI pawn it does not simulate. The owner reads movement, the AI's
// move request, the anim state and one-off animations; the receiver spawns the pawn class without AI or
// collision and feeds it. See the research log (pawn mirror entries, 2026-11-06/07).
#pragma once
#include "enemy.hpp"
#include "ue.hpp"

#include <string>

namespace coop::standin {

// Owner side: what one enemy pawn is doing, read every update.
struct Reader {
    ue::UObject* pawn = nullptr;
    ue::UObject* anim = nullptr;
    ue::UObject* states = nullptr;
    ue::UObject* move = nullptr;
    ue::UObject* lastOneShot = nullptr;
    int32_t lastState = -1;
};

// Fills the movement, state and flags of `out` (not the header, position or yaw). `oneShot` is set to an
// AnimSequence the pawn started since the last read during an attack or grapple, else nullptr.
bool Read(Reader& reader, ue::UObject* pawn, EnemyMotionPacket& out, ue::UObject*& oneShot);

// The AI controller's current target pawn, or nullptr.
ue::UObject* CurrentTarget(ue::UObject* pawn);

// Receiver side: one stand-in pawn.
struct StandIn {
    ue::UObject* pawn = nullptr;
    ue::UObject* mesh = nullptr;
    ue::UObject* anim = nullptr;
    ue::UObject* states = nullptr;
    ue::UObject* move = nullptr;
    ue::FName slot{};
    int32_t state = -1;
    bool counterForMe = false; // the latest one-shot was aimed at the local player
    bool dead = false;
    double spawnedAt = 0;
    bool weaponsOff = false;
    int fields[9]{}; // movement component property offsets, resolved at spawn
};

// Spawns a pawn of `pawnClass` with `bodyMesh`, its AI controller removed and collision off.
bool Spawn(ue::UObject* context, ue::UObject* pawnClass, ue::UObject* bodyMesh, const ue::FVector& at, float yaw,
           StandIn& out, std::string& error);
void Feed(StandIn& s, const EnemyMotionPacket& motion, const ue::FVector& at, double now);
bool PlayOneShot(StandIn& s, ue::UObject* sequence, bool atLocalPlayer);
void Kill(StandIn& s);
void Destroy(StandIn& s);

// ProcessEvent filter: true swallows a stand-in's gameplay anim notify (claw arming, grapple attempts,
// damage modifiers, and counter prompts for attacks aimed at someone else), which on the owner's machine
// belong to the real pawn.
bool MuteNotify(ue::UObject* obj, ue::UObject* fn, void* parms);
bool Any();

} // namespace coop::standin
