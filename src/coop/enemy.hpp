#pragma once

#include "pose.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace coop {

// Enemy poses travel as kPoseEnemy frames, appearance as EnemyInfoPacket. Every enemy has exactly one owner:
// the machine whose game simulates it. Entity ids belong to the owner, so each side's replicas hold only the
// other side's ids, and a hit on a replica is sent to that replica's owner. During free roam the guest owns
// its region's enemies; when the regions merge or split, enemies already fighting a player stay with their
// owner until they die or lose interest (KeepHandedEnemy) instead of vanishing.
constexpr uint32_t kEnemyInfoMagic = 0x4E454D44; // "DMEN"
constexpr uint16_t kEnemyInfoVersion = 2;
constexpr uint8_t kEnemyInfoFriendly = 1; // a story companion: shown, never hit or handed over
constexpr size_t kEnemyMeshPaths = 4;      // body plus up to three modular parts
constexpr size_t kEnemyPathLength = 120;
constexpr size_t kEnemyMaxBones = 159; // at most three packets per enemy, including @root
constexpr size_t kEnemyMaxStreamed = 16;
constexpr float kEnemyRadius = 8000.f;     // 80 m around either player
constexpr double kEnemySendInterval = 1.0 / 15;
constexpr double kEnemyInfoInterval = 1.0;
constexpr double kEnemyTimeout = 1.5;

// With pawn stand-ins an enemy costs one 112-byte motion update instead of up to three pose packets, so more
// are streamed; bone poses stay limited to the nearest kEnemyMaxStreamed (the fallback when the receiver
// shows no stand-in, and corpses).
constexpr size_t kEnemyMaxStandIns = 64;
constexpr float kStandInNearRadius = 3000.f; // 30 m: full rate inside, a third of it beyond
constexpr double kStandInInfoInterval = 5.0;  // EnemyInfo for an acknowledged stand-in

inline size_t EnemyStreamCap(bool standIns) { return standIns ? kEnemyMaxStandIns : kEnemyMaxStreamed; }

// Every how many enemy updates a stand-in's motion is sent, by squared distance to the nearest player (a
// negative distance marks an enemy kept in a fight, always near).
inline int StandInSendEvery(float distanceSq) {
    return distanceSq <= kStandInNearRadius * kStandInNearRadius ? 1 : 3;
}

// Whether an enemy, by its rank in the distance-sorted selection, may fall back to bone poses.
inline bool PoseFallbackAllowed(size_t rank) { return rank < kEnemyMaxStreamed; }

constexpr double kHandoffCalm = 6.0;   // an enemy that ignores its player this long goes back to normal rules
constexpr double kHandoffMax = 180.0;  // upper bound on keeping an enemy past an ownership change
constexpr double kHandoffAbsence = 30.0; // longest stand-in rebuild that still counts as the same fight

// Whether an enemy was fighting the partner when ownership changes. While a partner proxy is absent after
// an avatar rebuild, the fight is judged as of `presentAt`, the last time the target was available.
inline bool FightingAtHandoff(double onGuest, double now, bool playerHere, double presentAt) {
    double reference = playerHere || now - presentAt > kHandoffAbsence ? now : presentAt;
    return reference - onGuest <= kHandoffCalm;
}

// The guest sends its own enemies while it owns a region, and afterwards while it still keeps some of them.
inline bool EnemyStreamSendAllowed(bool host, bool peerEnabled, bool separated, bool keeping) {
    return peerEnabled && (host || separated || keeping);
}

// The host shows whatever the guest owns; the guest always shows the host's enemies.
inline bool EnemyStreamReceiveAllowed(bool host, bool peerEnabled) {
    return !host || peerEnabled;
}

inline bool PartnerProxyWanted(bool host, bool peerReady, bool freeRoam, bool separated, bool keeping) {
    return peerReady && (host || (freeRoam && !separated && keeping));
}

// Whether an enemy kept across an ownership change stays with its owner. `engaged` is the last time it
// targeted the player it was fighting and `since` when the ownership change happened.
inline bool KeepHandedEnemy(bool alive, double engaged, double since, double now) {
    return alive && now - engaged < kHandoffCalm && now - since < kHandoffMax;
}

#pragma pack(push, 1)
struct EnemyInfoPacket {
    uint32_t magic;
    uint16_t version;
    uint8_t paths, flags;
    uint32_t session, entity;
    char path[kEnemyMeshPaths][kEnemyPathLength]; // "Package.Asset" object paths of SkeletalMesh assets
};
#pragma pack(pop)

inline bool DecodeEnemyInfo(const char* data, int len, EnemyInfoPacket& out) {
    if (len != static_cast<int>(sizeof(EnemyInfoPacket))) return false;
    std::memcpy(&out, data, sizeof out);
    if (out.magic != kEnemyInfoMagic || out.version != kEnemyInfoVersion || !out.entity || !out.paths ||
        out.paths > kEnemyMeshPaths || (out.flags & ~kEnemyInfoFriendly))
        return false;
    for (size_t i = 0; i < kEnemyMeshPaths; ++i) {
        const char* p = out.path[i];
        size_t n = strnlen(p, kEnemyPathLength);
        if (n == kEnemyPathLength) return false;
        if (i < out.paths && (n < 3 || p[0] != '/' || !std::memchr(p, '.', n))) return false;
        if (i >= out.paths && n) return false;
        for (size_t c = 0; c < n; ++c)
            if (p[c] < 0x21 || p[c] > 0x7e) return false;
    }
    return true;
}

// A companion's runtime look: the base material asset of each slot of one of its meshes. The host builds
// MaterialInstanceDynamics at runtime, so the meshes alone render with empty slots on the other game.
constexpr uint32_t kEnemyMaterialsMagic = 0x544D4D44; // "DMMT"
constexpr uint16_t kEnemyMaterialsVersion = 1;
constexpr size_t kEnemyMaterialSlots = 8;
constexpr size_t kEnemyMaterialPathLength = 120;
constexpr size_t kEnemyMaterialScalars = 2; // runtime scalar parameters per slot (such as HC_ON)
constexpr size_t kEnemyScalarNameLength = 16;

struct EnemyMaterialScalar {
    std::string name;
    float value = 0;
};

#pragma pack(push, 1)
struct EnemyMaterialsPacket {
    uint32_t magic;
    uint16_t version;
    uint8_t slots, reserved;
    uint32_t session, entity;
    char mesh[kEnemyPathLength];                                   // the SkeletalMesh the slots belong to
    char material[kEnemyMaterialSlots][kEnemyMaterialPathLength]; // empty: leave the slot as it is
    char scalarName[kEnemyMaterialSlots][kEnemyMaterialScalars][kEnemyScalarNameLength]; // empty: unused
    float scalarValue[kEnemyMaterialSlots][kEnemyMaterialScalars];
};
static_assert(sizeof(EnemyMaterialsPacket) <= 1472, "DMMT must fit one UDP datagram");
#pragma pack(pop)

inline bool ValidObjectPath(const char* p, size_t capacity, bool allowEmpty) {
    size_t n = strnlen(p, capacity);
    if (n == capacity) return false;
    if (!n) return allowEmpty;
    if (n < 3 || p[0] != '/' || !std::memchr(p, '.', n)) return false;
    for (size_t c = 0; c < n; ++c)
        if (p[c] < 0x21 || p[c] > 0x7e) return false;
    return true;
}

inline bool ValidScalarName(const char* name, size_t n) {
    if (!n || n >= kEnemyScalarNameLength || (n == 4 && std::memcmp(name, "None", 4) == 0)) return false;
    for (size_t c = 0; c < n; ++c)
        if (!(std::isalnum(static_cast<unsigned char>(name[c])) || name[c] == '_')) return false;
    return true;
}
inline bool DecodeEnemyMaterials(const char* data, int len, EnemyMaterialsPacket& out) {
    if (len != static_cast<int>(sizeof(EnemyMaterialsPacket))) return false;
    std::memcpy(&out, data, sizeof out);
    if (out.magic != kEnemyMaterialsMagic || out.version != kEnemyMaterialsVersion || !out.entity || !out.slots ||
        out.slots > kEnemyMaterialSlots || out.reserved || !ValidObjectPath(out.mesh, kEnemyPathLength, false))
        return false;
    for (size_t i = 0; i < kEnemyMaterialSlots; ++i) {
        if (!ValidObjectPath(out.material[i], kEnemyMaterialPathLength, true)) return false;
        if (i >= out.slots && out.material[i][0]) return false;
        for (size_t k = 0; k < kEnemyMaterialScalars; ++k) {
            const char* name = out.scalarName[i][k];
            size_t n = strnlen(name, kEnemyScalarNameLength);
            if (!std::isfinite(out.scalarValue[i][k])) return false;
            if (n && (i >= out.slots || !out.material[i][0] || !ValidScalarName(name, n))) return false;
        }
    }
    return true;
}

inline bool EncodeEnemyMaterials(uint32_t session, uint32_t entity, const std::string& mesh,
                                 const std::vector<std::string>& materials, EnemyMaterialsPacket& out,
                                 const std::vector<std::vector<EnemyMaterialScalar>>& scalars = {}) {
    out = EnemyMaterialsPacket{};
    out.magic = kEnemyMaterialsMagic;
    out.version = kEnemyMaterialsVersion;
    out.session = session;
    out.entity = entity;
    if (mesh.size() >= kEnemyPathLength) return false;
    std::memcpy(out.mesh, mesh.data(), mesh.size());
    for (const auto& m : materials) {
        if (out.slots == kEnemyMaterialSlots) break;
        bool kept = m.size() < kEnemyMaterialPathLength;
        if (kept) std::memcpy(out.material[out.slots], m.data(), m.size());
        size_t k = 0;
        if (kept && !m.empty() && out.slots < scalars.size())
            for (const auto& s : scalars[out.slots])
                if (k < kEnemyMaterialScalars && ValidScalarName(s.name.c_str(), s.name.size()) && std::isfinite(s.value)) {
                    std::memcpy(out.scalarName[out.slots][k], s.name.data(), s.name.size());
                    out.scalarValue[out.slots][k++] = s.value;
                }
        ++out.slots;
    }
    EnemyMaterialsPacket check;
    return DecodeEnemyMaterials(reinterpret_cast<const char*>(&out), sizeof out, check);
}

inline bool EncodeEnemyInfo(uint32_t session, uint32_t entity, const std::vector<std::string>& paths, EnemyInfoPacket& out,
                            uint8_t flags = 0) {
    out = EnemyInfoPacket{};
    out.magic = kEnemyInfoMagic;
    out.version = kEnemyInfoVersion;
    out.flags = flags;
    out.session = session;
    out.entity = entity;
    for (const auto& p : paths) {
        if (out.paths == kEnemyMeshPaths || p.size() >= kEnemyPathLength) continue;
        std::memcpy(out.path[out.paths++], p.data(), p.size());
    }
    EnemyInfoPacket check;
    return DecodeEnemyInfo(reinterpret_cast<const char*>(&out), sizeof out, check);
}

// Pawn stand-ins (experimental, [Research] PawnEnemies=1 on both sides): instead of bone poses, the owner
// sends what a native client would get for an AI pawn. Each update carries the movement and the AI's move
// request, which drive the receiver's own copy of the pawn's anim graph, and the anim state. One-off
// animations (attacks, grapples), the pawn's class and its death are events.
constexpr uint32_t kEnemyMotionMagic = 0x4F4D4D44; // "DMMO"
constexpr uint16_t kEnemyMotionVersion = 1;
constexpr uint8_t kEnemyMaxAnimState = 63;

enum EnemyMotionFlags : uint8_t {
    kMotionAlive = 1 << 0,
    kMotionGrounded = 1 << 1,  // bHasTouchedGroundAfterSpawningOrTeleporting
    kMotionRequested = 1 << 2, // bHasRequestedVelocity
};

#pragma pack(push, 1)
struct EnemyMotionPacket {
    uint32_t magic;
    uint16_t version;
    uint8_t flags, movementMode;
    uint32_t session, entity, timeMs;
    float pos[3]; // absolute world coordinates of the actor
    float yaw;
    float velocity[3], acceleration[3], requestedVelocity[3];
    float desiredSpeed, maxWalkSpeed;
    float desiredRotation[3], deltaToDesiredRotation[3]; // pitch, yaw, roll
    float health;
    uint8_t state; // the Bend anim-state enum (0 ground movement, 5 melee attack, 6 grapple, 15 damage)
    uint8_t reserved[3];
};
#pragma pack(pop)
static_assert(sizeof(EnemyMotionPacket) == 112, "wire format");

inline bool DecodeEnemyMotion(const char* data, int len, EnemyMotionPacket& out) {
    if (len != static_cast<int>(sizeof(EnemyMotionPacket))) return false;
    std::memcpy(&out, data, sizeof out);
    if (out.magic != kEnemyMotionMagic || out.version != kEnemyMotionVersion || !out.session || !out.entity ||
        (out.flags & ~(kMotionAlive | kMotionGrounded | kMotionRequested)) || out.movementMode > 6 ||
        out.state > kEnemyMaxAnimState || out.reserved[0] || out.reserved[1] || out.reserved[2])
        return false;
    float values[(offsetof(EnemyMotionPacket, state) - offsetof(EnemyMotionPacket, pos)) / sizeof(float)];
    std::memcpy(values, data + offsetof(EnemyMotionPacket, pos), sizeof values);
    for (float f : values)
        if (!std::isfinite(f) || std::fabs(f) > 1e8f) return false;
    return out.health >= 0 && out.health <= 100000 && out.desiredSpeed >= 0 && out.maxWalkSpeed >= 0;
}

// The last few motion updates of one stand-in, sampled at a render time on the sender's clock: position and
// yaw are interpolated between the two updates around it, everything else comes from the later one. No
// extrapolation: past the newest update the newest is held.
class EnemyMotionBuffer {
public:
    bool Push(const EnemyMotionPacket& p, double arrival) {
        if (count_ && static_cast<int32_t>(p.timeMs - items_[(head_ + count_ - 1) % kSize].timeMs) <= 0) return false;
        if (count_ == kSize) {
            head_ = (head_ + 1) % kSize;
            --count_;
        }
        items_[(head_ + count_++) % kSize] = p;
        last_ = arrival;
        return true;
    }
    bool Sample(double senderMs, EnemyMotionPacket& out) const {
        if (!count_) return false;
        const EnemyMotionPacket* before = &At(0);
        if (senderMs <= static_cast<double>(before->timeMs) || count_ == 1) {
            out = senderMs <= static_cast<double>(before->timeMs) ? *before : At(count_ - 1);
            return true;
        }
        for (size_t i = 1; i < count_; ++i) {
            const auto& after = At(i);
            if (senderMs > static_cast<double>(after.timeMs)) {
                before = &after;
                continue;
            }
            double span = static_cast<double>(after.timeMs - before->timeMs);
            float t = static_cast<float>((senderMs - static_cast<double>(before->timeMs)) / span);
            out = after;
            for (int k = 0; k < 3; ++k) out.pos[k] = before->pos[k] + (after.pos[k] - before->pos[k]) * t;
            float turn = std::remainder(after.yaw - before->yaw, 360.f);
            out.yaw = before->yaw + turn * t;
            return true;
        }
        out = At(count_ - 1);
        return true;
    }
    double LastArrival() const { return last_; }
    void Reset() { head_ = count_ = 0; }
private:
    static constexpr size_t kSize = 8;
    const EnemyMotionPacket& At(size_t i) const { return items_[(head_ + i) % kSize]; }
    EnemyMotionPacket items_[kSize]{};
    size_t head_ = 0, count_ = 0;
    double last_ = -1e9;
};

constexpr uint32_t kEnemyEventMagic = 0x56454D44; // "DMEV"
constexpr uint16_t kEnemyEventVersion = 1;

enum EnemyEventKind : uint8_t {
    kEnemyEventClass = 1,   // path: the pawn's Blueprint class ("/Game/....SwarmerPawn_C")
    kEnemyEventOneShot = 2, // path: the AnimSequence the pawn started in an attack or grapple
    kEnemyEventDied = 3,    // no path
    kEnemyEventStandIn = 4, // receiver to owner, about once a second: it shows this entity as a stand-in, so the
                            // owner stops sending its bone poses while it lives; no path
};
constexpr uint8_t kEnemyEventAtReceiver = 1; // a one-shot aimed at the receiving player (counter prompts are theirs)

#pragma pack(push, 1)
struct EnemyEventPacket {
    uint32_t magic;
    uint16_t version;
    uint8_t kind, flags;
    uint32_t session, entity, sequence;
    char path[kEnemyPathLength];
};
#pragma pack(pop)

inline bool DecodeEnemyEvent(const char* data, int len, EnemyEventPacket& out) {
    if (len != static_cast<int>(sizeof(EnemyEventPacket))) return false;
    std::memcpy(&out, data, sizeof out);
    if (out.magic != kEnemyEventMagic || out.version != kEnemyEventVersion || !out.session || !out.entity ||
        !out.sequence || out.kind < kEnemyEventClass || out.kind > kEnemyEventStandIn)
        return false;
    if (out.flags & ~kEnemyEventAtReceiver) return false;
    if (out.flags && out.kind != kEnemyEventOneShot) return false;
    bool named = out.kind == kEnemyEventClass || out.kind == kEnemyEventOneShot;
    if (!ValidObjectPath(out.path, kEnemyPathLength, !named)) return false;
    if (!named && out.path[0]) return false;
    // Unused path bytes stay zero, so a packet has one encoding.
    for (size_t i = strnlen(out.path, kEnemyPathLength); i < kEnemyPathLength; ++i)
        if (out.path[i]) return false;
    return true;
}

inline bool EncodeEnemyEvent(uint32_t session, uint32_t entity, uint32_t sequence, EnemyEventKind kind,
                             const std::string& path, bool atReceiver, EnemyEventPacket& out) {
    out = EnemyEventPacket{};
    out.magic = kEnemyEventMagic;
    out.version = kEnemyEventVersion;
    out.kind = kind;
    out.flags = atReceiver && kind == kEnemyEventOneShot ? kEnemyEventAtReceiver : 0;
    out.session = session;
    out.entity = entity;
    out.sequence = sequence;
    if (path.size() >= kEnemyPathLength) return false;
    std::memcpy(out.path, path.data(), path.size());
    EnemyEventPacket check;
    return DecodeEnemyEvent(reinterpret_cast<const char*>(&out), sizeof out, check);
}

// Face rig, tongue, eyes, fingers and camera/reference helpers do not change an enemy's silhouette.
inline bool EnemyBoneSkipped(std::string_view name) {
    std::string lower(name);
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    for (const char* term : {"face", "tongue", "palate", "eye", "thumb", "index", "ring", "pinky", "middle", "cam0", "ref0", "lip", "brow", "cheek"})
        if (lower.find(term) != std::string::npos) return true;
    return false;
}

// Bones to stream: skipped bones drop their whole subtree so every sent bone's parent is sent too.
inline std::vector<int> EnemyBoneSubset(const std::vector<std::string>& names, const std::vector<int>& parents) {
    std::vector<int> subset;
    std::vector<bool> kept(names.size(), false);
    for (size_t i = 0; i < names.size() && subset.size() < kEnemyMaxBones; ++i) {
        int parent = i < parents.size() ? parents[i] : -1;
        bool parentKept = parent < 0 || (static_cast<size_t>(parent) < i && kept[parent]);
        if (!parentKept || EnemyBoneSkipped(names[i])) continue;
        kept[i] = true;
        subset.push_back(static_cast<int>(i));
    }
    return subset;
}

// A hit on a replicated enemy, sent to the enemy's owner, which applies the damage to its real pawn.
constexpr uint32_t kEnemyHitMagic = 0x48454D44; // "DMEH"
constexpr uint16_t kEnemyHitVersion = 1;
constexpr float kEnemyHitMaxDamage = 1000.f;
constexpr float kEnemyMeleeRange = 400.f;  // generous: guest and host positions differ by a few frames
constexpr float kEnemyShotRange = 15000.f;

#pragma pack(push, 1)
struct EnemyHitPacket {
    uint32_t magic;
    uint16_t version;
    uint8_t kind, reserved; // kind 1 melee, 2 gunshot
    uint32_t session, entity, sequence;
    float damage;
};
#pragma pack(pop)

inline bool DecodeEnemyHit(const char* data, int len, EnemyHitPacket& out) {
    if (len != static_cast<int>(sizeof(EnemyHitPacket))) return false;
    std::memcpy(&out, data, sizeof out);
    return out.magic == kEnemyHitMagic && out.version == kEnemyHitVersion && (out.kind == 1 || out.kind == 2) &&
        !out.reserved && out.session && out.entity && out.sequence && out.damage > 0 &&
        out.damage <= kEnemyHitMaxDamage;
}

inline EnemyHitPacket EncodeEnemyHit(uint32_t session, uint32_t entity, uint32_t sequence, bool melee, float damage) {
    EnemyHitPacket p{};
    p.magic = kEnemyHitMagic;
    p.version = kEnemyHitVersion;
    p.kind = melee ? 1 : 2;
    p.session = session;
    p.entity = entity;
    p.sequence = sequence;
    p.damage = damage;
    return p;
}

// Damage from an enemy attack on the partner's stand-in pawn, applied by that player's owner.
constexpr uint32_t kPartnerStrikeMagic = 0x58504D44; // "DMPX"
constexpr uint16_t kPartnerStrikeVersion = 1;

#pragma pack(push, 1)
struct PartnerStrikePacket {
    uint32_t magic;
    uint16_t version, reserved;
    uint32_t session, sequence;
    float damage;
};
#pragma pack(pop)

inline bool DecodePartnerStrike(const char* data, int len, PartnerStrikePacket& out) {
    if (len != static_cast<int>(sizeof(PartnerStrikePacket))) return false;
    std::memcpy(&out, data, sizeof out);
    return out.magic == kPartnerStrikeMagic && out.version == kPartnerStrikeVersion && !out.reserved && out.session &&
        out.sequence && out.damage > 0 && out.damage <= kEnemyHitMaxDamage;
}

inline PartnerStrikePacket EncodePartnerStrike(uint32_t session, uint32_t sequence, float damage) {
    PartnerStrikePacket p{};
    p.magic = kPartnerStrikeMagic;
    p.version = kPartnerStrikeVersion;
    p.session = session;
    p.sequence = sequence;
    p.damage = damage;
    return p;
}

// Hits travel twice over UDP; the receiver applies each sequence number once.
class EnemyHitDedupe {
public:
    bool Accept(uint32_t session, uint32_t sequence) {
        if (session != session_) {
            session_ = session;
            count_ = 0;
        }
        for (size_t i = 0; i < count_ && i < kWindow; ++i)
            if (seen_[i] == sequence) return false;
        seen_[count_++ % kWindow] = sequence;
        return true;
    }
private:
    static constexpr size_t kWindow = 64;
    uint32_t session_ = 0, seen_[kWindow]{};
    size_t count_ = 0;
};

// Distance along the aim ray to a standing body whose feet are at `feet`, or -1 when the ray misses its
// 50 cm radius, 190 cm tall cylinder. Direction must be normalized.
inline float AimDistanceToBody(const float start[3], const float direction[3], const float feet[3]) {
    float dx = feet[0] - start[0], dy = feet[1] - start[1];
    float horizontal = direction[0] * direction[0] + direction[1] * direction[1];
    if (horizontal < 1e-6f) return -1;
    // Closest approach of the ray to the body's vertical axis, measured horizontally.
    float t = (dx * direction[0] + dy * direction[1]) / horizontal;
    if (t < 0 || t > kEnemyShotRange) return -1;
    float ax = start[0] + direction[0] * t - feet[0], ay = start[1] + direction[1] * t - feet[1];
    float az = start[2] + direction[2] * t - feet[2];
    if (ax * ax + ay * ay > 50.f * 50.f || az < -10.f || az > 190.f) return -1;
    return t;
}

} // namespace coop
