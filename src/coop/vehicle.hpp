#pragma once

#include "enemy.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <string>

namespace coop {

// Vehicles other than the players' own bikes (a companion's bike, mission trucks) travel as their blueprint
// class and transform. The joining player spawns the same class for display, so every part comes with it.
constexpr uint32_t kVehicleMagic = 0x48564D44; // "DMVH"
constexpr uint16_t kVehicleVersion = 1;
constexpr uint8_t kVehicleDriven = 1; // someone sits on the driver seat
constexpr size_t kVehicleMaxStreamed = 6;
constexpr float kVehicleRadius = 15000.f; // 150 m around the host
constexpr double kVehicleTimeout = 3.0;

struct VehiclePacket {
    uint32_t magic;
    uint16_t version;
    uint8_t flags, reserved;
    uint32_t session, entity, timeMs;
    float position[3]; // world coordinates (origin-rebased like pose roots)
    float rotation[3]; // pitch, yaw, roll in degrees
    char cls[kEnemyPathLength]; // "/Game/.../BP_x.BP_x_C"
};

inline bool DecodeVehicle(const char* data, int len, VehiclePacket& out) {
    if (len != static_cast<int>(sizeof(VehiclePacket))) return false;
    std::memcpy(&out, data, sizeof out);
    if (out.magic != kVehicleMagic || out.version != kVehicleVersion || !out.entity || out.reserved ||
        (out.flags & ~kVehicleDriven) || !ValidObjectPath(out.cls, kEnemyPathLength, false))
        return false;
    for (float v : out.position)
        if (!std::isfinite(v) || std::fabs(v) > 1e8f) return false;
    for (float v : out.rotation)
        if (!std::isfinite(v) || std::fabs(v) > 1e4f) return false;
    return true;
}

inline bool EncodeVehicle(uint32_t session, uint32_t entity, uint32_t timeMs, const float position[3],
                          const float rotation[3], const std::string& cls, uint8_t flags, VehiclePacket& out) {
    out = VehiclePacket{};
    out.magic = kVehicleMagic;
    out.version = kVehicleVersion;
    out.flags = flags;
    out.session = session;
    out.entity = entity;
    out.timeMs = timeMs;
    std::memcpy(out.position, position, sizeof out.position);
    std::memcpy(out.rotation, rotation, sizeof out.rotation);
    if (cls.size() >= kEnemyPathLength) return false;
    std::memcpy(out.cls, cls.data(), cls.size());
    VehiclePacket check;
    return DecodeVehicle(reinterpret_cast<const char*>(&out), sizeof out, check);
}

inline float LerpAngle(float a, float b, float t) {
    float d = std::fmod(b - a, 360.f);
    if (d > 180.f) d -= 360.f;
    if (d < -180.f) d += 360.f;
    return a + d * t;
}

// Received transforms, sampled at a sender timestamp; holds the newest one past the end.
class VehicleTrack {
public:
    struct Sample {
        uint32_t timeMs = 0;
        float position[3]{}, rotation[3]{};
    };
    void Push(const VehiclePacket& p) {
        if (!samples_.empty() && static_cast<int32_t>(p.timeMs - samples_.back().timeMs) <= 0) return;
        Sample s;
        s.timeMs = p.timeMs;
        std::memcpy(s.position, p.position, sizeof s.position);
        std::memcpy(s.rotation, p.rotation, sizeof s.rotation);
        samples_.push_back(s);
        while (samples_.size() > 32) samples_.pop_front();
    }
    bool At(double senderMs, Sample& out) const {
        if (samples_.empty()) return false;
        out = samples_.front();
        if (senderMs <= samples_.front().timeMs) return true;
        for (size_t i = 1; i < samples_.size(); ++i) {
            const auto& a = samples_[i - 1];
            const auto& b = samples_[i];
            if (senderMs > b.timeMs) continue;
            float t = static_cast<float>((senderMs - a.timeMs) / static_cast<double>(b.timeMs - a.timeMs));
            out.timeMs = static_cast<uint32_t>(senderMs);
            for (int k = 0; k < 3; ++k) {
                out.position[k] = a.position[k] + (b.position[k] - a.position[k]) * t;
                out.rotation[k] = LerpAngle(a.rotation[k], b.rotation[k], t);
            }
            return true;
        }
        out = samples_.back();
        return true;
    }

private:
    std::deque<Sample> samples_;
};

} // namespace coop
