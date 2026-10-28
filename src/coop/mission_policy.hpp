#pragma once

#include <cstdint>
#include <string_view>

namespace coop {

// What a story mission assumes about Deacon, keyed by the code in its BendMissionData asset name
// (OBR-MissionData, TLL2-MissionData). Categories come from the mission catalog and cutscene casts;
// see docs/STORY-COOP.md.
enum MissionFlag : uint16_t {
    kMissionEscort = 1 << 0,     // a companion rides or walks along on their own
    kMissionPillion = 1 << 1,    // a story character uses the passenger seat (or Deacon rides on theirs)
    kMissionGuard = 1 << 2,      // a companion must survive
    kMissionStealth = 1 << 3,    // detection fails the mission or checkpoint
    kMissionCapture = 1 << 4,    // Deacon or a companion is captured or stripped of gear
    kMissionBikeLoss = 1 << 5,   // Deacon's bike is broken, stolen, left behind or replaced
    kMissionFlashback = 1 << 6,  // past timeline
    kMissionSetPiece = 1 << 7,   // boss, scripted horde or other single-fighter scene
    kMissionTransition = 1 << 8, // changes the open regions
    kMissionTalk = 1 << 9,       // talking or cutscenes only
};

struct MissionPolicy {
    std::string_view code;
    uint16_t flags;
};

inline constexpr MissionPolicy kMissionPolicies[] = {
    // Act 2: The Drifter Road
    {"HAT", kMissionEscort | kMissionSetPiece},
    {"BMB", kMissionEscort},
    {"OBR", kMissionPillion | kMissionEscort | kMissionBikeLoss},
    {"OBT", kMissionEscort},
    {"CRW", kMissionCapture},
    {"WMB", kMissionBikeLoss | kMissionEscort},
    {"NBE", kMissionStealth},
    {"WAN", kMissionSetPiece},
    {"WNG", kMissionFlashback | kMissionPillion},
    {"CTR", kMissionEscort},
    {"HGL", kMissionFlashback},
    {"TUB", kMissionEscort | kMissionTransition},
    {"LOL1", 0},
    {"LOL2", kMissionPillion | kMissionSetPiece},
    {"COL", kMissionTalk},
    {"TFL2", kMissionTalk},
    {"RBC", kMissionPillion},
    {"FIN", kMissionStealth},
    {"OBF", kMissionStealth | kMissionTalk},
    {"TLL1", kMissionEscort | kMissionTransition},
    // Act 3: Wings Of Angels
    {"TLL2", kMissionStealth | kMissionCapture},
    {"TLL3", kMissionEscort},
    {"TLL4", kMissionTalk},
    {"SFN", kMissionStealth},
    {"OJ1", kMissionStealth},
    {"OJ2", kMissionStealth},
    {"OJ3", kMissionStealth},
    {"NFS1", kMissionEscort},
    {"NFS4", kMissionSetPiece},
    {"NFS6", kMissionEscort},
    {"FMC1", kMissionEscort | kMissionSetPiece},
    {"FMC2", kMissionEscort},
    {"FMC3", kMissionEscort},
    {"WAD1", kMissionEscort},
    {"WAD2", kMissionEscort},
    {"WADN", kMissionEscort},
    {"WADR", kMissionEscort},
    {"WAD3", kMissionEscort},
    {"BTF", kMissionTalk},
    {"MBU1", kMissionEscort},
    {"MBU2", kMissionEscort},
    {"MBU3", kMissionEscort},
    {"MBU4", 0},
    {"BIP", kMissionPillion},
    {"NRS1", kMissionStealth},
    {"NRS2", kMissionTalk},
    // Act 4: A Camper's Life
    {"MBF1", kMissionGuard | kMissionPillion},
    {"LBL1", kMissionEscort | kMissionGuard},
    {"LBL2", kMissionTalk},
    {"PBC2", kMissionBikeLoss | kMissionStealth | kMissionEscort},
    {"PBC3", kMissionBikeLoss | kMissionStealth | kMissionEscort},
    {"PBC4", kMissionBikeLoss | kMissionStealth | kMissionCapture},
    {"AON1", kMissionCapture | kMissionStealth | kMissionBikeLoss},
    {"AON2", kMissionBikeLoss},
    {"ALL1", kMissionEscort | kMissionSetPiece},
    {"ALL2", kMissionSetPiece},
    {"BBD1", kMissionStealth},
    {"BBD3", kMissionEscort},
    {"BBD4", kMissionGuard | kMissionSetPiece},
    {"BBD5", 0},
    {"BBD6", kMissionSetPiece},
    {"TSR", kMissionFlashback | kMissionPillion},
    {"AQW", kMissionFlashback},
    {"RNA1", kMissionEscort | kMissionTransition},
    // Act 5: The Forsaken
    {"MUF1", kMissionSetPiece},
    {"MUF2", kMissionEscort},
    {"ANM1", kMissionEscort},
    {"ANM2", kMissionEscort},
    {"ANM3", kMissionEscort | kMissionStealth},
    {"ANM4", kMissionEscort},
    {"JWI1", kMissionEscort},
    {"IPR", kMissionFlashback},
    {"JWI2", kMissionEscort},
    {"TCA1", kMissionTalk},
    {"SOM1", kMissionTalk},
    {"SOM2", kMissionTalk},
    {"SOM3", kMissionTalk},
    {"SOM4", kMissionTalk},
    {"SOM5", kMissionTalk},
    {"RFS1", kMissionPillion},
    {"RFS2", kMissionPillion},
    {"RFS3", kMissionEscort},
    {"RFS4", kMissionPillion},
    {"CLO2", kMissionPillion},
    {"CLO3", kMissionEscort},
    {"CLO4", kMissionEscort},
    {"CLO5", kMissionPillion},
    {"IYK", kMissionSetPiece | kMissionPillion},
    // Act 6: Lost Life
    {"LTB1", kMissionSetPiece},
    {"ICF1", kMissionSetPiece},
    {"AHB5", kMissionGuard | kMissionStealth | kMissionCapture | kMissionTransition},
    {"LRH1", kMissionTransition},
    // Act 7: Rest In Peace
    {"WBB1", kMissionSetPiece},
    {"AWI1", kMissionEscort | kMissionSetPiece},
    {"AWI2", kMissionSetPiece},
};

// "OBR-MissionData" -> "OBR"; names without a code prefix give an empty code.
inline std::string_view MissionCode(std::string_view assetName) {
    auto dash = assetName.find('-');
    return dash == std::string_view::npos ? std::string_view{} : assetName.substr(0, dash);
}

// Unknown or empty codes (DLC challenges, no mission) have no flags.
inline uint16_t MissionFlags(std::string_view code) {
    if (code.empty()) return 0;
    for (const auto& policy : kMissionPolicies)
        if (policy.code == code) return policy.flags;
    return 0;
}

// A driver offers the passenger seat to the partner only while no story character needs it.
inline bool PassengerSeatOffered(bool allowPassengerSetting, uint16_t activeMissionFlags) {
    return allowPassengerSetting && !(activeMissionFlags & kMissionPillion);
}

// The partner's stand-in is normally announced to nearby enemies so they fight it. In stealth missions
// that would alert enemies nobody has been seen by, so only enemies already fighting are told.
inline bool ProxyAwarenessAllowed(uint16_t activeMissionFlags, bool enemyHasTarget) {
    return !(activeMissionFlags & kMissionStealth) || enemyHasTarget;
}

} // namespace coop
