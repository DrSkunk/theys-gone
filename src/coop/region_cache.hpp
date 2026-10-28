#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace coop {

// Remembers recently queried readable address ranges so hot pointer checks (IsLive runs several times per
// ProcessEvent) skip most VirtualQuery calls. On Windows each VirtualQuery is a kernel transition.
// Only readable results are kept, and only briefly: memory can be decommitted at any time, and a remembered
// "unreadable" range would hide allocations committed into it later.
class ReadableRegionCache {
public:
    struct Region {
        uintptr_t base = 0, limit = 0;
    };
    static constexpr size_t kSlots = 256;
    static constexpr uint64_t kTtlMs = 100;
    static constexpr uintptr_t kMinAddress = 0x10000, kMaxAddress = 0x7ffffffffff0;

    // `query(address, region)` returns true with the committed readable region holding `address`.
    template <typename Query> bool Readable(uintptr_t address, size_t length, uint64_t nowMs, Query&& query) {
        if (address < kMinAddress || address > kMaxAddress || length > kMaxAddress - address) return false;
        auto& slot = slots_[(address >> 16) & (kSlots - 1)];
        if (address >= slot.base && address + length <= slot.limit && nowMs < slot.expiresMs) return true;
        Region region;
        ++queries_;
        if (!query(address, region) || address < region.base || address >= region.limit) return false;
        slot = {region.base, region.limit, nowMs + kTtlMs};
        return address + length <= region.limit;
    }

    uint64_t Queries() const { return queries_; }

private:
    struct Slot {
        uintptr_t base = 0, limit = 0;
        uint64_t expiresMs = 0;
    };
    std::array<Slot, kSlots> slots_{};
    uint64_t queries_ = 0;
};

} // namespace coop
