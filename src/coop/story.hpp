#pragma once

#include <cstdint>
#include <cstring>
#include <string_view>

namespace coop {

// Story progress, host to guest. The host's save is authoritative: the guest compares saved job and
// objective progress with its own and reloads the host's save when they differ.
constexpr uint32_t kStoryMagic = 0x54534D44; // "DMST"
constexpr uint16_t kStoryVersion = 2;
constexpr double kStoryInterval = 1.0;        // host resend period
constexpr double kStorySampleInterval = 3.0;  // how often each side recomputes its own signature
constexpr double kStoryResyncDelay = 6.0;     // a mismatch must persist this long before reloading

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
#pragma pack(pop)

struct StorySignature {
    uint32_t hash = 0;
    uint16_t completed = 0, unlocked = 0;
    uint8_t episode = 0;
    bool valid = false;
};

// FNV-1a over job and mission names, completion flags, states and objective indices, in a fixed order.
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

// Guest decision: reload the host's save when its saved progress differs from ours, at most once per
// distinct host progress (so a signature that can never match does not reload on every autosave).
inline bool StoryNeedsResync(const StoryPacket& host, const StorySignature& mine, uint32_t lastResyncedSignature) {
    return mine.valid && host.savedSignature && host.savedSignature != mine.hash &&
           host.savedSignature != lastResyncedSignature;
}

} // namespace coop
