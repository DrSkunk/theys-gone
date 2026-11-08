#include "standin.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace coop::standin {

using namespace ue;

namespace {

UObject* Next(UObject* field) { return *reinterpret_cast<UObject**>(reinterpret_cast<uint8_t*>(field) + off::FieldNext); }
UObject* Children(UObject* structure) {
    return *reinterpret_cast<UObject**>(reinterpret_cast<uint8_t*>(structure) + off::StructChildren);
}
int ElementSize(UObject* property) {
    return *reinterpret_cast<int32_t*>(reinterpret_cast<uint8_t*>(property) + off::PropertyElementSize);
}

// UStructProperty::Struct (build 19221447).
UObject* PropertyStruct(UObject* property) {
    constexpr int kPropertySubclassOffset = 0x78;
    if (!IsLive(property) ||
        !ReadableMemory(reinterpret_cast<uint8_t*>(property) + kPropertySubclassOffset, sizeof(UObject*))) return nullptr;
    UObject* value = nullptr;
    std::memcpy(&value, reinterpret_cast<uint8_t*>(property) + kPropertySubclassOffset, sizeof value);
    return IsLive(value) && IsLive(value->cls) && Name(value->cls) == "ScriptStruct" ? value : nullptr;
}

// The objects of a TArray<UObject*> property (UArrayProperty::Inner at the same offset, an ObjectProperty).
std::vector<UObject*> ObjectArray(UObject* object, const char* name) {
    constexpr int kArrayInnerOffset = 0x78;
    std::vector<UObject*> result;
    auto property = IsLive(object) ? FindChild(object->cls, name) : nullptr;
    if (!IsLive(property) || Name(property->cls) != "ArrayProperty" ||
        !ReadableMemory(reinterpret_cast<uint8_t*>(property) + kArrayInnerOffset, sizeof(UObject*))) return result;
    UObject* inner = nullptr;
    std::memcpy(&inner, reinterpret_cast<uint8_t*>(property) + kArrayInnerOffset, sizeof inner);
    if (!IsLive(inner) || !IsLive(inner->cls) || Name(inner->cls) != "ObjectProperty") return result;
    struct { UObject** data; int32_t count, max; } array{};
    std::memcpy(&array, reinterpret_cast<uint8_t*>(object) + PropertyOffset(property), sizeof array);
    if (array.count <= 0 || array.count > 256 || !array.data ||
        !ReadableMemory(array.data, static_cast<size_t>(array.count) * sizeof(UObject*))) return result;
    result.assign(array.data, array.data + array.count);
    return result;
}

bool Derives(UObject* structure, UObject* base) {
    for (int depth = 0; IsLive(structure) && depth < 16; structure = Super(structure), ++depth)
        if (structure == base) return true;
    return false;
}

// Per anim Blueprint class: each asset player's blend weight and asset offsets, and the full-body slot.
struct AnimLayout {
    std::vector<int> weights, assets;
    FName slot{};
};

const AnimLayout& LayoutOf(UObject* animClass) {
    static std::unordered_map<UObject*, AnimLayout> layouts;
    if (auto found = layouts.find(animClass); found != layouts.end()) return found->second;
    AnimLayout& layout = layouts[animClass];
    auto scriptStruct = FindClass("ScriptStruct");
    auto player = FindObject("AnimNode_AssetPlayerBase", scriptStruct);
    auto slotNode = FindObject("AnimNode_Slot", scriptStruct);
    auto stop = FindClass("BendAnimInstance");
    for (auto c = animClass; player && IsLive(c) && c != stop; c = Super(c)) {
        int budget = 0;
        for (auto field = Children(c); field && budget < 8192; field = Next(field), ++budget) {
            if (!IsLive(field) || Name(field->cls) != "StructProperty") continue;
            auto structure = PropertyStruct(field);
            int base = PropertyOffset(field);
            if (!structure || base < 0) continue;
            if (slotNode && Derives(structure, slotNode)) {
                // The one-shot replay goes into the full-body slot (the swarmer graph has UpperBody, FullBody, face).
                auto name = FindChild(structure, "SlotName");
                auto cdo = FindDefaultObject(Name(animClass).c_str());
                if (!name || !cdo) continue;
                FName value{};
                std::memcpy(&value, reinterpret_cast<const uint8_t*>(cdo) + base + PropertyOffset(name), sizeof value);
                auto text = value.index ? NameString(value.index) : std::string();
                if (!layout.slot.index || text.find("Full") != std::string::npos) layout.slot = value;
            } else if (Derives(structure, player)) {
                auto weight = FindChild(structure, "BlendWeight");
                auto asset = FindChild(structure, "Sequence");
                if (!asset) asset = FindChild(structure, "BlendSpace");
                if (!weight || !asset) continue;
                layout.weights.push_back(base + PropertyOffset(weight));
                layout.assets.push_back(base + PropertyOffset(asset));
            }
        }
    }
    return layout;
}

// The animation asset of the highest-weighted asset player, ignoring facial layers.
UObject* DominantAsset(UObject* anim) {
    const auto& layout = LayoutOf(anim->cls);
    auto base = reinterpret_cast<const uint8_t*>(anim);
    float best = 0.001f;
    UObject* asset = nullptr;
    for (size_t i = 0; i < layout.weights.size(); ++i) {
        float weight = 0;
        std::memcpy(&weight, base + layout.weights[i], sizeof weight);
        if (weight <= best) continue;
        UObject* candidate = nullptr;
        std::memcpy(&candidate, base + layout.assets[i], sizeof candidate);
        if (!IsLive(candidate) || Name(candidate).find("_face_") != std::string::npos) continue;
        best = weight;
        asset = candidate;
    }
    return asset;
}

bool InState(UObject* machine, int32_t state) {
    Call test(machine, "IsInStateEnum");
    return test.set("State", static_cast<uint8_t>(state)).invoke() && test.get<bool>("ReturnValue");
}

// The Bend anim-state enum of the machine's current state; the previous one is tried first.
int32_t CurrentState(UObject* machine, int32_t previous) {
    if (!IsLive(machine)) return -1;
    if (previous >= 0 && InState(machine, previous)) return previous;
    for (int32_t state = 0; state <= kEnemyMaxAnimState; ++state)
        if (state != previous && InState(machine, state)) return state;
    return -1;
}

UObject* AnimOf(UObject* pawn) {
    UObject* mesh = nullptr;
    if (!GetProp(pawn, "Mesh", mesh) || !IsLive(mesh)) return nullptr;
    Call anim(mesh, "GetAnimInstance");
    return anim.invoke() ? anim.get<UObject*>("ReturnValue") : nullptr;
}

constexpr int32_t kMeleeAttack = 5, kGrapple = 6, kPerformAction = 4;

// Movement fields a stand-in gets from its owner, in StandIn::fields order.
constexpr const char* kMoveFields[] = {"Velocity", "MovementMode", "Acceleration", "RequestedVelocity", "desiredSpeed",
                                       "MaxWalkSpeed", "DesiredRotation", "DeltaToDesiredRotation"};
constexpr int kMoveFieldSizes[] = {12, 1, 12, 12, 4, 4, 12, 12};

void WriteField(UObject* obj, int offset, const void* value, int size) {
    if (offset > 0) std::memcpy(reinterpret_cast<uint8_t*>(obj) + offset, value, static_cast<size_t>(size));
}

std::unordered_map<UObject*, StandIn*> g_byMesh;

} // namespace

UObject* CurrentTarget(UObject* pawn) {
    UObject* controller = nullptr;
    if (!GetProp(pawn, "Controller", controller) || !IsLive(controller)) return nullptr;
    Call current(controller, "GetCurrentTargetPawn");
    return current.invoke() ? current.get<UObject*>("ReturnValue") : nullptr;
}

bool Read(Reader& r, UObject* pawn, EnemyMotionPacket& out, UObject*& oneShot) {
    oneShot = nullptr;
    if (pawn != r.pawn || !IsLive(r.anim) || !IsLive(r.move)) {
        r = Reader{};
        r.pawn = pawn;
        r.anim = AnimOf(pawn);
        GetProp(pawn, "AnimStateMachine", r.states);
        GetProp(pawn, "CharacterMovement", r.move);
    }
    if (!IsLive(r.move) || !IsLive(r.anim)) return false;
    FVector v{};
    if (GetProp(r.move, "Velocity", v)) std::memcpy(out.velocity, &v, sizeof v);
    if (GetProp(r.move, "Acceleration", v)) std::memcpy(out.acceleration, &v, sizeof v);
    if (GetProp(r.move, "RequestedVelocity", v)) std::memcpy(out.requestedVelocity, &v, sizeof v);
    FRotator rot{};
    if (GetProp(r.move, "DesiredRotation", rot)) std::memcpy(out.desiredRotation, &rot, sizeof rot);
    if (GetProp(r.move, "DeltaToDesiredRotation", rot)) std::memcpy(out.deltaToDesiredRotation, &rot, sizeof rot);
    float speed = 0;
    if (GetProp(r.move, "desiredSpeed", speed) && std::isfinite(speed)) out.desiredSpeed = std::max(speed, 0.f);
    if (GetProp(r.move, "MaxWalkSpeed", speed) && std::isfinite(speed)) out.maxWalkSpeed = std::max(speed, 0.f);
    uint8_t mode = 0;
    GetProp(r.move, "MovementMode", mode);
    out.movementMode = mode <= 6 ? mode : 0;
    bool requested = false, grounded = false;
    GetBoolProp(r.move, "bHasRequestedVelocity", requested);
    GetBoolProp(r.anim, "bHasTouchedGroundAfterSpawningOrTeleporting", grounded);
    out.flags = static_cast<uint8_t>((requested ? kMotionRequested : 0) | (grounded ? kMotionGrounded : 0));
    r.lastState = CurrentState(r.states, r.lastState);
    out.state = static_cast<uint8_t>(r.lastState >= 0 ? r.lastState : 0);
    if (r.lastState == kMeleeAttack || r.lastState == kGrapple) {
        auto asset = DominantAsset(r.anim);
        // Blend spaces cannot play in a slot; only sequences are one-shots.
        if (asset && asset != r.lastOneShot && Name(asset->cls) == "AnimSequence") oneShot = asset;
        if (asset) r.lastOneShot = asset;
    } else {
        r.lastOneShot = nullptr;
    }
    return true;
}

bool Spawn(UObject* context, UObject* pawnClass, UObject* bodyMesh, const FVector& at, float yaw, StandIn& s,
           std::string& error) {
    auto library = FindDefaultObject("AIBlueprintHelperLibrary");
    Call spawn(library, "SpawnAIFromClass");
    if (!library || !spawn.set("WorldContextObject", context).set("PawnClass", pawnClass)
                         .set("BehaviorTree", static_cast<UObject*>(nullptr)).set("Location", at)
                         .set("Rotation", FRotator{0, yaw, 0}).setBool("bNoCollisionFail", true).invoke()) {
        error = "SpawnAIFromClass failed";
        return false;
    }
    s = StandIn{};
    s.pawn = spawn.get<UObject*>("ReturnValue");
    if (!IsLive(s.pawn)) {
        error = "SpawnAIFromClass returned no pawn";
        return false;
    }
    // The pawn's own AI would fight; a native client's copy has none.
    UObject* controller = nullptr;
    if (GetProp(s.pawn, "Controller", controller) && IsLive(controller)) {
        UObject* brain = nullptr;
        struct { const wchar_t* data = nullptr; int32_t num = 0, max = 0; } reason;
        if (GetProp(controller, "BrainComponent", brain) && IsLive(brain)) Call(brain, "StopLogic").set("Reason", reason).invoke();
        // Keeping the stopped controller did not make the missing attack sequences load (lab, 2026-11-07).
        Call(controller, "UnPossess").invoke();
        Call(controller, "K2_DestroyActor").invoke();
    }
    Call(s.pawn, "SetActorEnableCollision").setBool("bNewActorEnableCollision", false).invoke();
    GetProp(s.pawn, "Mesh", s.mesh);
    if (IsLive(s.mesh) && IsLive(bodyMesh)) Call(s.mesh, "SetSkeletalMesh").set("NewMesh", bodyMesh).invoke();
    s.anim = AnimOf(s.pawn);
    GetProp(s.pawn, "AnimStateMachine", s.states);
    GetProp(s.pawn, "CharacterMovement", s.move);
    if (!IsLive(s.mesh) || !IsLive(s.anim) || !IsLive(s.move)) {
        Destroy(s);
        error = "stand-in has no mesh, anim instance or movement";
        return false;
    }
    // Its own movement tick would integrate and overwrite the owner's values the anim graph reads.
    Call(s.move, "SetComponentTickEnabled").setBool("bEnabled", false).invoke();
    for (size_t i = 0; i < std::size(kMoveFields); ++i) {
        auto property = FindChild(s.move->cls, kMoveFields[i]);
        s.fields[i] = property && ElementSize(property) == kMoveFieldSizes[i] ? PropertyOffset(property) : -1;
    }
    s.slot = LayoutOf(s.anim->cls).slot;
    s.spawnedAt = 0;
    g_byMesh[s.mesh] = &s;
    return true;
}

void Feed(StandIn& s, const EnemyMotionPacket& m, const FVector& at, double now, bool verifyWeapons) {
    if (!IsLive(s.pawn) || !IsLive(s.move)) return;
    if (s.spawnedAt == 0) s.spawnedAt = now;
    g_byMesh[s.mesh] = &s; // the owning replica may have moved since spawning
    // Not a teleport: teleporting every frame holds the anim graph in its post-teleport reset.
    Call(s.pawn, "K2_SetActorLocationAndRotation").set("NewLocation", at).set("NewRotation", FRotator{0, m.yaw, 0})
        .setBool("bSweep", false).setBool("bTeleport", false).invoke();
    WriteField(s.move, s.fields[0], m.velocity, 12);
    WriteField(s.move, s.fields[1], &m.movementMode, 1);
    WriteField(s.move, s.fields[2], m.acceleration, 12);
    WriteField(s.move, s.fields[3], m.requestedVelocity, 12);
    WriteField(s.move, s.fields[4], &m.desiredSpeed, 4);
    WriteField(s.move, s.fields[5], &m.maxWalkSpeed, 4);
    WriteField(s.move, s.fields[6], m.desiredRotation, 12);
    WriteField(s.move, s.fields[7], m.deltaToDesiredRotation, 12);
    SetBoolProp(s.move, "bHasRequestedVelocity", (m.flags & kMotionRequested) != 0);
    if (IsLive(s.anim)) SetBoolProp(s.anim, "bHasTouchedGroundAfterSpawningOrTeleporting", (m.flags & kMotionGrounded) != 0);
    // PerformAction locked the stand-in's state machine in the lab, so it is not forwarded.
    if (m.state != s.state && m.state != kPerformAction && IsLive(s.states) && !s.dead) {
        Call(s.states, "RequestStateChange").set("State", m.state).setBool("ForceReenter", false).invoke();
        s.state = m.state;
    }
    // Its claws are separate weapon actors spawned with the pawn; their overlap would deal real damage. They are
    // looked up in the pawn's EquippedWeapons and, as actors it owns, its Children; a full object scan (50-70 ms,
    // lab and Steam Deck, 2026-11-08) is only the fallback when neither list has one.
    if (!s.weaponsOff && now - s.spawnedAt >= 0.5) {
        s.weaponsOff = true;
        static UObject* weaponClass = FindClass("BendWeapon");
        std::vector<UObject*> weapons;
        for (const char* list : {"EquippedWeapons", "Children"})
            for (auto weapon : ObjectArray(s.pawn, list))
                if (IsLive(weapon) && weaponClass && IsA(weapon, weaponClass) &&
                    std::find(weapons.begin(), weapons.end(), weapon) == weapons.end())
                    weapons.push_back(weapon);
        s.weaponsDirect = static_cast<int>(weapons.size());
        if (weapons.empty() || verifyWeapons) {
            // Stand-ins due in the same tick share one scan.
            static double scannedAt = -1;
            static std::unordered_multimap<UObject*, UObject*> weaponsOf;
            if (scannedAt != now) {
                scannedAt = now;
                weaponsOf.clear();
                for (int32_t i = 0; weaponClass && i < ObjectCount(); ++i) {
                    auto weapon = ObjectAt(i);
                    if (!IsLive(weapon) || !IsA(weapon, weaponClass) || IsDefaultOrArchetype(weapon)) continue;
                    UObject *owner = nullptr, *instigator = nullptr;
                    GetProp(weapon, "Owner", owner);
                    GetProp(weapon, "Instigator", instigator);
                    if (owner) weaponsOf.emplace(owner, weapon);
                    if (instigator && instigator != owner) weaponsOf.emplace(instigator, weapon);
                }
            }
            auto [first, last] = weaponsOf.equal_range(s.pawn);
            s.weaponsScanned = 0;
            for (auto it = first; it != last; ++it) {
                ++s.weaponsScanned;
                if (std::find(weapons.begin(), weapons.end(), it->second) == weapons.end()) weapons.push_back(it->second);
            }
        }
        for (auto weapon : weapons)
            if (IsLive(weapon)) Call(weapon, "SetActorEnableCollision").setBool("bNewActorEnableCollision", false).invoke();
    }
}

bool PlayOneShot(StandIn& s, UObject* sequence, bool atLocalPlayer) {
    if (!IsLive(s.anim) || !IsLive(sequence) || !s.slot.index || s.dead) return false;
    s.counterForMe = atLocalPlayer;
    Call play(s.anim, "PlaySlotAnimationAsDynamicMontage");
    play.set("Asset", sequence).set("SlotNodeName", s.slot).set("BlendInTime", 0.1f).set("BlendOutTime", 0.2f)
        .set("InPlayRate", 1.f).set("LoopCount", 1).set("BlendOutTriggerTime", -1.f);
    return play.invoke() && IsLive(play.get<UObject*>("ReturnValue"));
}

void Kill(StandIn& s) {
    if (s.dead || !IsLive(s.pawn)) return;
    s.dead = true;
    // The game's own death (OnDied, ragdoll); ApplyDamage is scaled down by the game and does not kill.
    Call(s.pawn, "ForceKill").set("Killer", static_cast<UObject*>(nullptr)).setBool("forceRagdoll", false).invoke();
}

void Destroy(StandIn& s) {
    g_byMesh.erase(s.mesh);
    if (IsLive(s.pawn)) Call(s.pawn, "K2_DestroyActor").invoke();
    s = StandIn{};
}

bool Any() { return !g_byMesh.empty(); }

bool MuteNotify(UObject* obj, UObject* fn, void* parms) {
    if (g_byMesh.empty() || !parms) return false;
    static const int names[4] = {FindNameIndex("Received_Notify"), FindNameIndex("Received_NotifyBegin"),
                                 FindNameIndex("Received_NotifyTick"), FindNameIndex("Received_NotifyEnd")};
    if (std::find(std::begin(names), std::end(names), fn->name.index) == std::end(names)) return false;
    auto found = g_byMesh.find(*static_cast<UObject**>(parms)); // MeshComp is the first parameter
    if (found == g_byMesh.end()) return false;
    // 0 keep, 1 gameplay (always muted), 2 counter window or prompt (kept for attacks on the local player).
    static std::unordered_map<UObject*, int> kinds;
    auto kind = kinds.find(obj->cls);
    if (kind == kinds.end()) {
        auto name = Name(obj->cls);
        int value = 0;
        for (const char* gameplay : {"AnimNotify_MeleeArmed_C", "AnimNotify_AttemptGrapple_C", "AnimNotifyState_DamageModifier_C"})
            if (name == gameplay) value = 1;
        for (const char* counter : {"AnimNotify_CounterIcon_C", "AnimNotify_CanCounterMyAttack_C",
                                    "AnimNotify_CanCounterMyAttack_State_C", "AnimNotify_CanProvokeCounterMyAttack_State_C"})
            if (name == counter) value = 2;
        kind = kinds.emplace(obj->cls, value).first;
    }
    return kind->second == 1 || (kind->second == 2 && !found->second->counterForMe);
}

} // namespace coop::standin
