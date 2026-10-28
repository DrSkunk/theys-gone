#pragma once

#include <cstdint>
#include <cstring>
#include <string_view>

namespace coop {

// Story progress, host to guest. The host's save is authoritative: the guest compares saved job and
// objective progress with its own and reloads the host's save when they differ.
constexpr uint32_t kStoryMagic = 0x54534D44; // "DMST"
constexpr uint16_t kStoryVersion = 3;
constexpr double kStoryInterval = 1.0;        // host resend period
constexpr double kStorySampleInterval = 30.0; // how often each side recomputes its own signature (saves recompute immediately)
constexpr double kStoryResyncDelay = 6.0;     // a mismatch must persist this long before reloading
constexpr double kObjectiveProgressInterval = 3.0; // host retries completed objectives
constexpr uint32_t kObjectiveProgressMagic = 0x424F4D44; // "DMOB"
constexpr uint16_t kObjectiveProgressVersion = 1;
constexpr size_t kObjectiveNameBytes = 48;

#pragma pack(push, 1)
struct StoryPacket {
    uint32_t magic;
    uint16_t version;
    uint8_t episode, flags;
    uint32_t session;
    uint32_t savedSignature; // progress at the host's last save; 0 until it has saved this session
    uint32_t liveSignature;  // progress right now
    uint32_t saveSerial;     // increments with every host save
    uint16_t completed, unlocked; // jobs and missions complete / known
    uint32_t sequence;       // increments on every host resend
};

struct ObjectiveProgressPacket {
    uint32_t magic;
    uint16_t version;
    uint8_t state, flags;
    uint32_t session;
    uint32_t sequence;
    char jobName[kObjectiveNameBytes];
    char objectiveId[kObjectiveNameBytes];
};
#pragma pack(pop)
static_assert(sizeof(ObjectiveProgressPacket) == 112, "objective progress wire size");

struct StorySignature {
    uint32_t hash = 0;
    uint16_t completed = 0, unlocked = 0;
    uint8_t episode = 0;
    bool valid = false;
};

// FNV-1a over job and mission state plus active-job objective IDs and states, in a fixed order.
class StoryHasher {
public:
    void Add(std::string_view name, bool completed, uint8_t state = 0, int32_t objective = -1) {
        for (char c : name) Mix(static_cast<uint8_t>(c));
        Mix(completed ? 1 : 0);
        Mix(state);
        uint32_t progress = static_cast<uint32_t>(objective);
        for (int shift = 0; shift < 32; shift += 8) Mix(static_cast<uint8_t>(progress >> shift));
        ++unlocked_;
        completed_ += completed ? 1 : 0;
    }
    void AddObjectiveState(std::string_view jobName, std::string_view identifier, uint16_t index, uint8_t state) {
        Mix(0xff);
        for (char c : jobName) Mix(static_cast<uint8_t>(c));
        Mix(0);
        for (char c : identifier) Mix(static_cast<uint8_t>(c));
        Mix(0);
        Mix(static_cast<uint8_t>(index));
        Mix(static_cast<uint8_t>(index >> 8));
        Mix(state);
    }
    StorySignature Finish(uint8_t episode) {
        Mix(episode);
        StorySignature out;
        out.hash = hash_ ? hash_ : 1;
        out.completed = completed_;
        out.unlocked = unlocked_;
        out.episode = episode;
        out.valid = true;
        return out;
    }

private:
    void Mix(uint8_t byte) {
        hash_ ^= byte;
        hash_ *= 16777619u;
    }
    uint32_t hash_ = 2166136261u;
    uint16_t completed_ = 0, unlocked_ = 0;
};

inline StoryPacket EncodeStory(uint32_t session, const StorySignature& live, uint32_t savedSignature, uint32_t saveSerial,
                               uint32_t sequence) {
    StoryPacket out{};
    out.magic = kStoryMagic;
    out.version = kStoryVersion;
    out.session = session;
    out.episode = live.episode;
    out.savedSignature = savedSignature;
    out.liveSignature = live.hash;
    out.saveSerial = saveSerial;
    out.completed = live.completed;
    out.unlocked = live.unlocked;
    out.sequence = sequence;
    return out;
}

inline bool DecodeStory(const char* data, int len, StoryPacket& out) {
    if (len != static_cast<int>(sizeof(StoryPacket))) return false;
    std::memcpy(&out, data, sizeof out);
    return out.magic == kStoryMagic && out.version == kStoryVersion && out.session && out.sequence && !out.flags &&
           out.completed <= out.unlocked;
}

inline bool StorySequenceIsNewer(uint32_t candidate, uint32_t previous) {
    uint32_t distance = candidate - previous;
    return distance != 0 && distance < 0x80000000u;
}

inline bool StoryPacketIsNewer(const StoryPacket& candidate, const StoryPacket& previous) {
    return candidate.session != previous.session || StorySequenceIsNewer(candidate.sequence, previous.sequence);
}

inline bool StoryNameIsValid(std::string_view name) {
    if (name.empty() || name.size() >= kObjectiveNameBytes) return false;
    for (char c : name) {
        bool valid = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                     c == '_' || c == '-' || c == '.';
        if (!valid) return false;
    }
    return true;
}

inline ObjectiveProgressPacket EncodeObjectiveProgress(uint32_t session, uint32_t sequence, std::string_view jobName,
                                                       std::string_view objectiveId, uint8_t state) {
    ObjectiveProgressPacket out{};
    if (!session || !sequence || state != 2 || !StoryNameIsValid(jobName) || !StoryNameIsValid(objectiveId)) return out;
    out.magic = kObjectiveProgressMagic;
    out.version = kObjectiveProgressVersion;
    out.state = state;
    out.session = session;
    out.sequence = sequence;
    std::memcpy(out.jobName, jobName.data(), jobName.size());
    std::memcpy(out.objectiveId, objectiveId.data(), objectiveId.size());
    return out;
}

inline bool DecodeObjectiveProgress(const char* data, int len, ObjectiveProgressPacket& out) {
    if (len != static_cast<int>(sizeof(ObjectiveProgressPacket))) return false;
    std::memcpy(&out, data, sizeof out);
    auto validField = [](const char* value) {
        size_t length = 0;
        while (length < kObjectiveNameBytes && value[length]) ++length;
        return length < kObjectiveNameBytes && StoryNameIsValid(std::string_view(value, length));
    };
    return out.magic == kObjectiveProgressMagic && out.version == kObjectiveProgressVersion && out.state == 2 &&
           !out.flags && out.session && out.sequence && validField(out.jobName) && validField(out.objectiveId);
}

inline bool ObjectiveProgressPacketIsNewer(const ObjectiveProgressPacket& candidate,
                                           const ObjectiveProgressPacket& previous) {
    return candidate.session != previous.session || StorySequenceIsNewer(candidate.sequence, previous.sequence);
}

// Guest decision: reload progress that is neither already verified nor being fetched.
inline bool StoryNeedsResync(const StoryPacket& host, const StorySignature& mine, uint32_t lastResyncedSignature,
                             uint32_t pendingResyncSignature) {
    return mine.valid && host.savedSignature && host.savedSignature != mine.hash &&
           host.savedSignature != lastResyncedSignature && host.savedSignature != pendingResyncSignature;
}

inline bool StoryResyncMatches(uint32_t expectedSignature, const StorySignature& loaded) {
    return expectedSignature && loaded.valid && expectedSignature == loaded.hash;
}

} // namespace coop
