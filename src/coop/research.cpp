#include "research.hpp"

#include "log.hpp"
#include "skeleton.hpp"
#include "story.hpp"

#include <windows.h>
#ifdef GetProp
#undef GetProp
#endif

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <unordered_map>
#include <sstream>
#include <unordered_set>
#include <vector>

namespace coop::research {

using namespace ue;

namespace {

UObject* Next(UObject* field) {
    return *reinterpret_cast<UObject**>(reinterpret_cast<uint8_t*>(field) + off::FieldNext);
}

UObject* Children(UObject* structure) {
    return *reinterpret_cast<UObject**>(reinterpret_cast<uint8_t*>(structure) + off::StructChildren);
}

int ElementSize(UObject* property) {
    return *reinterpret_cast<int32_t*>(reinterpret_cast<uint8_t*>(property) + off::PropertyElementSize);
}

constexpr uint64_t kCpfNet = 0x20;        // CPF_Net: the property is marked for replication
constexpr uint64_t kCpfRepNotify = 0x100000000ull; // CPF_RepNotify

uint64_t PropertyFlags(UObject* property) {
    return *reinterpret_cast<uint64_t*>(reinterpret_cast<uint8_t*>(property) + off::PropertyFlags);
}

bool IsFunction(UObject* field) {
    auto kind = Name(field->cls);
    return kind == "Function" || kind == "DelegateFunction";
}

std::string Signature(UObject* fn) {
    std::string text = "(";
    int count = 0;
    for (auto p = Children(fn); p && count < 64; p = Next(p), ++count) {
        if (!IsLive(p)) break;
        if (count) text += ", ";
        text += Name(p->cls) + " " + Name(p) + "@" + std::to_string(PropertyOffset(p)) + "+" +
                std::to_string(ElementSize(p));
    }
    uint32_t flags = *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(fn) + off::FunctionFlags);
    char suffix[80];
    std::snprintf(suffix, sizeof suffix, ") parms=%u flags=0x%08x",
                  *reinterpret_cast<uint16_t*>(reinterpret_cast<uint8_t*>(fn) + off::FunctionParmsSize), flags);
    text += suffix;
    // Native thunk address as an RVA of the executable, so crash dumps can be matched to UFunctions.
    uintptr_t native = *reinterpret_cast<uintptr_t*>(reinterpret_cast<uint8_t*>(fn) + off::FunctionNative);
    if (native > g.imageBase && native < g.imageBase + 0x10000000) {
        std::snprintf(suffix, sizeof suffix, " native=0x%llx", static_cast<unsigned long long>(native - g.imageBase));
        text += suffix;
    }
    return text;
}

bool IsClassObject(UObject* obj) {
    if (!IsLive(obj) || !IsLive(obj->cls)) return false;
    auto kind = Name(obj->cls);
    return kind == "Class" || kind.ends_with("GeneratedClass") || kind == "ScriptStruct" ||
           kind == "UserDefinedStruct";
}

std::string Value(UObject* property, const uint8_t* data) {
    auto kind = Name(property->cls);
    int offset = PropertyOffset(property), size = ElementSize(property);
    if (offset < 0 || size <= 0) return "?";
    auto at = data + offset;
    char text[96];
    if (kind == "IntProperty" && size == 4) {
        int32_t v;
        std::memcpy(&v, at, 4);
        std::snprintf(text, sizeof text, "%d", v);
    } else if (kind == "FloatProperty" && size == 4) {
        float v;
        std::memcpy(&v, at, 4);
        std::snprintf(text, sizeof text, "%g", v);
    } else if (kind == "BoolProperty") {
        std::snprintf(text, sizeof text, "%s", *at ? "true" : "false");
    } else if (kind == "ByteProperty" && size == 1) {
        std::snprintf(text, sizeof text, "%u", *at);
    } else if (kind == "NameProperty" && size == 8) {
        FName v;
        std::memcpy(&v, at, 8);
        return "'" + NameString(v.index) + "'";
    } else if (kind == "ObjectProperty" || kind == "ClassProperty") {
        UObject* v;
        std::memcpy(&v, at, sizeof v);
        if (!v) return "null";
        return IsLive(v) ? FullName(v) : "<stale>";
    } else if (kind == "StrProperty") {
        const wchar_t* chars;
        int32_t count;
        std::memcpy(&chars, at, sizeof chars);
        std::memcpy(&count, at + sizeof chars, 4);
        if (!chars || count <= 0 || count > 256 || !ReadableMemory(chars, count * sizeof(wchar_t))) return "\"\"";
        std::string narrow;
        for (int i = 0; i + 1 < count; ++i) narrow += chars[i] < 128 ? static_cast<char>(chars[i]) : '?';
        return "\"" + narrow + "\"";
    } else if (kind == "StructProperty" && size == 12) {
        float v[3];
        std::memcpy(v, at, 12);
        std::snprintf(text, sizeof text, "(%g,%g,%g)", v[0], v[1], v[2]);
    } else {
        std::snprintf(text, sizeof text, "<%s %d>", kind.c_str(), size);
    }
    return text;
}

struct TraceEntry {
    uint64_t count = 0;
    std::string object;
    std::vector<std::string> samples;
};

struct TraceKey {
    UObject* cls;
    UObject* fn;
    // Set only for lifecycle/RepNotify functions, so those are counted per object instead of per class.
    UObject* obj = nullptr;
    bool operator==(const TraceKey&) const = default;
};

struct TraceHash {
    size_t operator()(const TraceKey& k) const {
        return std::hash<void*>()(k.cls) ^ (std::hash<void*>()(k.fn) << 1) ^ (std::hash<void*>()(k.obj) << 2);
    }
};

bool g_tracing = false;
double g_traceStart = 0;
std::unordered_map<TraceKey, TraceEntry, TraceHash> g_trace;
std::unordered_map<UObject*, bool> g_tracePerObject;

double Seconds() {
    LARGE_INTEGER frequency, counter;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&counter);
    return static_cast<double>(counter.QuadPart) / frequency.QuadPart;
}

} // namespace

bool DumpReflection(const std::wstring& path, std::string& summary) {
    FILE* file = _wfopen(path.c_str(), L"w");
    if (!file) {
        summary = "could not open the reflection dump file";
        return false;
    }
    int classes = 0, functions = 0, properties = 0;
    for (int32_t i = 0, n = ObjectCount(); i < n; ++i) {
        auto cls = ObjectAt(i);
        if (!IsClassObject(cls)) continue;
        ++classes;
        auto super = Super(cls);
        std::fprintf(file, "CLASS %s : %s\n", FullName(cls).c_str(), super ? Name(super).c_str() : "-");
        int budget = 0;
        for (auto field = Children(cls); field && budget < 8192; field = Next(field), ++budget) {
            if (!IsLive(field) || !IsLive(field->cls)) break;
            if (IsFunction(field)) {
                ++functions;
                std::fprintf(file, "  FN %s%s\n", Name(field).c_str(), Signature(field).c_str());
            } else {
                ++properties;
                uint64_t flags = PropertyFlags(field);
                std::fprintf(file, "  %s %s @%d+%d pflags=0x%llx%s\n", Name(field->cls).c_str(), Name(field).c_str(),
                             PropertyOffset(field), ElementSize(field), static_cast<unsigned long long>(flags),
                             (flags & kCpfNet) ? " net" : "");
            }
        }
    }
    std::fclose(file);
    char text[128];
    std::snprintf(text, sizeof text, "%d classes, %d functions, %d properties", classes, functions, properties);
    summary = text;
    return true;
}

bool DumpObjects(const std::wstring& path, std::string& summary) {
    FILE* file = _wfopen(path.c_str(), L"w");
    if (!file) {
        summary = "could not open the object census file";
        return false;
    }
    std::map<std::string, int> counts;
    static const char* kInteresting[] = {"mission", "objective", "manager", "director", "spawn", "encounter",
                                         "horde", "interact", "story", "quest", "task", "ambush", "camp",
                                         "condition", "trigger", "perception", "faction", "stimul"};
    int live = 0;
    std::vector<std::string> named;
    for (int32_t i = 0, n = ObjectCount(); i < n; ++i) {
        auto obj = ObjectAt(i);
        if (!IsLive(obj) || !IsLive(obj->cls) || IsDefaultOrArchetype(obj)) continue;
        ++live;
        auto cls = Name(obj->cls);
        ++counts[cls];
        std::string lower = cls;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
        for (auto token : kInteresting) {
            if (lower.find(token) != std::string::npos) {
                if (named.size() < 50000) named.push_back(FullName(obj));
                break;
            }
        }
    }
    std::fprintf(file, "# %d live objects, %d classes\n", live, static_cast<int>(counts.size()));
    std::vector<std::pair<int, std::string>> sorted;
    for (auto& [name, count] : counts) sorted.emplace_back(count, name);
    std::sort(sorted.rbegin(), sorted.rend());
    for (auto& [count, name] : sorted) std::fprintf(file, "COUNT %6d %s\n", count, name.c_str());
    for (auto& name : named) std::fprintf(file, "OBJ %s\n", name.c_str());
    std::fclose(file);
    summary = std::to_string(live) + " live objects, " + std::to_string(named.size()) + " listed by name";
    return true;
}

namespace {

std::string ByteProp(UObject* obj, const char* name) {
    uint8_t v = 0;
    return ue::GetProp(obj, name, v) ? std::to_string(v) : "?";
}

std::string BoolProp(UObject* obj, const char* name) {
    bool v = false;
    return ue::GetBoolProp(obj, name, v) ? (v ? "true" : "false") : "?";
}

std::string FloatProp(UObject* obj, const char* name) {
    float v = 0;
    char text[32];
    if (!ue::GetProp(obj, name, v)) return "?";
    std::snprintf(text, sizeof text, "%g", v);
    return text;
}

std::string ObjectProp(UObject* obj, const char* name) {
    UObject* v = nullptr;
    if (!IsLive(obj)) return "?";
    // Actor.Owner and similar are TWeakObjectPtr (ObjectIndex, SerialNumber), not raw pointers.
    if (auto prop = FindChild(obj->cls, name); prop && Name(prop->cls) == "WeakObjectProperty") {
        uint64_t weak = 0;
        if (!ue::GetProp(obj, name, weak)) return "?";
        if (!weak) return "null";
        v = ue::ResolveWeak(weak);
        return v ? FullName(v) : "<stale>";
    }
    if (!ue::GetProp(obj, name, v)) return "?";
    if (!v) return "null";
    return IsLive(v) ? FullName(v) : "<stale>";
}

void ActorReport(FILE* file, const char* label, UObject* actor) {
    if (!IsLive(actor)) {
        std::fprintf(file, "%s: none\n", label);
        return;
    }
    std::fprintf(file, "%s: %s vtable=0x%llx class=%s Role=%s RemoteRole=%s bReplicates=%s bReplicateMovement=%s "
                 "bNetStartup=%s bNetTemporary=%s NetUpdateFrequency=%s NetDriverName=%s Owner=%s\n",
                 label, FullName(actor).c_str(), static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(actor->vtable) - g.imageBase),
                 Name(actor->cls).c_str(), ByteProp(actor, "Role").c_str(),
                 ByteProp(actor, "RemoteRole").c_str(), BoolProp(actor, "bReplicates").c_str(),
                 BoolProp(actor, "bReplicateMovement").c_str(), BoolProp(actor, "bNetStartup").c_str(),
                 BoolProp(actor, "bNetTemporary").c_str(), FloatProp(actor, "NetUpdateFrequency").c_str(),
                 [&] { FName n{}; return ue::GetProp(actor, "NetDriverName", n) ? NameString(n.index) : std::string("?"); }().c_str(),
                 ObjectProp(actor, "Owner").c_str());
}

UObject* WorldSettingsForWorld(UObject* world) {
    UObject* level = nullptr;
    auto settingsClass = FindClass("WorldSettings");
    if (!IsLive(world) || !settingsClass || !ue::GetProp(world, "PersistentLevel", level) || !IsLive(level))
        return nullptr;
    for (int32_t i = 0, n = ObjectCount(); i < n; ++i) {
        auto obj = ObjectAt(i);
        if (IsLive(obj) && IsLive(obj->cls) && obj->outer == level && IsA(obj, settingsClass))
            return obj;
    }
    return nullptr;
}

} // namespace

bool ProbeNetwork(const std::wstring& path, UObject* localPawn, std::string& summary) {
    FILE* file = _wfopen(path.c_str(), L"w");
    if (!file) {
        summary = "could not open the network probe file";
        return false;
    }
    auto actorClass = FindClass("Actor");
    auto worldClass = FindClass("World");
    auto engineClass = FindClass("GameEngine");
    auto gameModeClass = FindClass("GameMode");
    auto gameStateClass = FindClass("GameState");
    auto playerStateClass = FindClass("PlayerState");
    auto controllerClass = FindClass("PlayerController");
    auto netDriverClass = FindClass("NetDriver");
    auto swarmerPawnClass = FindClass("SwarmerPawn_C");

    std::fprintf(file, "# built-in networking probe\n");
    std::fprintf(file, "classes: NetDriver=%p IpNetDriver=%p SteamNetDriver=%p DemoNetDriver=%p OnlineBeaconHost=%p "
                 "SteamNetConnection=%p IpConnection=%p\n",
                 static_cast<void*>(netDriverClass), static_cast<void*>(FindClass("IpNetDriver")),
                 static_cast<void*>(FindClass("SteamNetDriver")), static_cast<void*>(FindClass("DemoNetDriver")),
                 static_cast<void*>(FindClass("OnlineBeaconHost")), static_cast<void*>(FindClass("SteamNetConnection")),
                 static_cast<void*>(FindClass("IpConnection")));

    // Property-flag sanity: Actor.bReplicates and Actor.Role are replicated in stock UE4.
    if (actorClass) {
        for (auto name : {"bReplicates", "Role", "RemoteRole", "Owner", "Instigator", "bHidden", "NetTag"}) {
            auto prop = FindChild(actorClass, name);
            if (prop)
                std::fprintf(file, "Actor.%s pflags=0x%llx%s\n", name,
                             static_cast<unsigned long long>(PropertyFlags(prop)), (PropertyFlags(prop) & kCpfNet) ? " net" : "");
        }
    }

    int worlds = 0, engines = 0, replicatedActors = 0, totalActors = 0, netDrivers = 0, swarmerActors = 0;
    std::map<std::string, int> replicatedByClass, netPropsByPackage, netClassesByPackage, proxiesByClass;
    UObject *gameMode = nullptr, *gameState = nullptr, *playerState = nullptr, *controller = nullptr;
    // A hybrid guest has its own authoritative GameState and the host's replicated one; report both.
    std::vector<UObject*> gameStates;
    for (int32_t i = 0, n = ObjectCount(); i < n; ++i) {
        auto obj = ObjectAt(i);
        if (!IsLive(obj) || !IsLive(obj->cls)) continue;
        if (IsClassObject(obj)) {
            // Count properties marked CPF_Net per class, grouped by package.
            int netProps = 0;
            int budget = 0;
            for (auto field = Children(obj); field && budget < 8192; field = Next(field), ++budget) {
                if (!IsLive(field) || !IsLive(field->cls)) break;
                if (!IsFunction(field) && (PropertyFlags(field) & kCpfNet)) ++netProps;
            }
            if (netProps) {
                auto full = FullName(obj);
                auto slash = full.find('/', 1);
                auto dot = full.find('.', slash == std::string::npos ? 0 : slash);
                auto package = full.substr(0, dot == std::string::npos ? full.size() : dot);
                netPropsByPackage[package] += netProps;
                ++netClassesByPackage[package];
                if (!full.starts_with("/Script/Engine") && !full.starts_with("/Script/GameplayAbilities") &&
                    !full.starts_with("/Script/GameplayDebugger"))
                    std::fprintf(file, "NETCLASS %s cpf-net-properties=%d\n", full.c_str(), netProps);
            }
            continue;
        }
        if (IsDefaultOrArchetype(obj)) continue;
        if (worldClass && IsA(obj, worldClass)) {
            ++worlds;
            UObject* navigation = nullptr;
            ue::GetProp(obj, "NavigationSystem", navigation);
            std::fprintf(file, "WORLD %s NetDriver=%s DemoNetDriver=%s NetworkManager=%s NavigationSystem=%s GameState=%s\n",
                         FullName(obj).c_str(), ObjectProp(obj, "NetDriver").c_str(),
                         ObjectProp(obj, "DemoNetDriver").c_str(), ObjectProp(obj, "NetworkManager").c_str(),
                         ObjectProp(obj, "NavigationSystem").c_str(), ObjectProp(obj, "GameState").c_str());
            if (IsLive(navigation))
                std::fprintf(file, "NAVSYSTEM %s class=%s bAllowClientSideNavigation=%s MainNavData=%s\n",
                             FullName(navigation).c_str(), Name(navigation->cls).c_str(),
                             BoolProp(navigation, "bAllowClientSideNavigation").c_str(),
                             ObjectProp(navigation, "MainNavData").c_str());
        } else if (engineClass && IsA(obj, engineClass)) {
            ++engines;
            struct { void* data; int32_t num; int32_t max; } definitions{};
            std::fprintf(file, "ENGINE %s NetClientTicksPerSecond=%s", FullName(obj).c_str(),
                         FloatProp(obj, "NetClientTicksPerSecond").c_str());
            if (ue::GetProp(obj, "NetDriverDefinitions", definitions) && definitions.data && definitions.num >= 0 &&
                definitions.num <= 16 && ReadableMemory(definitions.data, definitions.num * 24)) {
                std::fprintf(file, " NetDriverDefinitions=%d", definitions.num);
                for (int d = 0; d < definitions.num; ++d) {
                    FName names[3];
                    std::memcpy(names, static_cast<uint8_t*>(definitions.data) + d * 24, 24);
                    std::fprintf(file, " [%s -> %s, fallback %s]", NameString(names[0].index).c_str(),
                                 NameString(names[1].index).c_str(), NameString(names[2].index).c_str());
                }
            }
            std::fprintf(file, "\n");
        } else if (netDriverClass && IsA(obj, netDriverClass)) {
            ++netDrivers;
            struct { void* data; int32_t num, max; } clients{};
            UObject* server = nullptr;
            ue::GetProp(obj, "ServerConnection", server);
            ue::GetProp(obj, "ClientConnections", clients);
            std::fprintf(file, "NETDRIVER %s ServerConnection=%s ClientConnections=%d\n", FullName(obj).c_str(),
                         server ? FullName(server).c_str() : "null", clients.num);
        } else if (actorClass && IsA(obj, actorClass)) {
            ++totalActors;
            bool replicates = false;
            if (ue::GetBoolProp(obj, "bReplicates", replicates) && replicates) {
                ++replicatedActors;
                ++replicatedByClass[Name(obj->cls)];
            }
            uint8_t role = 3;
            if (ue::GetProp(obj, "Role", role) && role != 3) ++proxiesByClass[Name(obj->cls) + " role=" + std::to_string(role)];
            if (swarmerPawnClass && IsA(obj, swarmerPawnClass)) {
                ++swarmerActors;
                ActorReport(file, "SWARMER", obj);
                Call location(obj, "K2_GetActorLocation");
                if (location.invoke()) {
                    auto at = location.get<FVector>("ReturnValue");
                    std::fprintf(file, "  Health=%s bReplicateHealthToAll=%s Controller=%s Location=(%.1f %.1f %.1f)\n",
                                 FloatProp(obj, "Health").c_str(), BoolProp(obj, "bReplicateHealthToAll").c_str(),
                                 ObjectProp(obj, "Controller").c_str(), at.x, at.y, at.z);
                } else {
                    std::fprintf(file, "  Health=%s bReplicateHealthToAll=%s Controller=%s Location=unavailable\n",
                                 FloatProp(obj, "Health").c_str(), BoolProp(obj, "bReplicateHealthToAll").c_str(),
                                 ObjectProp(obj, "Controller").c_str());
                }
                UObject* mesh = nullptr;
                ue::GetProp(obj, "Mesh", mesh);
                Call alive(obj, "IsAlive");
                bool measuredAlive = alive.invoke();
                std::fprintf(file, "  IsAlive=%s bIsDying=%s bTearOff=%s bHidden=%s Mesh=%s SkeletalMesh=%s AnimInstance=%s\n",
                             measuredAlive ? (alive.get<bool>("ReturnValue") ? "true" : "false") : "?",
                             BoolProp(obj, "bIsDying").c_str(), BoolProp(obj, "bTearOff").c_str(),
                             BoolProp(obj, "bHidden").c_str(), ObjectProp(obj, "Mesh").c_str(),
                             ObjectProp(mesh, "SkeletalMesh").c_str(), ObjectProp(mesh, "AnimScriptInstance").c_str());
            }
            if (playerStateClass && IsA(obj, playerStateClass)) {
                auto playerId = FindChild(playerStateClass, "PlayerId");
                auto playerName = FindChild(playerStateClass, "PlayerName");
                std::fprintf(file, "PLAYERSTATE_OBJECT %s Role=%s PlayerId=%s PlayerName=%s "
                             "bIsSpectator=%s bIsInactive=%s Owner=%s\n",
                             FullName(obj).c_str(), ByteProp(obj, "Role").c_str(),
                             playerId ? Value(playerId, reinterpret_cast<const uint8_t*>(obj)).c_str() : "?",
                             playerName ? Value(playerName, reinterpret_cast<const uint8_t*>(obj)).c_str() : "?",
                             BoolProp(obj, "bIsSpectator").c_str(), BoolProp(obj, "bIsInactive").c_str(),
                             ObjectProp(obj, "Owner").c_str());
            }
            if (gameStateClass && IsA(obj, gameStateClass)) gameStates.push_back(obj);
            if (gameModeClass && !gameMode && IsA(obj, gameModeClass)) gameMode = obj;
            else if (gameStateClass && !gameState && IsA(obj, gameStateClass)) gameState = obj;
            else if (playerStateClass && !playerState && IsA(obj, playerStateClass)) playerState = obj;
            else if (controllerClass && !controller && IsA(obj, controllerClass)) controller = obj;
        }
    }
    std::fprintf(file, "SWARMER_ACTORS count=%d\n", swarmerActors);
    ActorReport(file, "GAMEMODE", gameMode);
    if (IsLive(gameMode))
        std::fprintf(file, "  bUseSeamlessTravel=%s GameSession=%s GameState=%s NumTravellingPlayers=%s\n",
                     BoolProp(gameMode, "bUseSeamlessTravel").c_str(), ObjectProp(gameMode, "GameSession").c_str(),
                     ObjectProp(gameMode, "GameState").c_str(),
                     [&] { int32_t v = 0; return ue::GetProp(gameMode, "NumTravellingPlayers", v) ? std::to_string(v) : std::string("?"); }().c_str());
    if (IsLive(gameMode)) {
        UObject* session = nullptr;
        ue::GetProp(gameMode, "GameSession", session);
        auto intProp = [&](UObject* obj, const char* name) {
            int32_t v = 0;
            return IsLive(obj) && ue::GetProp(obj, name, v) ? std::to_string(v) : std::string("?");
        };
        std::fprintf(file, "  NumPlayers=%s NumSpectators=%s GameSession.MaxPlayers=%s MaxSpectators=%s MaxSplitscreensPerConnection=%s\n",
                     intProp(gameMode, "NumPlayers").c_str(), intProp(gameMode, "NumSpectators").c_str(),
                     intProp(session, "MaxPlayers").c_str(), intProp(session, "MaxSpectators").c_str(),
                     IsLive(session) ? ByteProp(session, "MaxSplitscreensPerConnection").c_str() : "?");
    }
    ActorReport(file, "GAMESTATE", gameState);
    for (auto state : gameStates) {
        if (!IsLive(state)) continue;
        std::fprintf(file, "GAMESTATE_OBJECT %s Role=%s\n", FullName(state).c_str(), ByteProp(state, "Role").c_str());
        struct { void* data; int32_t num, max; } playerArray{};
        if (ue::GetProp(state, "PlayerArray", playerArray) && playerArray.num >= 0 && playerArray.num <= 64 &&
            (!playerArray.num || (playerArray.data && ReadableMemory(playerArray.data, playerArray.num * sizeof(UObject*))))) {
            std::fprintf(file, "GAMESTATE_PLAYERARRAY %s count=%d\n", Name(state).c_str(), playerArray.num);
            for (int32_t i = 0; i < playerArray.num; ++i) {
                UObject* entry = nullptr;
                std::memcpy(&entry, static_cast<uint8_t*>(playerArray.data) + i * sizeof entry, sizeof entry);
                std::fprintf(file, "PLAYERARRAY %s[%d] %s\n", Name(state).c_str(), i,
                             IsLive(entry) ? FullName(entry).c_str() : entry ? "<stale>" : "null");
            }
        } else {
            std::fprintf(file, "GAMESTATE_PLAYERARRAY %s unavailable\n", Name(state).c_str());
        }
    }
    ActorReport(file, "PLAYERSTATE", playerState);
    ActorReport(file, "CONTROLLER", controller);
    if (IsLive(controller))
        std::fprintf(file, "  NetConnection=%s Player=%s NetPlayerIndex=%s\n", ObjectProp(controller, "NetConnection").c_str(),
                     ObjectProp(controller, "Player").c_str(), ByteProp(controller, "NetPlayerIndex").c_str());
    ActorReport(file, "PAWN", localPawn);
    if (IsLive(localPawn))
        std::fprintf(file, "  CharacterMovement=%s\n", ObjectProp(localPawn, "CharacterMovement").c_str());

    // Every player controller and net connection, to see joins and connection liveness on either side.
    auto connectionClass = FindClass("NetConnection");
    for (int32_t i = 0, n = ObjectCount(); i < n; ++i) {
        auto obj = ObjectAt(i);
        if (!IsLive(obj) || !IsLive(obj->cls) || IsDefaultOrArchetype(obj)) continue;
        if (controllerClass && IsA(obj, controllerClass)) {
            UObject* pawn = nullptr;
            ue::GetProp(obj, "Pawn", pawn);
            auto pawnProp = [&](const char* name) {
                return IsLive(pawn) ? ObjectProp(pawn, name) : std::string("null");
            };
            auto pawnByte = [&](const char* name) {
                return IsLive(pawn) ? ByteProp(pawn, name) : std::string("null");
            };
            std::fprintf(file, "PC %s Role=%s RemoteRole=%s bReplicates=%s NetConnection=%s Player=%s Pawn=%s "
                         "PlayerState=%s MyBendPawn=%s StateName=%s PawnRole=%s PawnRemoteRole=%s PawnOwner=%s "
                         "PawnController=%s PawnPlayerState=%s RideVehicleComponent=%s\n",
                         FullName(obj).c_str(), ByteProp(obj, "Role").c_str(), ByteProp(obj, "RemoteRole").c_str(),
                         BoolProp(obj, "bReplicates").c_str(), ObjectProp(obj, "NetConnection").c_str(),
                         ObjectProp(obj, "Player").c_str(), ObjectProp(obj, "Pawn").c_str(),
                         ObjectProp(obj, "PlayerState").c_str(), ObjectProp(obj, "MyBendPawn").c_str(),
                         [&] { FName v{}; return ue::GetProp(obj, "StateName", v) ? NameString(v.index) : std::string("?"); }().c_str(),
                         pawnByte("Role").c_str(), pawnByte("RemoteRole").c_str(), pawnProp("Owner").c_str(),
                         pawnProp("Controller").c_str(), pawnProp("PlayerState").c_str(),
                         pawnProp("RideVehicleComponent").c_str());
        } else if (connectionClass && IsA(obj, connectionClass)) {
            struct { void* data; int32_t num, max; } channels{};
            ue::GetProp(obj, "OpenChannels", channels);
            double last = 0;
            ue::GetProp(obj, "LastReceiveTime", last);
            std::fprintf(file, "CONN %s driver=%s OpenChannels=%d LastReceiveTime=%.2f ViewTarget=%s OwningActor=%s\n",
                         FullName(obj).c_str(), ObjectProp(obj, "driver").c_str(), channels.num, last,
                         ObjectProp(obj, "ViewTarget").c_str(), ObjectProp(obj, "OwningActor").c_str());
        } else if (netDriverClass && IsA(obj, netDriverClass)) {
            float t = 0;
            ue::GetProp(obj, "Time", t);
            std::fprintf(file, "DRIVERTIME %s Time=%.2f World=%s\n", FullName(obj).c_str(), t, ObjectProp(obj, "World").c_str());
        }
    }
    std::fprintf(file, "ACTORS total=%d bReplicates=%d worlds=%d engines=%d netdrivers=%d\n", totalActors,
                 replicatedActors, worlds, engines, netDrivers);
    std::vector<std::pair<int, std::string>> sorted;
    for (auto& [name, count] : replicatedByClass) sorted.emplace_back(count, name);
    std::sort(sorted.rbegin(), sorted.rend());
    for (auto& [count, name] : sorted) std::fprintf(file, "REPLICATES %6d %s\n", count, name.c_str());
    for (auto& [name, count] : proxiesByClass) std::fprintf(file, "PROXY %6d %s\n", count, name.c_str());
    for (auto& [package, count] : netPropsByPackage)
        std::fprintf(file, "NETPROPS %s classes=%d properties=%d\n", package.c_str(), netClassesByPackage[package], count);
    std::fclose(file);
    char text[160];
    std::snprintf(text, sizeof text, "%d worlds, %d net drivers, %d/%d actors replicate, %d packages declare replicated properties",
                  worlds, netDrivers, replicatedActors, totalActors, static_cast<int>(netPropsByPackage.size()));
    summary = text;
    return true;
}

namespace {

bool NetConsole(const std::wstring& command, std::string& summary) {
    auto controllerClass = FindClass("PlayerController");
    auto library = FindDefaultObject("KismetSystemLibrary");
    UObject* controller = nullptr;
    for (int32_t i = 0, n = ObjectCount(); controllerClass && i < n && !controller; ++i) {
        auto obj = ObjectAt(i);
        if (IsLive(obj) && IsLive(obj->cls) && !IsDefaultOrArchetype(obj) && IsA(obj, controllerClass)) controller = obj;
    }
    if (!controller || !library) {
        summary = "no player controller or KismetSystemLibrary to run the console command with";
        return false;
    }
    struct { const wchar_t* data; int32_t num, max; } text{command.c_str(), static_cast<int32_t>(command.size() + 1),
                                                           static_cast<int32_t>(command.size() + 1)};
    Call exec(library, "ExecuteConsoleCommand");
    exec.set("WorldContextObject", controller).set("Command", text).set("SpecificPlayer", static_cast<UObject*>(nullptr));
    std::string narrow;
    for (auto c : command) narrow += c < 128 ? static_cast<char>(c) : '?';
    bool ok = exec.invoke();
    summary = (ok ? "sent console command: " : "failed to send console command: ") + narrow;
    return ok;
}

} // namespace

// UWorld::Listen(FURL&) on build 19221447: NetDriver check, CreateNamedNetDriver / FindNamedNetDriver /
// SetWorld, virtual InitListen, MaxInternetClientRate clamp, "lanplay". CreateNamedNetDriver follows
// Engine.NetDriverDefinitions (GOG driver, IpNetDriver fallback); PatchNetDriverDefinitions points that at
// USteamNetDriver first, because the default socket subsystem is SteamSockets and IpNetDriver cannot bind.
constexpr uintptr_t kWorldListenRva = 0x2c52660;
constexpr uint8_t kWorldListenPrologue[] = {0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57,
                                            0x48, 0x83, 0xec, 0x50, 0x48, 0x8b, 0xf2, 0x48, 0x8b, 0xd9};

struct FStringRaw { const wchar_t* data = nullptr; int32_t num = 0, max = 0; };
struct FURLRaw {
    FStringRaw protocol, host;
    int32_t port = 7777;
    int32_t pad0 = 0;
    FStringRaw map, redirect;
    struct { void* data = nullptr; int32_t num = 0, max = 0; } options;
    FStringRaw portal;
    int32_t valid = 1;
    int32_t pad1 = 0;
};
static_assert(sizeof(FURLRaw) == 112, "FURL layout (UE 4.10)");

UObject* FindGameWorld() {
    auto worldClass = FindClass("World");
    for (int32_t i = 0, n = ObjectCount(); worldClass && i < n; ++i) {
        auto obj = ObjectAt(i);
        if (!IsLive(obj) || !IsLive(obj->cls) || IsDefaultOrArchetype(obj) || !IsA(obj, worldClass)) continue;
        UObject* manager = nullptr;
        if (ue::GetProp(obj, "NetworkManager", manager) && manager) return obj;
    }
    return nullptr;
}

// UNetDriver::SetWorld(UWorld*): unregisters the tick delegates from the old world (+0x88) and registers
// them on the new one. Non-virtual on this build.
constexpr uintptr_t kNetDriverSetWorldRva = 0x29d0400;
constexpr uint8_t kNetDriverSetWorldPrologue[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57,
                                                  0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xb1, 0x88, 0x00, 0x00, 0x00};
constexpr int kNetDriverInitListenSlot = 0x230 / 8;
constexpr int kWorldNetworkNotifyOffset = 0x28;

const uint8_t* CheckedCode(uintptr_t rva, const uint8_t* prologue, size_t size) {
    auto code = reinterpret_cast<const uint8_t*>(g.imageBase + rva);
    return ReadableMemory(code, size) && std::memcmp(code, prologue, size) == 0 ? code : nullptr;
}

std::string ErrorText(const FStringRaw& error) {
    std::string text;
    if (error.data && error.num > 0 && error.num < 512 && ReadableMemory(error.data, error.num * sizeof(wchar_t)))
        for (int i = 0; i + 1 < error.num; ++i) text += error.data[i] < 128 ? static_cast<char>(error.data[i]) : '?';
    return text;
}

// A full object scan costs 25-40 ms in game (lab, 2026-11-08); the story sample and every applied objective
// looked up the job manager this way, so the instance is kept while it stays live.
UObject* FindInstance(const char* className) {
    static std::unordered_map<std::string, UObject*> cache;
    auto cls = FindClass(className);
    if (!cls) return nullptr;
    auto& instance = cache[className];
    if (IsLive(instance) && IsLive(instance->cls) && IsA(instance, cls)) return instance;
    instance = nullptr;
    for (int32_t i = 0, n = ObjectCount(); i < n; ++i) {
        auto obj = ObjectAt(i);
        if (IsLive(obj) && IsLive(obj->cls) && !IsDefaultOrArchetype(obj) && IsA(obj, cls)) return instance = obj;
    }
    return nullptr;
}

// Rewrites Engine.NetDriverDefinitions[*].DriverClassName (and the fallback) to the Steam net driver, so
// the engine's own listen and pending-connection paths create a USteamNetDriver. Idempotent.
bool PatchNetDriverDefinitions(std::string& summary) {
    auto engine = FindInstance("GameEngine");
    auto strings = FindDefaultObject("KismetStringLibrary");
    if (!engine || !strings || !FindClass("SteamNetDriver")) {
        summary = "no engine, KismetStringLibrary or SteamNetDriver class";
        return false;
    }
    static const wchar_t path[] = L"/Script/OnlineSubsystemSteam.SteamNetDriver";
    FStringRaw text{path, static_cast<int32_t>(std::size(path)), static_cast<int32_t>(std::size(path))};
    Call conv(strings, "Conv_StringToName");
    conv.set("InString", text);
    if (!conv.invoke()) {
        summary = "Conv_StringToName failed";
        return false;
    }
    FName steam = conv.get<FName>("ReturnValue");
    struct { uint8_t* data; int32_t num, max; } definitions{};
    if (!ue::GetProp(engine, "NetDriverDefinitions", definitions) || !definitions.data || definitions.num <= 0 ||
        definitions.num > 16 || !ReadableMemory(definitions.data, definitions.num * 24)) {
        summary = "NetDriverDefinitions unreadable";
        return false;
    }
    std::string report;
    for (int d = 0; d < definitions.num; ++d) {
        FName names[3];
        std::memcpy(names, definitions.data + d * 24, 24);
        report += (d ? "; " : "") + NameString(names[0].index) + ": " + NameString(names[1].index) + " -> " + NameString(steam.index);
        names[1] = steam;
        names[2] = steam;
        std::memcpy(definitions.data + d * 24, names, 24);
    }
    summary = "NetDriverDefinitions patched (" + report + ")";
    return true;
}

bool g_acceptAnyP2P = false;

bool AcceptAnyP2PSession() { return g_acceptAnyP2P; }

bool g_engineOwnsP2P = false;

bool EngineOwnsP2P() { return g_engineOwnsP2P; }

bool g_dropHostStatus = true, g_dropGuestVisibility = true;
bool g_clientNavigation = false;

void SetClientNavigation(bool enabled) { g_clientNavigation = enabled; }

bool g_pawnReplication = false;
bool g_clientPossess = false;
bool g_bendHealthReplication = false;
bool g_characterReplicationInstalled = false;

void SetPawnReplication(bool enabled) { g_pawnReplication = enabled; }
void SetClientPossess(bool enabled) { g_clientPossess = enabled; }

void SetBendHealthReplication(bool enabled) {
    if (g_characterReplicationInstalled && enabled != g_bendHealthReplication) {
        Log("research: BendHealthReplication cannot change after pawn replication is installed; restart required");
        return;
    }
    g_bendHealthReplication = enabled;
}

// Bend locomotion is animation root motion. The server does not reproduce it for a remote player's pawn, so it
// rejects the guest's moves and corrects the guest back. UE4's AGameNetworkManager::ClientAuthorativePosition
// (read from the class default object in ServerMoveHandleClientError) makes the server accept the client's
// position instead. Applies to the default object and any live instance; idempotent.
void SetClientAuthoritativePosition(bool enabled) {
    static bool logged = false;
    if (!enabled) return;
    auto cls = FindClass("GameNetworkManager");
    if (!cls) return;
    // The client-authoritative branch only runs when ServerCheckClientError passes, which fails once the
    // error exceeds MAXPOSITIONERRORSQUARED (3 uu^2 by default), so raise that bound as well.
    constexpr float kMaxErrorSquared = 1.0e12f;
    auto maxError = ue::FindChild(cls, "MAXPOSITIONERRORSQUARED");
    int maxErrorOffset = maxError ? ue::PropertyOffset(maxError) : -1;
    int set = 0;
    float oldMaxError = -1.0f;
    for (int i = 0; i < ObjectCount(); ++i) {
        auto obj = ObjectAt(i);
        if (!IsLive(obj) || !IsLive(obj->cls) || !IsA(obj, cls)) continue;
        bool changed = false;
        bool current = false;
        if (ue::GetBoolProp(obj, "ClientAuthorativePosition", current) && !current &&
            ue::SetBoolProp(obj, "ClientAuthorativePosition", true))
            changed = true;
        if (maxErrorOffset > 0) {
            auto value = reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(obj) + maxErrorOffset);
            if (*value < kMaxErrorSquared) {
                oldMaxError = *value;
                *value = kMaxErrorSquared;
                changed = true;
            }
        }
        if (changed) ++set;
    }
    if (set || !logged)
        Log("research: GameNetworkManager.ClientAuthorativePosition set on %d objects (MAXPOSITIONERRORSQUARED "
            "@%d %.1f -> %.0f)", set, maxErrorOffset, oldMaxError, kMaxErrorSquared);
    logged = true;
}

using detail::LifetimeProperty;
using detail::LifetimeProperties;

void CharacterLifetimeProperties(UObject* obj, LifetimeProperties* properties) {
    using Register = void (*)(UObject*, LifetimeProperties*);
    reinterpret_cast<Register>(g.imageBase + 0x275b770)(obj, properties);
    if (properties->num < 0 || properties->num > properties->max || properties->max > 4096 ||
        (properties->num && !ReadableMemory(properties->data, properties->num * sizeof(LifetimeProperty)))) {
        Log("research: lifetime: invalid property array for %s", FullName(obj->cls).c_str());
        return;
    }
    auto health = ue::FindChild(obj->cls, "Health");
    if (!IsLive(health) || !(PropertyFlags(health) & kCpfNet) || Name(health->cls) != "FloatProperty" ||
        ElementSize(health) != sizeof(float) ||
        *reinterpret_cast<int32_t*>(reinterpret_cast<uint8_t*>(health) + off::PropertyArrayDim) != 1) {
        Log("research: lifetime: %s stock-count=%d has no supported CPF_Net float Health",
            FullName(obj->cls).c_str(), properties->num);
        return;
    }
    // Stock ACharacter registration reads UProperty::RepIndex at +0x40 and emits 12-byte entries.
    uint16_t index = 0;
    std::memcpy(&index, reinterpret_cast<uint8_t*>(health) + 0x40, sizeof index);
    int stockCount = properties->num;
    bool registered = false;
    for (int i = 0; i < properties->num; ++i) {
        const auto& entry = properties->data[i];
        registered |= entry.index == index;
    }
    bool added = false;
    if (g_bendHealthReplication) {
        auto result = detail::RegisterLifetimeProperty(*properties, index,
            reinterpret_cast<detail::Reallocate>(g.imageBase + 0x19740c0));
        if (result == detail::LifetimeRegistration::invalid || result == detail::LifetimeRegistration::allocationFailed) {
            Log("research: lifetime: Health registration FAILED for %s (result=%d)",
                FullName(obj->cls).c_str(), static_cast<int>(result));
            return;
        }
        added = result == detail::LifetimeRegistration::added;
    }
    static std::vector<UObject*> reported;
    if (std::find(reported.begin(), reported.end(), obj->cls) != reported.end()) return;
    reported.push_back(obj->cls);
    std::string entries;
    for (int i = 0; i < properties->num; ++i)
        entries += (i ? "," : "") + std::to_string(properties->data[i].index) + ":" +
                   std::to_string(properties->data[i].condition);
    Log("research: lifetime: %s stock-count=%d Health@%d RepIndex=%u stock-registered=%d added=%d total=%d "
        "entries(index:condition)=[%s]", FullName(obj->cls).c_str(), stockCount, PropertyOffset(health),
        index, registered, added, properties->num, entries.c_str());
}

// Bend's character classes override UObject::GetLifetimeReplicatedProps (vtable +0x198) with an empty
// function (0x1b2340 is a folded `ret`), so FRepLayout gets no lifetime properties and their actor channels
// carry nothing after the spawn: no Role/RemoteRole, Owner, ReplicatedMovement, Controller or PlayerState.
// Point those slots back at the stock ACharacter version (0x275b770, which chains APawn and AActor). Both
// sides must do this before their net driver builds the class rep layouts, or the layouts will not match.
int RestoreCharacterReplication() {
    static int patched = -1;
    if (patched >= 0) return patched;
    constexpr int kSlot = 0x198 / 8;
    auto empty = reinterpret_cast<void*>(g.imageBase + 0x1b2340);
    auto registration = reinterpret_cast<void*>(&CharacterLifetimeProperties);
    auto characterClass = FindClass("Character");
    if (!characterClass) return -1;
    std::vector<void**> seen;
    patched = 0;
    for (int i = 0; i < ObjectCount(); ++i) {
        auto obj = ObjectAt(i);
        if (!IsLive(obj) || !IsLive(obj->cls) || !IsA(obj, characterClass)) continue;
        auto vtable = obj->vtable;
        if (std::find(seen.begin(), seen.end(), vtable) != seen.end()) continue;
        seen.push_back(vtable);
        if (vtable[kSlot] != empty) continue;
        DWORD old;
        if (!VirtualProtect(vtable + kSlot, sizeof(void*), PAGE_READWRITE, &old)) continue;
        vtable[kSlot] = registration;
        VirtualProtect(vtable + kSlot, sizeof(void*), old, &old);
        ++patched;
        Log("research: pawn replication: restored GetLifetimeReplicatedProps for %s (vtable exe+0x%llx)",
            Name(obj->cls).c_str(), static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(vtable) - g.imageBase));
    }
    g_characterReplicationInstalled = true;
    Log("research: pawn replication: %d character vtables patched, %d seen", patched, static_cast<int>(seen.size()));
    return patched;
}

void SetStreamingRpcDrops(bool hostStatus, bool guestVisibility) {
    g_dropHostStatus = hostStatus;
    g_dropGuestVisibility = guestVisibility;
}

void WalkStep(UObject* obj, UObject* fn);

bool DropProcessEvent(UObject* obj, UObject* fn) {
    WalkStep(obj, fn);
    if (!g_engineOwnsP2P || !fn) return false;
    static int names[3] = {FindNameIndex("ClientUpdateLevelStreamingStatus"), FindNameIndex("ServerUpdateLevelVisibility"),
                           FindNameIndex("ClientFlushLevelStreaming")};
    static uint64_t dropped[3] = {};
    for (int i = 0; i < 3; ++i) {
        if (names[i] <= 0 || fn->name.index != names[i]) continue;
        if (i == 1 ? !g_dropGuestVisibility : !g_dropHostStatus) return false;
        if (dropped[i]++ % 500 == 0)
            Log("research: dropped %s on %s (%llu so far)", NameString(names[i]).c_str(),
                IsLive(obj) ? Name(obj).c_str() : "?", static_cast<unsigned long long>(dropped[i]));
        if (dropped[i] == 1) {
            // Who sends it: log the executable frames above ProcessEvent once.
            void* frames[24]{};
            USHORT count = RtlCaptureStackBackTrace(0, 24, frames, nullptr);
            std::string text;
            char item[32];
            for (USHORT f = 0; f < count; ++f) {
                auto at = reinterpret_cast<uintptr_t>(frames[f]);
                if (at > g.imageBase && at < g.imageBase + 0x10000000) {
                    std::snprintf(item, sizeof item, " exe+0x%llx", static_cast<unsigned long long>(at - g.imageBase));
                    text += item;
                }
            }
            Log("research: %s caller stack:%s", NameString(names[i]).c_str(), text.c_str());
        }
        return true;
    }
    return false;
}

bool NetListen(const std::string& controllerClass, const std::string& pawnClass, bool spectators, std::string& summary) {
    auto world = FindGameWorld();
    if (!world) {
        summary = "no game world with a network manager found";
        return false;
    }
    UObject* existing = nullptr;
    if (ue::GetProp(world, "NetDriver", existing) && existing) {
        summary = "the world already has a net driver: " + FullName(existing);
        return false;
    }
    std::string patched;
    if (!PatchNetDriverDefinitions(patched)) {
        summary = patched;
        return false;
    }
    Log("research: %s", patched.c_str());
    if (g_pawnReplication) RestoreCharacterReplication();
    g_engineOwnsP2P = true;
    g_acceptAnyP2P = true;
    // Experiment: joining players get engine-default controller and pawn classes, so the Bend blueprint
    // controller/pawn logic (written for exactly one local player) does not run for player two.
    if (auto gameMode = FindInstance("GameMode")) {
        auto swap = [&](const char* property, const char* className) {
            auto prop = FindChild(gameMode->cls, property);
            auto cls = FindClass(className);
            if (!prop || !cls) return;
            UObject* old = nullptr;
            std::memcpy(&old, reinterpret_cast<uint8_t*>(gameMode) + PropertyOffset(prop), sizeof old);
            std::memcpy(reinterpret_cast<uint8_t*>(gameMode) + PropertyOffset(prop), &cls, sizeof cls);
            Log("research: GameMode.%s %s -> %s", property, old ? Name(old).c_str() : "null", Name(cls).c_str());
        };
        if (!controllerClass.empty()) swap("PlayerControllerClass", controllerClass.c_str());
        if (!pawnClass.empty()) swap("DefaultPawnClass", pawnClass.c_str());
        if (spectators) {
            // Joining players become spectators: HandleStartingNewPlayer then skips RestartPlayer and the
            // game's single-player spawn chain (InitStartSpot and friends crashed there).
            bool value = false;
            uint8_t mask = 0;
            if (ue::GetBoolProp(gameMode, "bStartPlayersAsSpectators", value, &mask) && mask) {
                auto prop = FindChild(gameMode->cls, "bStartPlayersAsSpectators");
                auto at = reinterpret_cast<uint8_t*>(gameMode) + PropertyOffset(prop);
                *at = static_cast<uint8_t>(*at | mask);
                Log("research: GameMode.bStartPlayersAsSpectators %d -> 1", value ? 1 : 0);
            } else {
                Log("research: bStartPlayersAsSpectators not found");
            }
        }
    }
    if (auto listen = CheckedCode(kWorldListenRva, kWorldListenPrologue, sizeof kWorldListenPrologue)) {
        using ListenFn = bool (*)(UObject*, FURLRaw*);
        FURLRaw url;
        bool ok = reinterpret_cast<ListenFn>(const_cast<uint8_t*>(listen))(world, &url);
        UObject* driver = nullptr;
        ue::GetProp(world, "NetDriver", driver);
        summary = std::string("UWorld::Listen returned ") + (ok ? "true" : "false") + ", NetDriver=" +
                  (driver ? FullName(driver) : std::string("null"));
        if (ok && driver) {
            g_acceptAnyP2P = true;
            return true;
        }
        Log("research: %s; falling back to a hand-built driver", summary.c_str());
    }
    // Fallback: build the driver by hand. The default socket subsystem on this build is SteamSockets, so a
    // plain IpNetDriver cannot bind ("SteamSockets: binding to port 7777 failed"). USteamNetDriver speaks
    // Steam P2P, which is what the socket subsystem expects, and gives relay/NAT traversal for free.
    auto driverClass = FindClass("SteamNetDriver");
    if (!driverClass) driverClass = FindClass("IpNetDriver");
    auto engine = FindInstance("GameEngine");
    auto statics = FindDefaultObject("GameplayStatics");
    auto setWorld = CheckedCode(kNetDriverSetWorldRva, kNetDriverSetWorldPrologue, sizeof kNetDriverSetWorldPrologue);
    if (!driverClass || !engine || !statics || !setWorld) {
        summary = std::string("missing pieces:") + (driverClass ? "" : " net-driver-class") + (engine ? "" : " game-engine") +
                  (statics ? "" : " gameplay-statics") + (setWorld ? "" : " setworld-prologue");
        return false;
    }
    Call spawn(statics, "SpawnObject");
    spawn.set("ObjectClass", driverClass).set("Outer", engine);
    if (!spawn.invoke()) {
        summary = "GameplayStatics.SpawnObject failed";
        return false;
    }
    auto driver = spawn.get<UObject*>("ReturnValue");
    if (!IsLive(driver)) {
        summary = "SpawnObject returned no net driver";
        return false;
    }
    using SetWorldFn = void (*)(UObject*, UObject*);
    reinterpret_cast<SetWorldFn>(const_cast<uint8_t*>(setWorld))(driver, world);
    // UNetDriver::InitListen(FNetworkNotify*, FURL&, bool bReuseAddressAndPort, FString& Error): the error
    // reference is the fifth argument and travels on the stack.
    using InitListenFn = bool (*)(UObject*, void*, FURLRaw*, bool, FStringRaw*);
    auto initListen = reinterpret_cast<InitListenFn>(driver->vtable[kNetDriverInitListenSlot]);
    FURLRaw url;
    FStringRaw error;
    bool ok = initListen(driver, reinterpret_cast<uint8_t*>(world) + kWorldNetworkNotifyOffset, &url, false, &error);
    summary = "InitListen on " + FullName(driver) + " returned " + (ok ? "true" : "false");
    if (!ok) {
        summary += " error=\"" + ErrorText(error) + "\"";
        reinterpret_cast<SetWorldFn>(const_cast<uint8_t*>(setWorld))(driver, nullptr);
        return false;
    }
    // Hand the driver to the world as UWorld::Listen would. It is not registered in the engine's
    // FNamedNetDriver list, so engine lookups by name (FindNamedNetDriver) will not see it.
    if (auto prop = FindChild(world->cls, "NetDriver")) {
        std::memcpy(reinterpret_cast<uint8_t*>(world) + PropertyOffset(prop), &driver, sizeof driver);
        summary += "; World.NetDriver assigned";
    }
    g_acceptAnyP2P = true;
    return true;
}

// ---- Client join without LoadMap -------------------------------------------------------------------
// UEngine::LoadMap(FWorldContext&, FURL, UPendingNetGame*, FString& Error) reloads the map, which this game
// cannot survive (see REPLICATION-RESEARCH.md). When a pending connection has been welcomed, the hook
// instead runs UEngine::MovePendingLevel (vtable slot 0x4c0/8: World.NetDriver = Pending->NetDriver,
// NetDriverName = GameNetDriver, SetWorld) on the world that is already running, points the driver's
// FNetworkNotify at that world and returns success. TickWorldTravel then sends NMT_Join as usual.
constexpr uintptr_t kLoadMapRva = 0x2c013a0;
constexpr uint8_t kLoadMapPrologue[] = {0x48, 0x89, 0x5c, 0x24, 0x20, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55,
                                        0x41, 0x56, 0x41, 0x57, 0x48, 0x8d, 0xac, 0x24, 0xc0, 0xfd, 0xff, 0xff};
constexpr int kEngineMovePendingLevelSlot = 0x4c0 / 8;
constexpr int kContextPendingNetGame = 0x1c0;
constexpr int kContextWorld = 0x290;

using LoadMapFn = bool (*)(void*, uint8_t*, FURLRaw*, UObject*, FStringRaw*);
LoadMapFn g_loadMapOriginal = nullptr;

// UNavigationSystem::InitializeForWorld(UWorld*, FNavigationSystemRunMode) on build 19221447. LoadMap calls it
// with GameMode (1) after MovePendingLevel and InitializeActorsForPlay. When World.NavigationSystem is null it
// runs CreateNavigationSystem (RVA 0x26313f0), which returns null on NM_Client unless the configured class's
// default object has bAllowClientSideNavigation; a stock client therefore has no navigation system.
constexpr uintptr_t kInitializeNavigationForWorldRva = 0x263ed10;
constexpr uint8_t kInitializeNavigationForWorldPrologue[] = {0x48, 0x85, 0xc9, 0x74, 0x5b, 0x48, 0x89, 0x5c, 0x24, 0x08,
                                                             0x57, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xd9, 0x0f, 0xb6,
                                                             0xfa, 0x48, 0x8b, 0x89, 0x80, 0x04, 0x00, 0x00};
constexpr uint8_t kNavigationRunModeGame = 1;

void RecreateClientNavigation(UObject* world, UObject* navigationClass) {
    auto initialize = CheckedCode(kInitializeNavigationForWorldRva, kInitializeNavigationForWorldPrologue,
                                  sizeof kInitializeNavigationForWorldPrologue);
    auto defaults = IsLive(navigationClass) ? FindDefaultObject(Name(navigationClass).c_str()) : nullptr;
    if (!initialize || !IsLive(defaults)) {
        Log("research: client navigation skipped: %s", initialize ? "no navigation-system default object"
                                                                  : "InitializeForWorld prologue mismatch");
        return;
    }
    bool allowed = false;
    ue::GetBoolProp(defaults, "bAllowClientSideNavigation", allowed);
    if (!allowed && !ue::SetBoolProp(defaults, "bAllowClientSideNavigation", true)) {
        Log("research: client navigation skipped: cannot set %s.bAllowClientSideNavigation", Name(navigationClass).c_str());
        return;
    }
    Log("research: client navigation: %s.bAllowClientSideNavigation %s; running InitializeForWorld(GameMode)",
        Name(navigationClass).c_str(), allowed ? "was already true" : "set to true");
    using InitializeForWorldFn = void (*)(UObject*, uint8_t);
    reinterpret_cast<InitializeForWorldFn>(const_cast<uint8_t*>(initialize))(world, kNavigationRunModeGame);
    UObject* navigation = nullptr;
    ue::GetProp(world, "NavigationSystem", navigation);
    Log("research: client navigation: World.NavigationSystem=%s class=%s MainNavData=%s",
        IsLive(navigation) ? FullName(navigation).c_str() : "null",
        IsLive(navigation) ? Name(navigation->cls).c_str() : "-", ObjectProp(navigation, "MainNavData").c_str());
}

bool HookedLoadMap(void* engine, uint8_t* context, FURLRaw* url, UObject* pending, FStringRaw* error) {
    if (!pending) {
        Log("research: LoadMap without a pending game; passing through");
        return g_loadMapOriginal(engine, context, url, pending, error);
    }
    UObject* world = *reinterpret_cast<UObject**>(context + kContextWorld);
    UObject* contextPending = *reinterpret_cast<UObject**>(context + kContextPendingNetGame);
    UObject* driver = nullptr;
    ue::GetProp(pending, "NetDriver", driver);
    Log("research: LoadMap intercepted: pending=%s context-pending=%s driver=%s world=%s",
        IsLive(pending) ? FullName(pending).c_str() : "?", contextPending == pending ? "same" : "different",
        IsLive(driver) ? FullName(driver).c_str() : "null", IsLive(world) ? FullName(world).c_str() : "null");
    if (!IsLive(world) || !IsLive(driver) || contextPending != pending) {
        Log("research: LoadMap intercept aborted; passing through");
        return g_loadMapOriginal(engine, context, url, pending, error);
    }
    auto logJoinState = [&](const char* stage) {
        auto currentWorld = *reinterpret_cast<UObject**>(context + kContextWorld);
        auto currentPending = *reinterpret_cast<UObject**>(context + kContextPendingNetGame);
        auto level = IsLive(currentWorld) ? ObjectProp(currentWorld, "PersistentLevel") : std::string("?");
        auto settings = WorldSettingsForWorld(currentWorld);
        bool navigationEnabled = false;
        auto navigationEnabledText =
            IsLive(settings) && ue::GetBoolProp(settings, "bEnableNavigationSystem", navigationEnabled)
                ? (navigationEnabled ? "true" : "false")
                : "?";
        Log("research: LoadMap %s: world=%s persistent-level=%s net-driver=%s navigation=%s "
            "world-settings=%s bEnableNavigationSystem=%s pending=%s pending-driver=%s context-pending=%s",
            stage, IsLive(currentWorld) ? FullName(currentWorld).c_str() : "null", level.c_str(),
            ObjectProp(currentWorld, "NetDriver").c_str(), ObjectProp(currentWorld, "NavigationSystem").c_str(),
            IsLive(settings) ? FullName(settings).c_str() : "none", navigationEnabledText,
            IsLive(pending) ? FullName(pending).c_str() : "null", ObjectProp(pending, "NetDriver").c_str(),
            currentPending == pending ? "same" : "different");
    };
    auto vtable = *reinterpret_cast<void***>(engine);
    auto movePendingLevel = vtable[kEngineMovePendingLevelSlot];
    auto moveAddress = reinterpret_cast<uintptr_t>(movePendingLevel);
    auto moveRva = moveAddress > g.imageBase && moveAddress < g.imageBase + 0x10000000
                       ? moveAddress - g.imageBase
                       : 0;
    Log("research: MovePendingLevel target=%p RVA=0x%llx", movePendingLevel,
        static_cast<unsigned long long>(moveRva));
    logJoinState("before MovePendingLevel");
    UObject* navigationBefore = nullptr;
    ue::GetProp(world, "NavigationSystem", navigationBefore);
    auto navigationClass = IsLive(navigationBefore) ? navigationBefore->cls : nullptr;
    using MovePendingLevelFn = void (*)(void*, uint8_t*);
    reinterpret_cast<MovePendingLevelFn>(movePendingLevel)(engine, context);
    logJoinState("after MovePendingLevel");
    if (g_clientNavigation) {
        RecreateClientNavigation(world, navigationClass);
        logJoinState("after client navigation");
    }
    // InitConnect registered the pending game as the driver's FNetworkNotify (its interface sits at +0x28,
    // like UWorld's). Repoint it at the world so later control messages reach UWorld::NotifyControlMessage.
    auto pendingNotify = reinterpret_cast<uintptr_t>(pending) + kWorldNetworkNotifyOffset;
    auto worldNotify = reinterpret_cast<uintptr_t>(world) + kWorldNetworkNotifyOffset;
    int repointed = 0;
    auto bytes = reinterpret_cast<uint8_t*>(driver);
    for (int offset = 0x28; offset + 8 <= 0x400; offset += 8) {
        uintptr_t value;
        std::memcpy(&value, bytes + offset, 8);
        if (value == pendingNotify) {
            std::memcpy(bytes + offset, &worldNotify, 8);
            ++repointed;
            Log("research: driver notify at +0x%x repointed to the world", offset);
        }
    }
    UObject* worldDriver = nullptr;
    ue::GetProp(world, "NetDriver", worldDriver);
    Log("research: LoadMap intercept done: World.NetDriver=%s notify-repointed=%d",
        IsLive(worldDriver) ? FullName(worldDriver).c_str() : "null", repointed);
    g_acceptAnyP2P = true;
    return true;
}

bool InstallLoadMapHook(std::string& summary) {
    if (g_loadMapOriginal) return true;
    auto target = const_cast<uint8_t*>(CheckedCode(kLoadMapRva, kLoadMapPrologue, sizeof kLoadMapPrologue));
    if (!target) {
        summary = "UEngine::LoadMap prologue mismatch; not hooking";
        return false;
    }
    auto tramp = static_cast<uint8_t*>(VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!tramp) {
        summary = "VirtualAlloc failed";
        return false;
    }
    std::memcpy(tramp, target, sizeof kLoadMapPrologue);
    uint8_t jmp[14] = {0xFF, 0x25, 0, 0, 0, 0};
    uint64_t back = reinterpret_cast<uint64_t>(target + sizeof kLoadMapPrologue);
    std::memcpy(jmp + 6, &back, 8);
    std::memcpy(tramp + sizeof kLoadMapPrologue, jmp, sizeof jmp);
    uint8_t patch[sizeof kLoadMapPrologue];
    std::memset(patch, 0x90, sizeof patch);
    uint64_t hook = reinterpret_cast<uint64_t>(&HookedLoadMap);
    std::memcpy(jmp + 6, &hook, 8);
    std::memcpy(patch, jmp, sizeof jmp);
    DWORD old;
    if (!VirtualProtect(target, sizeof patch, PAGE_EXECUTE_READWRITE, &old)) {
        summary = "VirtualProtect failed";
        return false;
    }
    std::memcpy(target, patch, sizeof patch);
    VirtualProtect(target, sizeof patch, old, &old);
    FlushInstructionCache(GetCurrentProcess(), target, sizeof patch);
    g_loadMapOriginal = reinterpret_cast<LoadMapFn>(tramp);
    Log("research: UEngine::LoadMap hooked");
    return true;
}

// ---- Client keeps its own controller ---------------------------------------------------------------
// UNetConnection::HandleClientPlayer(APlayerController* PC, UNetConnection*) on build 19221447. On a client it
// destroys the local player's controller and makes the replicated one local (SetPlayer). Days Gone's
// single-player code then finds no Deacon for player 0 and crashes. The wrapper hides the local controller
// from it and gives it back to the local player afterwards: the replicated controller still gets its
// connection (Role, NetConnection, connection State/PlayerController/OwningActor) but no viewport or input.
constexpr uintptr_t kHandleClientPlayerRva = 0x29b1f40;
constexpr uint8_t kHandleClientPlayerPrologue[] = {0x48, 0x89, 0x5c, 0x24, 0x18, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41,
                                                   0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8b, 0xec, 0x48, 0x83, 0xec, 0x30};
using HandleClientPlayerFn = void (*)(UObject*, UObject*, UObject*);
HandleClientPlayerFn g_handleClientPlayerOriginal = nullptr;

void SetObjectProp(UObject* obj, const char* name, UObject* value) {
    if (auto prop = IsLive(obj) ? FindChild(obj->cls, name) : nullptr)
        std::memcpy(reinterpret_cast<uint8_t*>(obj) + PropertyOffset(prop), &value, sizeof value);
}

// The joined controller, watched while a trace runs so PlayerState/Pawn changes get a timestamp.
UObject* g_watchedController = nullptr;
UObject* g_watchedPlayerState = nullptr;
UObject* g_watchedPawn = nullptr;
unsigned g_watchedPawnRole = 0;

void HookedHandleClientPlayer(UObject* connection, UObject* pc, UObject* netConnection) {
    auto local = FindInstance("LocalPlayer");
    UObject* keep = nullptr;
    if (IsLive(local)) ue::GetProp(local, "PlayerController", keep);
    if (!IsLive(keep) || keep == pc) keep = nullptr;
    Log("research: HandleClientPlayer before: PC=%s Player=%s PlayerState=%s Pawn=%s local-controller=%s",
        IsLive(pc) ? FullName(pc).c_str() : "?", ObjectProp(pc, "Player").c_str(),
        ObjectProp(pc, "PlayerState").c_str(), ObjectProp(pc, "Pawn").c_str(),
        IsLive(keep) ? FullName(keep).c_str() : "none");
    if (keep) SetObjectProp(local, "PlayerController", nullptr);
    g_handleClientPlayerOriginal(connection, pc, netConnection);
    if (keep) {
        SetObjectProp(local, "PlayerController", keep);
        SetObjectProp(keep, "Player", local);
    }
    Log("research: HandleClientPlayer after: PC=%s Player=%s PlayerState=%s Pawn=%s local-player kept %s",
        IsLive(pc) ? FullName(pc).c_str() : "?", ObjectProp(pc, "Player").c_str(),
        ObjectProp(pc, "PlayerState").c_str(), ObjectProp(pc, "Pawn").c_str(),
        keep ? FullName(keep).c_str() : "nothing");
    if (IsLive(pc)) {
        g_watchedController = pc;
        g_watchedPlayerState = nullptr;
        g_watchedPawn = nullptr;
        ue::GetProp(pc, "PlayerState", g_watchedPlayerState);
        ue::GetProp(pc, "Pawn", g_watchedPawn);
    }
}

bool InstallHandleClientPlayerHook(std::string& summary) {
    if (g_handleClientPlayerOriginal) return true;
    auto code = CheckedCode(kHandleClientPlayerRva, kHandleClientPlayerPrologue, sizeof kHandleClientPlayerPrologue);
    auto cdo = FindDefaultObject("SteamNetConnection");
    if (!code || !cdo) {
        summary = code ? "no SteamNetConnection default object" : "HandleClientPlayer prologue mismatch";
        return false;
    }
    auto vtable = cdo->vtable;
    for (int slot = 0; slot < 512; ++slot) {
        if (!ReadableMemory(vtable + slot, sizeof(void*))) break;
        if (vtable[slot] != code) continue;
        DWORD old;
        if (!VirtualProtect(vtable + slot, sizeof(void*), PAGE_READWRITE, &old)) {
            summary = "VirtualProtect on the vtable failed";
            return false;
        }
        g_handleClientPlayerOriginal = reinterpret_cast<HandleClientPlayerFn>(const_cast<uint8_t*>(code));
        vtable[slot] = reinterpret_cast<void*>(&HookedHandleClientPlayer);
        VirtualProtect(vtable + slot, sizeof(void*), old, &old);
        Log("research: SteamNetConnection.HandleClientPlayer (vtable slot %d) wrapped", slot);
        return true;
    }
    summary = "HandleClientPlayer not found in the SteamNetConnection vtable";
    return false;
}

// ---- Thin-client experiment -----------------------------------------------------------------------
namespace {

// The joined player's controller: on the host it is an authority controller with a NetConnection; on the
// guest it is the autonomous-proxy controller the host replicated.
UObject* RemoteController(uint8_t role) {
    auto cls = FindClass("PlayerController");
    for (int32_t i = 0, n = ObjectCount(); cls && i < n; ++i) {
        auto obj = ObjectAt(i);
        if (!IsLive(obj) || !IsLive(obj->cls) || IsDefaultOrArchetype(obj) || !IsA(obj, cls)) continue;
        UObject* connection = nullptr;
        uint8_t actual = 0;
        if (ue::GetProp(obj, "NetConnection", connection) && connection && ue::GetProp(obj, "Role", actual) && actual == role)
            return obj;
    }
    return nullptr;
}

UObject* g_takeoverOwnController = nullptr;
UObject* NetConnectionOf(UObject* actor);

} // namespace

bool NetPossess(UObject* hostPawn, const std::string& pawnClass, std::string& summary) {
    auto pc = RemoteController(3);
    if (!pc) {
        summary = "no joined player controller on this PC (run net-listen and let the guest connect first)";
        return false;
    }
    UObject* current = nullptr;
    ue::GetProp(pc, "Pawn", current);
    if (IsLive(current)) {
        summary = "the joined controller already possesses " + FullName(current);
        return false;
    }
    if (!IsLive(hostPawn)) {
        summary = "the host needs a gameplay pawn to spawn the guest next to";
        return false;
    }
    auto cls = FindClass(pawnClass.empty() ? "BendDefaultPlayerPawn_C" : pawnClass.c_str());
    auto statics = FindDefaultObject("GameplayStatics");
    if (!cls || !statics) {
        summary = "pawn class or GameplayStatics not found";
        return false;
    }
    Call where(hostPawn, "K2_GetActorLocation");
    if (!where.invoke()) {
        summary = "could not read the host pawn's location";
        return false;
    }
    FVector at = where.get<FVector>("ReturnValue");
    FTransform xf{};
    xf.rotation = {0, 0, 0, 1};
    xf.translation = {at.x + 300, at.y, at.z + 50};
    xf.scale = {1, 1, 1};
    Call begin(statics, "BeginSpawningActorFromClass");
    begin.set("WorldContextObject", hostPawn).set("ActorClass", cls).set("SpawnTransform", xf)
        .setBool("bNoCollisionFail", true).set("Owner", pc);
    if (!begin.invoke() || !IsLive(begin.get<UObject*>("ReturnValue"))) {
        summary = "BeginSpawningActorFromClass failed";
        return false;
    }
    auto pawn = begin.get<UObject*>("ReturnValue");
    Log("research: net-possess: spawning %s for %s", FullName(pawn).c_str(), FullName(pc).c_str());
    Call finish(statics, "FinishSpawningActor");
    finish.set("Actor", pawn).set("SpawnTransform", xf).invoke();
    if (!IsLive(pawn)) {
        summary = "the spawned pawn did not survive FinishSpawningActor";
        return false;
    }
    Log("research: net-possess: spawned; possessing");
    bool possessed = Call(pc, "Possess").set("InPawn", pawn).invoke();
    UObject* after = nullptr;
    ue::GetProp(pc, "Pawn", after);
    Log("research: net-possess: pawn RemoteRole=%s Controller=%s GetNetConnection=%s controller NetConnection=%s",
        ByteProp(pawn, "RemoteRole").c_str(), ObjectProp(pawn, "Controller").c_str(),
        IsLive(NetConnectionOf(pawn)) ? FullName(NetConnectionOf(pawn)).c_str() : "null",
        ObjectProp(pc, "NetConnection").c_str());
    summary = std::string("Possess ") + (possessed ? "ran" : "failed") + "; " + FullName(pc) + ".Pawn=" +
              (IsLive(after) ? FullName(after) : std::string("null"));
    return possessed && after == pawn;
}

// APlayerController::BuildInputStack (vtable +0xcc8, 0x2a98560): after a takeover the old local pawn's
// InputComponent can stay on the stack and consume look input, so the taken-over controller drops components
// owned by any pawn other than its own.
using BuildInputStackFn = void (*)(UObject*, void*);
BuildInputStackFn g_buildInputStackOriginal = nullptr;
UObject* g_inputStackOwner = nullptr;

void HookedBuildInputStack(UObject* pc, void* array) {
    g_buildInputStackOriginal(pc, array);
    static int calls = 0;
    if (calls++ < 8) Log("research: BuildInputStack #%d on %s", calls, IsLive(pc) ? Name(pc).c_str() : "?");
    if (pc != g_inputStackOwner) return;
    struct WeakPtr {
        int32_t index, serial;
    };
    struct StackArray {
        WeakPtr* data;
        int32_t num, max;
    };
    auto stack = static_cast<StackArray*>(array);
    UObject* pawn = nullptr;
    ue::GetProp(pc, "Pawn", pawn);
    static auto pawnClass = FindClass("Pawn");
    int32_t kept = 0;
    for (int32_t i = 0; i < stack->num; ++i) {
        auto component = ObjectAt(stack->data[i].index);
        auto owner = IsLive(component) ? component->outer : nullptr;
        if (IsLive(owner) && pawnClass && IsA(owner, pawnClass) && owner != pawn) {
            static int logged = 0;
            if (logged++ < 4) Log("research: input stack dropped %s", FullName(component).c_str());
            continue;
        }
        stack->data[kept++] = stack->data[i];
    }
    stack->num = kept;
    // Blueprint actors push their InputComponents onto GetPlayerController(0), which is still the guest's own
    // controller; the taken-over controller adopts that CurrentInputStack (+0x5c8 data, +0x5d0 count). In Bend
    // each entry points at a handle whose first qword is component+0x30; the stock FWeakObjectPtr constructor
    // (0x1aceb20) turns the component into the stack's weak pointer.
    if (IsLive(g_takeoverOwnController) && g_takeoverOwnController != pc) {
        using MakeWeakFn = void (*)(WeakPtr*, UObject*);
        auto makeWeak = reinterpret_cast<MakeWeakFn>(g.imageBase + 0x1aceb20);
        auto base = reinterpret_cast<uint8_t*>(g_takeoverOwnController);
        auto handles = *reinterpret_cast<uint8_t***>(base + 0x5c8);
        auto count = *reinterpret_cast<int32_t*>(base + 0x5d0);
        if (count < 0 || count > 64 || !ReadableMemory(handles, count * sizeof(void*))) count = 0;
        for (int32_t i = 0; i < count; ++i) {
            auto inner = ReadableMemory(handles[i], sizeof(void*)) ? *reinterpret_cast<uint8_t**>(handles[i]) : nullptr;
            auto component = inner ? reinterpret_cast<UObject*>(inner - 0x30) : nullptr;
            if (!IsLive(component)) continue;
            if (stack->num >= stack->max) {
                static int full = 0;
                if (full++ < 4) Log("research: input stack full (max %d); %s not adopted", stack->max, FullName(component).c_str());
                continue;
            }
            WeakPtr weak{-1, 0};
            makeWeak(&weak, component);
            stack->data[stack->num++] = weak;
            static int adopted = 0;
            if (adopted++ < 8) Log("research: input stack adopted %s", FullName(component).c_str());
        }
    }
}

// Both local controllers run ProcessPlayerInput every frame against one shared, cached input stack, so the
// guest's own controller rebuilt it with its old pawn's bindings. While a takeover is active it is skipped.
namespace {
void InstallProcessInputHook(UObject* pc);
}

static void MarkInputStackDirty(UObject* pc) {
    auto dirty = reinterpret_cast<uint8_t*>(pc) + 0x5d8;
    Log("research: input stack dirty byte on %s was %u; set to 1", Name(pc).c_str(), *dirty);
    *dirty = 1;
}

// UWorld::PlayerControllerList (+0x560, TArray<TWeakObjectPtr<APlayerController>>) decides
// GetPlayerController(0) and Bend's GetPlayerCharacter(0) (0x5ad4b0). Bend's UpdateRotation (via 0x5b6040)
// picks the mouse look path only when player 0's pawn has look input, and Blueprint actors bind input to
// player 0, so the controller the local player drives must be first.
static void PromoteController(UObject* pc) {
    auto world = FindGameWorld();
    if (!IsLive(world) || !IsLive(pc)) return;
    struct WeakPtr {
        int32_t index, serial;
    };
    struct List {
        WeakPtr* data;
        int32_t num, max;
    };
    auto list = reinterpret_cast<List*>(reinterpret_cast<uint8_t*>(world) + 0x560);
    if (list->num <= 0 || list->num > 16 || !ReadableMemory(list->data, list->num * sizeof(WeakPtr))) return;
    for (int32_t i = 0; i < list->num; ++i)
        if (list->data[i].index == pc->internalIndex) {
            if (i == 0) return;
            auto entry = list->data[i];
            for (int32_t j = i; j > 0; --j) list->data[j] = list->data[j - 1];
            list->data[0] = entry;
            Log("research: %s moved from %d to the front of the world's PlayerControllerList (%d entries)",
                Name(pc).c_str(), i, list->num);
            return;
        }
    Log("research: %s is not in the world's PlayerControllerList", Name(pc).c_str());
}

static void FixTakeoverInput(UObject* pc, UObject* pawn) {
    constexpr int kBuildInputStackSlot = 0xcc8 / 8;
    if (!g_buildInputStackOriginal &&
        reinterpret_cast<uintptr_t>(pc->vtable[kBuildInputStackSlot]) == g.imageBase + 0x2a98560) {
        g_buildInputStackOriginal = reinterpret_cast<BuildInputStackFn>(pc->vtable[kBuildInputStackSlot]);
        DWORD old;
        if (VirtualProtect(pc->vtable + kBuildInputStackSlot, sizeof(void*), PAGE_READWRITE, &old)) {
            pc->vtable[kBuildInputStackSlot] = reinterpret_cast<void*>(&HookedBuildInputStack);
            VirtualProtect(pc->vtable + kBuildInputStackSlot, sizeof(void*), old, &old);
        }
    }
    g_inputStackOwner = pc;
    // APawn::bInputEnabled (+0x350 bit 4) gates the pawn's InputComponent in BuildInputStack.
    auto bits = reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(pawn) + 0x350);
    Log("research: net-takeover: %s bInputEnabled=%u; BuildInputStack %s", Name(pawn).c_str(), (*bits >> 4) & 1,
        g_buildInputStackOriginal ? "filtered" : "not hooked (unexpected slot)");
    *bits |= 1u << 4;
    // InputYawScale/InputPitchScale (+0x53c/+0x540) carry the player's look scaling; the replicated controller
    // only has class defaults.
    if (IsLive(g_takeoverOwnController)) {
        auto from = reinterpret_cast<const float*>(reinterpret_cast<uint8_t*>(g_takeoverOwnController) + 0x53c);
        auto to = reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(pc) + 0x53c);
        Log("research: net-takeover: input yaw/pitch scale %.3f/%.3f -> %.3f/%.3f", to[0], to[1], from[0], from[1]);
        to[0] = from[0];
        to[1] = from[1];
    }
    InstallProcessInputHook(pc);
    MarkInputStackDirty(pc);
    PromoteController(pc);
}

bool NetTakeover(std::string& summary) {
    auto local = FindInstance("LocalPlayer");
    if (!IsLive(local)) {
        summary = "no local player";
        return false;
    }
    UObject* current = nullptr;
    ue::GetProp(local, "PlayerController", current);
    // The SetPlayer slot is the one HandleClientPlayer calls on the new controller (vtable +0xc70).
    constexpr int kSetPlayerSlot = 0xc70 / 8;
    using SetPlayerFn = void (*)(UObject*, UObject*);
    auto setPlayer = [&](UObject* pc) {
        auto fn = pc->vtable[kSetPlayerSlot];
        auto at = reinterpret_cast<uintptr_t>(fn);
        if (at <= g.imageBase || at >= g.imageBase + 0x10000000) return false;
        reinterpret_cast<SetPlayerFn>(fn)(pc, local);
        return true;
    };
    if (IsLive(g_takeoverOwnController) && current != g_takeoverOwnController) {
        // Second run: give the local player its own controller back.
        if (IsLive(current)) SetObjectProp(current, "Player", nullptr);
        SetObjectProp(local, "PlayerController", nullptr);
        bool ok = setPlayer(g_takeoverOwnController);
        g_inputStackOwner = nullptr;
        MarkInputStackDirty(g_takeoverOwnController);
        PromoteController(g_takeoverOwnController);
        summary = std::string("returned the local player to ") + FullName(g_takeoverOwnController) + (ok ? "" : " (SetPlayer failed)");
        g_takeoverOwnController = nullptr;
        return ok;
    }
    auto pc = RemoteController(2);
    if (!pc) {
        summary = "no replicated controller on this PC (run net-connect first)";
        return false;
    }
    UObject* pawn = nullptr;
    ue::GetProp(pc, "Pawn", pawn);
    if (!IsLive(pawn)) {
        summary = "the replicated controller has no pawn yet (run net-possess on the host first)";
        return false;
    }
    g_takeoverOwnController = IsLive(current) ? current : nullptr;
    if (g_takeoverOwnController) SetObjectProp(g_takeoverOwnController, "Player", nullptr);
    SetObjectProp(local, "PlayerController", nullptr);
    if (!setPlayer(pc)) {
        summary = "SetPlayer slot does not point into the executable; nothing changed";
        if (g_takeoverOwnController) {
            SetObjectProp(local, "PlayerController", g_takeoverOwnController);
            SetObjectProp(g_takeoverOwnController, "Player", local);
        }
        g_takeoverOwnController = nullptr;
        return false;
    }
    Log("research: net-takeover: %s is now the local controller", FullName(pc).c_str());
    bool restarted = Call(pc, "ClientRestart").set("NewPawn", pawn).invoke();
    bool viewed = Call(pc, "SetViewTargetWithBlend").set("NewViewTarget", pawn).set("BlendTime", 0.f).invoke();
    if (g_clientPossess) {
        // ABendPlayerController::Possess (vtable +0x700, 0x799ea0) calls the authority-only engine Possess, which
        // only logs on a client, and then does Bend's local setup: pawn delegate bindings and the native player
        // navigation object at +0x2078 that CancelCurrentPlayerNavigation dereferences. A replicated controller
        // never runs it, because the engine only possesses on the server.
        constexpr int kPossessSlot = 0x700 / 8;
        auto navigation = [&] { return *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(pc) + 0x2078); };
        void* before = navigation();
        auto fn = reinterpret_cast<uintptr_t>(pc->vtable[kPossessSlot]);
        if (fn == g.imageBase + 0x799ea0) {
            reinterpret_cast<void (*)(UObject*, UObject*)>(fn)(pc, pawn);
            Log("research: net-takeover: Bend Possess ran on the client; navigation object %p -> %p", before, navigation());
        } else {
            Log("research: net-takeover: Possess slot is exe+0x%llx, not Bend's; skipped",
                static_cast<unsigned long long>(fn - g.imageBase));
        }
    }
    // APlayerController::ProcessPlayerInput (0x2aa84e0) keeps the input stack in one static array and only
    // rebuilds it (BuildInputStack, +0xcc8) while the ticking controller's dirty byte +0x5d8 is set. A replicated
    // controller never has it set, so it would keep feeding look input to the old local pawn's InputComponent.
    FixTakeoverInput(pc, pawn);
    summary = "local player now drives " + FullName(pawn) + " through " + FullName(pc) + " (ClientRestart " +
              (restarted ? "ran" : "failed") + ", view target " + (viewed ? "set" : "not set") + "); own controller kept as " +
              (g_takeoverOwnController ? FullName(g_takeoverOwnController) : std::string("none"));
    return true;
}

namespace {

std::string Describe(UObject* v) { return v ? (IsLive(v) ? FullName(v) : std::string("<stale>")) : std::string("null"); }

// AActor::GetNetConnection is vtable slot 149 (+0x4a8) on this build: APawn (0x2a55380) forwards to its
// Controller, APlayerController (0x2aa2780) returns NetConnection while Player is set. UActorChannel::
// ReplicateActor (0x2831ee0) compares it with the channel's connection to decide bNetOwner.
UObject* NetConnectionOf(UObject* actor) {
    constexpr int kGetNetConnectionSlot = 0x4a8 / 8;
    if (!IsLive(actor)) return nullptr;
    auto fn = reinterpret_cast<uintptr_t>(actor->vtable[kGetNetConnectionSlot]);
    if (fn <= g.imageBase || fn >= g.imageBase + 0x10000000) return nullptr;
    return reinterpret_cast<UObject* (*)(UObject*)>(fn)(actor);
}

std::string CallBool(UObject* obj, const char* function) {
    Call c(obj, function);
    return c.ok() && c.invoke() ? (c.get<bool>("ReturnValue") ? "true" : "false") : "?";
}

void SetByteProp(UObject* obj, const char* name, uint8_t value) {
    if (auto prop = IsLive(obj) ? FindChild(obj->cls, name) : nullptr)
        reinterpret_cast<uint8_t*>(obj)[PropertyOffset(prop)] = value;
}

std::string TickBytes(UObject* actor) {
    // PrimaryActorTick is at +40 (80 bytes); bytes 8..23 hold the tick groups, flag bits and tick state.
    auto tick = reinterpret_cast<const uint8_t*>(actor) + 40;
    std::string text;
    char hex[4];
    for (int i = 8; i < 24; ++i) {
        std::snprintf(hex, sizeof hex, "%02x", tick[i]);
        text += hex;
    }
    return text;
}

void DiagPawn(UObject* pawn) {
    FVector at{};
    if (Call where(pawn, "K2_GetActorLocation"); where.invoke()) at = where.get<FVector>("ReturnValue");
    uint64_t weakOwner = 0;
    ue::GetProp(pawn, "Owner", weakOwner);
    UObject* movement = nullptr;
    ue::GetProp(pawn, "CharacterMovement", movement);
    Log("research: diag PAWN %s Role=%s RemoteRole=%s Controller=%s PlayerState=%s GetNetConnection=%s "
        "IsLocallyControlled=%s InputComponent=%s Owner=%s ownerWeak=%016llx at=(%.0f %.0f %.0f) "
        "MovementMode=%s tick=%s",
        FullName(pawn).c_str(), ByteProp(pawn, "Role").c_str(), ByteProp(pawn, "RemoteRole").c_str(),
        ObjectProp(pawn, "Controller").c_str(), ObjectProp(pawn, "PlayerState").c_str(),
        Describe(NetConnectionOf(pawn)).c_str(), CallBool(pawn, "IsLocallyControlled").c_str(),
        ObjectProp(pawn, "InputComponent").c_str(), ObjectProp(pawn, "Owner").c_str(),
        static_cast<unsigned long long>(weakOwner), at.x, at.y, at.z,
        IsLive(movement) ? ByteProp(movement, "MovementMode").c_str() : "?", TickBytes(pawn).c_str());
}

UObject* LocalController() {
    auto local = FindInstance("LocalPlayer");
    UObject* pc = nullptr;
    if (IsLive(local)) ue::GetProp(local, "PlayerController", pc);
    return IsLive(pc) ? pc : nullptr;
}

UObject* g_forcedAutonomousPawn = nullptr;

} // namespace

bool NetDiag(std::string& summary) {
    auto localPc = LocalController();
    Log("research: diag LocalPlayer.PlayerController=%s", Describe(localPc).c_str());
    if (auto statics = FindDefaultObject("GameplayStatics"); statics && localPc) {
        Call first(statics, "GetPlayerController");
        first.set("WorldContextObject", localPc).set<int32_t>("PlayerIndex", 0);
        if (first.ok() && first.invoke())
            Log("research: diag GetPlayerController(0)=%s", Describe(first.get<UObject*>("ReturnValue")).c_str());
    }
    int controllers = 0, pawns = 0;
    auto pcClass = FindClass("PlayerController");
    auto pawnClass = FindClass("BendDefaultPlayerPawn_C");
    for (int i = 0; i < ObjectCount(); ++i) {
        auto obj = ObjectAt(i);
        if (!IsLive(obj) || !IsLive(obj->cls) || IsDefaultOrArchetype(obj)) continue;
        if (pcClass && IsA(obj, pcClass)) {
            ++controllers;
            FRotator rot{};
            ue::GetProp(obj, "ControlRotation", rot);
            Log("research: diag PC %s Role=%s RemoteRole=%s Player=%s NetConnection=%s GetNetConnection=%s "
                "IsLocalController=%s Pawn=%s AcknowledgedPawn=%s PlayerInput=%s InputComponent=%s "
                "PlayerCameraManager=%s ControlRotation=(%.1f %.1f %.1f) tick=%s",
                FullName(obj).c_str(), ByteProp(obj, "Role").c_str(), ByteProp(obj, "RemoteRole").c_str(),
                ObjectProp(obj, "Player").c_str(), ObjectProp(obj, "NetConnection").c_str(),
                Describe(NetConnectionOf(obj)).c_str(), CallBool(obj, "IsLocalController").c_str(),
                ObjectProp(obj, "Pawn").c_str(), ObjectProp(obj, "AcknowledgedPawn").c_str(),
                ObjectProp(obj, "PlayerInput").c_str(), ObjectProp(obj, "InputComponent").c_str(),
                ObjectProp(obj, "PlayerCameraManager").c_str(), rot.pitch, rot.yaw, rot.roll, TickBytes(obj).c_str());
        } else if (pawnClass && IsA(obj, pawnClass)) {
            ++pawns;
            DiagPawn(obj);
        }
    }
    summary = std::to_string(controllers) + " player controllers and " + std::to_string(pawns) +
              " player pawns logged";
    return true;
}

bool NetAutonomous(std::string& summary) {
    auto pc = LocalController();
    UObject* pawn = nullptr;
    if (pc) ue::GetProp(pc, "Pawn", pawn);
    if (!IsLive(pawn)) {
        summary = "the local controller has no pawn";
        return false;
    }
    uint8_t role = 0;
    ue::GetProp(pawn, "Role", role);
    if (pawn == g_forcedAutonomousPawn && role == 2) {
        SetByteProp(pawn, "Role", 1);
        g_forcedAutonomousPawn = nullptr;
        summary = FullName(pawn) + " Role 2 -> 1 (restored)";
        return true;
    }
    if (role != 1) {
        summary = FullName(pawn) + " Role is " + std::to_string(role) + ", not SimulatedProxy; nothing changed";
        return false;
    }
    SetByteProp(pawn, "Role", 2);
    g_forcedAutonomousPawn = pawn;
    summary = FullName(pawn) + " Role 1 -> 2 (forced AutonomousProxy on this PC only)";
    return true;
}

bool NetNudge(std::string& summary) {
    auto pc = LocalController();
    if (!pc) {
        summary = "no local controller";
        return false;
    }
    FRotator before{};
    ue::GetProp(pc, "ControlRotation", before);
    bool yaw = Call(pc, "AddYawInput").set("Val", 30.f).invoke();
    char text[160];
    std::snprintf(text, sizeof text, "AddYawInput(30) %s on %s; ControlRotation yaw before %.1f (run net-diag to compare)",
                  yaw ? "ran" : "failed", Name(pc).c_str(), before.yaw);
    summary = text;
    return yaw;
}

namespace {

// APlayerController::InputKey (vtable +0xc50, 0x2aa4620; Bend first offers the key to a global interceptor)
// and InputAxis (+0xc60, 0x2aa4550) on build 19221447. Both controller classes share the slot, so the
// wrappers count per controller object to show which one the viewport feeds.
constexpr int kInputKeySlot = 0xc50 / 8, kInputAxisSlot = 0xc60 / 8;
constexpr uintptr_t kInputKeyRva = 0x2aa4620, kInputAxisRva = 0x2aa4550;
using InputKeyFn = bool (*)(UObject*, const int32_t*, int32_t, float, bool);
using InputAxisFn = bool (*)(UObject*, const int32_t*, float, float, int32_t, bool);
InputKeyFn g_inputKeyOriginal = nullptr;
InputAxisFn g_inputAxisOriginal = nullptr;
void** g_inputVtable = nullptr;

struct InputCount {
    UObject* pc = nullptr;
    uint64_t keys = 0, axes = 0, rotations = 0, processes = 0, dirtyAtProcess = 0;
    double addYaw = 0, rotationInputYaw = 0, rotationDelta = 0;
    std::string firstKeys;
};

// APlayerController::UpdateRotation (PC vtable +0xaf8; Bend override 0x7a5f90): the camera manager turns
// RotationInput (PC +0x530) into a view rotation (PC +0x1fe4) that becomes ControlRotation.
constexpr int kUpdateRotationSlot = 0xaf8 / 8;
constexpr uintptr_t kUpdateRotationRva = 0x7a5f90;
using UpdateRotationFn = void (*)(UObject*, float);
UpdateRotationFn g_updateRotationOriginal = nullptr;
InputCount g_inputCounts[6];

// BendPlayer pawn look handlers (vtable +0xbb8 = 0x77c640, +0xbc0 = 0x77c6f0): both forward the axis value to
// Controller->AddYawInput (pawn +0x380). Counting them per pawn shows whether the guest's look input reaches its pawn.
constexpr int kLookSlots[2] = {0xbb8 / 8, 0xbc0 / 8};
constexpr uintptr_t kLookRvas[2] = {0x77c640, 0x77c6f0};
using LookFn = void (*)(UObject*, float);
LookFn g_lookOriginal[2] = {};
struct LookCount {
    UObject* pawn = nullptr;
    uint64_t calls[2] = {}, nonzero[2] = {};
    double sum[2] = {};
};
LookCount g_lookCounts[6];

LookCount* LookCountFor(UObject* pawn) {
    for (auto& c : g_lookCounts)
        if (c.pawn == pawn) return &c;
    for (auto& c : g_lookCounts)
        if (!c.pawn) {
            c.pawn = pawn;
            return &c;
        }
    return nullptr;
}

std::string ExeStack();

template <int Index> void HookedLook(UObject* pawn, float value) {
    if (auto c = LookCountFor(pawn)) {
        if (c->calls[Index]++ == 0)
            Log("research: first look%d on %s value=%.3f stack:%s", Index, IsLive(pawn) ? Name(pawn).c_str() : "?", value,
                ExeStack().c_str());
        if (value != 0.f) ++c->nonzero[Index];
        c->sum[Index] += value;
    }
    g_lookOriginal[Index](pawn, value);
}

// Bend camera instance AddRotationInput (vtable +0x248), fed by the camera manager's UpdateRotation (0x45d610)
// with the sensitivity-scaled RotationInput packed as an FVector2D.
bool PatchSlot(void** vtable, int slot, void* replacement);
constexpr int kCameraInputSlot = 0x248 / 8;
using CameraInputFn = bool (*)(void*, uint64_t);
CameraInputFn g_cameraInputOriginal = nullptr;
struct CameraInputCount {
    void* camera = nullptr;
    uint64_t calls = 0;
    double sum[2] = {};
};
CameraInputCount g_cameraInputs[6];

bool HookedCameraInput(void* camera, uint64_t packed) {
    float v[2];
    std::memcpy(v, &packed, sizeof v);
    for (auto& c : g_cameraInputs)
        if (c.camera == camera || !c.camera) {
            c.camera = camera;
            ++c.calls;
            c.sum[0] += v[0];
            c.sum[1] += v[1];
            break;
        }
    return g_cameraInputOriginal(camera, packed);
}

void HookCameraInput(UObject* pc) {
    UObject* manager = nullptr;
    ue::GetProp(pc, "PlayerCameraManager", manager);
    if (!IsLive(manager)) return;
    auto cm = reinterpret_cast<uint8_t*>(manager);
    auto stack = *reinterpret_cast<UObject***>(cm + 0x35d0);
    if (*reinterpret_cast<int32_t*>(cm + 0x35d8) < 1 || !ReadableMemory(stack, sizeof(void*)) || !IsLive(stack[0])) return;
    auto camera = stack[0];
    auto slot = camera->vtable[kCameraInputSlot];
    if (slot == reinterpret_cast<void*>(&HookedCameraInput)) return;
    if (g_cameraInputOriginal && slot != reinterpret_cast<void*>(g_cameraInputOriginal)) {
        Log("research: camera %s has a different AddRotationInput; not hooked", Name(camera->cls).c_str());
        return;
    }
    g_cameraInputOriginal = reinterpret_cast<CameraInputFn>(slot);
    PatchSlot(camera->vtable, kCameraInputSlot, reinterpret_cast<void*>(&HookedCameraInput));
    auto rva = [&](int offset) {
        return static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(camera->vtable[offset / 8]) - g.imageBase);
    };
    Log("research: camera vtable exe+0x%llx slots 0x2d8=exe+0x%llx 0x308=exe+0x%llx 0x320=exe+0x%llx",
        static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(camera->vtable) - g.imageBase), rva(0x2d8),
        rva(0x308), rva(0x320));
    Log("research: hooked AddRotationInput on %s (exe+0x%llx)", Name(camera->cls).c_str(),
        static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(slot) - g.imageBase));
}

InputCount* InputCountFor(UObject* pc) {
    for (auto& c : g_inputCounts)
        if (c.pc == pc) return &c;
    for (auto& c : g_inputCounts)
        if (!c.pc) {
            c.pc = pc;
            return &c;
        }
    return nullptr;
}

std::string ExeStack() {
    void* frames[24]{};
    USHORT count = RtlCaptureStackBackTrace(1, 24, frames, nullptr);
    std::string text;
    char item[32];
    for (USHORT f = 0; f < count; ++f) {
        auto at = reinterpret_cast<uintptr_t>(frames[f]);
        if (at > g.imageBase && at < g.imageBase + 0x10000000) {
            std::snprintf(item, sizeof item, " exe+0x%llx", static_cast<unsigned long long>(at - g.imageBase));
            text += item;
        }
    }
    return text;
}

bool HookedInputKey(UObject* pc, const int32_t* key, int32_t event, float amount, bool gamepad) {
    if (auto c = InputCountFor(pc)) {
        if (c->keys == 0)
            Log("research: input first key on %s: %s event=%d gamepad=%d stack:%s", IsLive(pc) ? Name(pc).c_str() : "?",
                NameString(key[0]).c_str(), event, gamepad ? 1 : 0, ExeStack().c_str());
        if (++c->keys <= 12 && event == 0) c->firstKeys += " " + NameString(key[0]);
    }
    return g_inputKeyOriginal(pc, key, event, amount, gamepad);
}

bool HookedInputAxis(UObject* pc, const int32_t* key, float delta, float deltaTime, int32_t samples, bool gamepad) {
    if (auto c = InputCountFor(pc)) {
        if (c->axes == 0)
            Log("research: input first axis on %s: %s delta=%.3f gamepad=%d stack:%s", IsLive(pc) ? Name(pc).c_str() : "?",
                NameString(key[0]).c_str(), delta, gamepad ? 1 : 0, ExeStack().c_str());
        if (delta != 0.f) ++c->axes;
    }
    return g_inputAxisOriginal(pc, key, delta, deltaTime, samples, gamepad);
}

void HookedUpdateRotation(UObject* pc, float deltaTime) {
    if (auto c = InputCountFor(pc)) {
        ++c->rotations;
        c->rotationInputYaw += reinterpret_cast<const float*>(reinterpret_cast<const uint8_t*>(pc) + 0x530)[1];
        c->rotationDelta += deltaTime;
    }
    g_updateRotationOriginal(pc, deltaTime);
}

// APlayerController::AddYawInput (PC vtable +0xbb0, 0x2a96270).
constexpr int kAddYawSlot = 0xbb0 / 8;
constexpr uintptr_t kAddYawRva = 0x2a96270;
using AddYawFn = void (*)(UObject*, float);
AddYawFn g_addYawOriginal = nullptr;

void HookedAddYaw(UObject* pc, float value) {
    if (auto c = InputCountFor(pc)) c->addYaw += value;
    g_addYawOriginal(pc, value);
}

// ABendPlayerController::ProcessPlayerInput (PC vtable +0xcc0, 0x79b1c0), called from TickPlayerInput.
constexpr int kProcessInputSlot = 0xcc0 / 8;
constexpr uintptr_t kProcessInputRva = 0x79b1c0;
using ProcessInputFn = void (*)(UObject*, float, bool);
ProcessInputFn g_processInputOriginal = nullptr;

void HookedProcessInput(UObject* pc, float deltaTime, bool paused) {
    if (auto c = InputCountFor(pc)) {
        ++c->processes;
        if (reinterpret_cast<const uint8_t*>(pc)[0x5d8]) ++c->dirtyAtProcess;
    }
    if (g_inputStackOwner && pc == g_takeoverOwnController) return;
    g_processInputOriginal(pc, deltaTime, paused);
}

bool PatchSlot(void** vtable, int slot, void* replacement) {
    DWORD old;
    if (!VirtualProtect(vtable + slot, sizeof(void*), PAGE_READWRITE, &old)) return false;
    vtable[slot] = replacement;
    VirtualProtect(vtable + slot, sizeof(void*), old, &old);
    return true;
}

void InstallProcessInputHook(UObject* pc) {
    if (g_processInputOriginal) return;
    if (reinterpret_cast<uintptr_t>(pc->vtable[kProcessInputSlot]) != g.imageBase + kProcessInputRva) {
        Log("research: ProcessPlayerInput slot unexpected; own controller keeps processing input");
        return;
    }
    g_processInputOriginal = reinterpret_cast<ProcessInputFn>(pc->vtable[kProcessInputSlot]);
    PatchSlot(pc->vtable, kProcessInputSlot, reinterpret_cast<void*>(&HookedProcessInput));
}

// net-walk: feed AddMovementInput to the local controller's pawn from its own ReceiveTick for a few seconds.
UObject* g_walkPawn = nullptr;
double g_walkUntil = 0;
uint64_t g_walkSteps = 0;
FVector g_walkFrom{};
double g_walkNextReport = 0;
bool g_walkConsume = false;

// UPawnMovementComponent::ConsumeInputVector (CMC vtable +0x530; Bend 0x474c60). Bend's CMC TickComponent only
// reads LastControlInputVector (pawn +0x39c), so whoever calls this slot decides whether a pawn's input is used.
constexpr int kConsumeSlot = 0x530 / 8;
using ConsumeFn = FVector* (*)(UObject*, FVector*);
ConsumeFn g_consumeOriginal = nullptr;
void** g_consumeVtable = nullptr;
struct ConsumeCount {
    UObject* movement = nullptr;
    uint64_t calls = 0;
};
ConsumeCount g_consumeCounts[8];

ConsumeCount* ConsumeCountFor(UObject* movement) {
    for (auto& c : g_consumeCounts)
        if (c.movement == movement) return &c;
    for (auto& c : g_consumeCounts)
        if (!c.movement) {
            c.movement = movement;
            return &c;
        }
    return nullptr;
}

FVector* HookedConsume(UObject* movement, FVector* out) {
    if (auto c = ConsumeCountFor(movement); c && c->calls++ == 0)
        Log("research: first ConsumeInputVector on %s stack:%s", IsLive(movement) ? Describe(movement).c_str() : "?",
            ExeStack().c_str());
    return g_consumeOriginal(movement, out);
}

uint64_t ConsumeCalls(UObject* movement) {
    for (auto& c : g_consumeCounts)
        if (c.movement == movement) return c.calls;
    return 0;
}

void WrapConsume(UObject* movement) {
    if (g_consumeVtable || !IsLive(movement)) return;
    auto vtable = movement->vtable;
    g_consumeOriginal = reinterpret_cast<ConsumeFn>(vtable[kConsumeSlot]);
    if (PatchSlot(vtable, kConsumeSlot, reinterpret_cast<void*>(&HookedConsume))) {
        g_consumeVtable = vtable;
        Log("research: ConsumeInputVector wrapped on %s (original exe+0x%llx)", Name(movement->cls).c_str(),
            static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(g_consumeOriginal) - g.imageBase));
    }
}

// Movement telemetry for net-walk: Days Gone drives ground locomotion through desiredSpeed and anim root motion,
// so these fields show which stage drops the input on a net client.
void WalkReport(UObject* pawn, const char* stage) {
    FVector at{}, velocity{}, acceleration{};
    if (Call where(pawn, "K2_GetActorLocation"); where.invoke()) at = where.get<FVector>("ReturnValue");
    UObject* movement = nullptr;
    ue::GetProp(pawn, "CharacterMovement", movement);
    float desired = -1, smoothed = -1, maxWalk = -1;
    std::string mode = "?";
    if (IsLive(movement)) {
        ue::GetProp(movement, "Velocity", velocity);
        ue::GetProp(movement, "Acceleration", acceleration);
        ue::GetProp(movement, "desiredSpeed", desired);
        ue::GetProp(movement, "DesiredSpeedSmoothed", smoothed);
        ue::GetProp(movement, "MaxWalkSpeed", maxWalk);
        mode = ByteProp(movement, "MovementMode");
    }
    UObject* mesh = nullptr;
    UObject* anim = nullptr;
    ue::GetProp(pawn, "Mesh", mesh);
    if (IsLive(mesh)) ue::GetProp(mesh, "AnimScriptInstance", anim);
    auto last = reinterpret_cast<const FVector*>(reinterpret_cast<const uint8_t*>(pawn) + 0x39c);
    Log("research: net-walk %s %s at=(%.0f %.0f %.0f) vel=(%.0f %.0f %.0f) accel=(%.0f %.0f %.0f) desiredSpeed=%.1f "
        "smoothed=%.1f maxWalk=%.0f mode=%s rootMotionMode=%s playingRootMotion=%s netRootMotionMontage=%s "
        "meshUpdateFlag=%s consumeCalls=%llu lastInput=(%.2f %.2f %.2f) anim=%s",
        stage, Name(pawn).c_str(), at.x, at.y, at.z, velocity.x, velocity.y, velocity.z, acceleration.x,
        acceleration.y, acceleration.z, desired, smoothed, maxWalk, mode.c_str(),
        IsLive(anim) ? ByteProp(anim, "RootMotionMode").c_str() : "?", CallBool(pawn, "IsPlayingRootMotion").c_str(),
        CallBool(pawn, "IsPlayingNetworkedRootMotionMontage").c_str(),
        IsLive(mesh) ? ByteProp(mesh, "MeshComponentUpdateFlag").c_str() : "?",
        static_cast<unsigned long long>(ConsumeCalls(movement)), last->x, last->y, last->z, Describe(anim).c_str());
}

} // namespace

bool NetInput(std::string& summary) {
    if (!g_inputVtable) {
        auto pc = LocalController();
        if (!pc) {
            summary = "no local controller";
            return false;
        }
        auto vtable = pc->vtable;
        auto key = reinterpret_cast<uintptr_t>(vtable[kInputKeySlot]);
        auto axis = reinterpret_cast<uintptr_t>(vtable[kInputAxisSlot]);
        if (key != g.imageBase + kInputKeyRva || axis != g.imageBase + kInputAxisRva) {
            char text[160];
            std::snprintf(text, sizeof text, "unexpected input slots (key exe+0x%llx, axis exe+0x%llx); nothing patched",
                          static_cast<unsigned long long>(key - g.imageBase),
                          static_cast<unsigned long long>(axis - g.imageBase));
            summary = text;
            return false;
        }
        g_inputKeyOriginal = reinterpret_cast<InputKeyFn>(key);
        g_inputAxisOriginal = reinterpret_cast<InputAxisFn>(axis);
        if (!PatchSlot(vtable, kInputKeySlot, reinterpret_cast<void*>(&HookedInputKey)) ||
            !PatchSlot(vtable, kInputAxisSlot, reinterpret_cast<void*>(&HookedInputAxis))) {
            summary = "VirtualProtect on the controller vtable failed";
            return false;
        }
        if (reinterpret_cast<uintptr_t>(vtable[kAddYawSlot]) == g.imageBase + kAddYawRva) {
            g_addYawOriginal = reinterpret_cast<AddYawFn>(vtable[kAddYawSlot]);
            PatchSlot(vtable, kAddYawSlot, reinterpret_cast<void*>(&HookedAddYaw));
        }
        if (reinterpret_cast<uintptr_t>(vtable[kUpdateRotationSlot]) == g.imageBase + kUpdateRotationRva) {
            g_updateRotationOriginal = reinterpret_cast<UpdateRotationFn>(vtable[kUpdateRotationSlot]);
            PatchSlot(vtable, kUpdateRotationSlot, reinterpret_cast<void*>(&HookedUpdateRotation));
        }
        if (reinterpret_cast<uintptr_t>(vtable[kProcessInputSlot]) == g.imageBase + kProcessInputRva) {
            g_processInputOriginal = reinterpret_cast<ProcessInputFn>(vtable[kProcessInputSlot]);
            PatchSlot(vtable, kProcessInputSlot, reinterpret_cast<void*>(&HookedProcessInput));
        }
        UObject* pawn = nullptr;
        ue::GetProp(pc, "Pawn", pawn);
        if (IsLive(pawn)) {
            void* hooks[2] = {reinterpret_cast<void*>(&HookedLook<0>), reinterpret_cast<void*>(&HookedLook<1>)};
            for (int i = 0; i < 2; ++i)
                if (reinterpret_cast<uintptr_t>(pawn->vtable[kLookSlots[i]]) == g.imageBase + kLookRvas[i]) {
                    g_lookOriginal[i] = reinterpret_cast<LookFn>(pawn->vtable[kLookSlots[i]]);
                    PatchSlot(pawn->vtable, kLookSlots[i], hooks[i]);
                }
        }
        for (auto& c : g_inputCounts)
            if (IsLive(c.pc)) HookCameraInput(c.pc);
        HookCameraInput(pc);
        g_inputVtable = vtable;
        summary = "InputKey/InputAxis wrapped on " + Name(pc->cls) + "; send input, then run net-input again";
        return true;
    }
    int listed = 0;
    for (auto& c : g_inputCounts) {
        if (!c.pc) continue;
        ++listed;
        FRotator control{};
        if (IsLive(c.pc)) ue::GetProp(c.pc, "ControlRotation", control);
        auto bytes = reinterpret_cast<const uint8_t*>(c.pc);
        auto view = reinterpret_cast<const float*>(bytes + 0x1fe4);
        auto input = reinterpret_cast<const float*>(bytes + 0x530);
        FRotator camera{};
        UObject* manager = nullptr;
        if (IsLive(c.pc)) ue::GetProp(c.pc, "PlayerCameraManager", manager);
        if (IsLive(manager))
            if (Call get(manager, "GetCameraRotation"); get.invoke()) camera = get.get<FRotator>("ReturnValue");
        Log("research: input %s keys=%llu axes(nonzero)=%llu updateRotation=%llu control=(%.1f %.1f %.1f) "
            "camera=(%.1f %.1f %.1f) view+0x1fe4=(%.1f %.1f %.1f) rotationInput+0x530=(%.2f %.2f %.2f) "
            "processPlayerInput=%llu (dirty at entry %llu) inputYawScale=%.3f inputPitchScale=%.3f addYaw=%.3f "
            "rotationInputYaw=%.3f rotationDelta=%.3f first presses:%s",
            Describe(c.pc).c_str(), static_cast<unsigned long long>(c.keys), static_cast<unsigned long long>(c.axes),
            static_cast<unsigned long long>(c.rotations), control.pitch, control.yaw, control.roll, camera.pitch,
            camera.yaw, camera.roll, view[0], view[1], view[2], input[0], input[1], input[2],
            static_cast<unsigned long long>(c.processes), static_cast<unsigned long long>(c.dirtyAtProcess),
            *reinterpret_cast<const float*>(bytes + 0x53c), *reinterpret_cast<const float*>(bytes + 0x540), c.addYaw,
            c.rotationInputYaw, c.rotationDelta, c.firstKeys.c_str());
        c.addYaw = c.rotationInputYaw = c.rotationDelta = 0;
        c.keys = c.axes = c.rotations = c.processes = c.dirtyAtProcess = 0;
        c.firstKeys.clear();
        // BendCamManager state used by UpdateRotation (0x45d610): camera stack (+0x35d0/+0x35d8), the fixed
        // cameras at +0x3768..+0x3780 (active when +0x34 bit 0), and the flags at +0x35b0.
        if (IsLive(manager)) {
            auto cm = reinterpret_cast<const uint8_t*>(manager);
            auto name = [](const void* p) {
                auto o = static_cast<UObject*>(const_cast<void*>(p));
                return IsLive(o) ? Name(o->cls) : std::string(p ? "?" : "null");
            };
            std::string cams;
            auto stack = *reinterpret_cast<void* const*>(cm + 0x35d0);
            auto count = *reinterpret_cast<const int32_t*>(cm + 0x35d8);
            for (int32_t i = 0; i < count && i < 6 && ReadableMemory(stack, (i + 1) * sizeof(void*)); ++i)
                cams += " " + name(static_cast<void* const*>(stack)[i]) + "@" +
                        std::to_string(reinterpret_cast<uintptr_t>(static_cast<void* const*>(stack)[i]));
            std::string fixed;
            for (int off = 0x3768; off <= 0x3780; off += 8) {
                auto cam = *reinterpret_cast<const uint8_t* const*>(cm + off);
                fixed += " " + name(cam);
                if (cam && ReadableMemory(cam + 0x34, 1)) fixed += (cam[0x34] & 1) ? "(on)" : "(off)";
            }
            Log("research: camera %s of %s flags+0x35b0=0x%02x stack(%d):%s fixed:%s",
                Name(manager->cls).c_str(), Name(c.pc).c_str(), cm[0x35b0], count, cams.c_str(), fixed.c_str());
        }
    }
    for (auto& c : g_lookCounts) {
        if (!c.pawn) continue;
        auto bits = *reinterpret_cast<const uint32_t*>(reinterpret_cast<const uint8_t*>(c.pawn) + 0x350);
        Log("research: look %s look0 calls=%llu nonzero=%llu sum=%.3f look1 calls=%llu nonzero=%llu sum=%.3f "
            "bInputEnabled(+0x350 bit4)=%u",
            Describe(c.pawn).c_str(), static_cast<unsigned long long>(c.calls[0]),
            static_cast<unsigned long long>(c.nonzero[0]), c.sum[0], static_cast<unsigned long long>(c.calls[1]),
            static_cast<unsigned long long>(c.nonzero[1]), c.sum[1], (bits >> 4) & 1);
        c.calls[0] = c.calls[1] = c.nonzero[0] = c.nonzero[1] = 0;
        c.sum[0] = c.sum[1] = 0;
    }
    for (auto& c : g_cameraInputs) {
        if (!c.camera) continue;
        Log("research: camera input %llu calls=%llu sum=(%.3f %.3f)", static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(c.camera)), static_cast<unsigned long long>(c.calls),
            c.sum[0], c.sum[1]);
        c.calls = 0;
        c.sum[0] = c.sum[1] = 0;
    }
    // ProcessPlayerInput's static input stack (TArray<TWeakObjectPtr<UInputComponent>> at 0x4d36680), bottom first.
    struct WeakPtr {
        int32_t index, serial;
    };
    struct StackArray {
        WeakPtr* data;
        int32_t num, max;
    };
    auto stack = reinterpret_cast<const StackArray*>(g.imageBase + 0x4d36680);
    Log("research: input stack has %d entries", stack->num);
    for (int32_t i = 0; i < stack->num && i < 16; ++i) {
        auto component = ObjectAt(stack->data[i].index);
        Log("research: input stack[%d] %s outer=%s", i, Describe(component).c_str(),
            IsLive(component) ? Describe(component->outer).c_str() : "?");
    }
    for (auto& c : g_inputCounts) {
        if (!IsLive(c.pc)) continue;
        auto bytes = reinterpret_cast<const uint8_t*>(c.pc);
        Log("research: input %s dirty(+0x5d8)=%u currentInputStack(+0x5d0)=%d", Name(c.pc).c_str(), bytes[0x5d8],
            *reinterpret_cast<const int32_t*>(bytes + 0x5d0));
    }
    summary = std::to_string(listed) + " controllers received input since the last net-input (counters reset)";
    return true;
}

bool NetWalk(bool remote, bool consume, std::string& summary) {
    auto pc = LocalController();
    if (remote) {
        // Host side: walk the pawn of a controller that belongs to a remote connection.
        UObject* found = nullptr;
        auto pcClass = FindClass("PlayerController");
        for (int32_t i = 0, n = ObjectCount(); pcClass && !found && i < n; ++i) {
            auto obj = ObjectAt(i);
            if (IsLive(obj) && IsLive(obj->cls) && !IsDefaultOrArchetype(obj) && IsA(obj, pcClass) && obj != pc &&
                IsLive(NetConnectionOf(obj)))
                found = obj;
        }
        pc = found;
    }
    UObject* pawn = nullptr;
    if (pc) ue::GetProp(pc, "Pawn", pawn);
    if (!IsLive(pawn)) {
        summary = remote ? "no remote controller with a pawn" : "the local controller has no pawn";
        return false;
    }
    UObject* walkMovement = nullptr;
    ue::GetProp(pawn, "CharacterMovement", walkMovement);
    WrapConsume(walkMovement);
    g_walkConsume = consume;
    WalkReport(pawn, "start");
    g_walkNextReport = Seconds() + 0.5;
    g_walkFrom = {};
    Call where(pawn, "K2_GetActorLocation");
    if (where.invoke()) g_walkFrom = where.get<FVector>("ReturnValue");
    g_walkPawn = pawn;
    g_walkSteps = 0;
    g_walkUntil = Seconds() + 3.0;
    char text[200];
    std::snprintf(text, sizeof text, "walking %s forward for 3 s from (%.0f %.0f %.0f); progress is logged",
                  Name(pawn).c_str(), g_walkFrom.x, g_walkFrom.y, g_walkFrom.z);
    summary = text;
    return true;
}

void WalkStep(UObject* obj, UObject* fn) {
    // One-shot caller stacks for the client movement RPCs locate Bend's CMC tick path (ReplicateMoveToServer).
    // Gated on the engine net session so the name lookups never run during early startup.
    if (g_engineOwnsP2P && fn) {
        static int serverMove = FindNameIndex("ServerMove");
        static int serverMoveDual = FindNameIndex("ServerMoveDual");
        static bool loggedMove = false, loggedDual = false;
        bool isMove = serverMove > 0 && fn->name.index == serverMove;
        bool isDual = serverMoveDual > 0 && fn->name.index == serverMoveDual;
        if ((isMove && !loggedMove) || (isDual && !loggedDual)) {
            (isMove ? loggedMove : loggedDual) = true;
            Log("research: first %s on %s (vtable exe+0x%llx) stack:%s", Name(fn).c_str(), Describe(obj).c_str(),
                static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(obj->vtable) - g.imageBase),
                ExeStack().c_str());
        }
    }
    if (!g_walkPawn || obj != g_walkPawn) return;
    static int receiveTick = FindNameIndex("ReceiveTick");
    if (!fn || fn->name.index != receiveTick) return;
    if (!IsLive(g_walkPawn)) {
        g_walkPawn = nullptr;
        return;
    }
    if (Seconds() >= g_walkUntil) {
        FVector to{};
        Call where(g_walkPawn, "K2_GetActorLocation");
        if (where.invoke()) to = where.get<FVector>("ReturnValue");
        Log("research: net-walk: %llu steps on %s; location (%.0f %.0f %.0f) -> (%.0f %.0f %.0f) Role=%s",
            static_cast<unsigned long long>(g_walkSteps), Name(g_walkPawn).c_str(), g_walkFrom.x, g_walkFrom.y,
            g_walkFrom.z, to.x, to.y, to.z, ByteProp(g_walkPawn, "Role").c_str());
        WalkReport(g_walkPawn, "end");
        g_walkPawn = nullptr;
        return;
    }
    if (Seconds() >= g_walkNextReport) {
        g_walkNextReport = Seconds() + 0.5;
        WalkReport(g_walkPawn, "step");
    }
    Call forward(g_walkPawn, "GetActorForwardVector");
    if (!forward.invoke()) return;
    // APawn::ControlInputVector sits at +0x390 on this build (APawn::AddMovementInput 0x2a52020).
    auto control = reinterpret_cast<const FVector*>(reinterpret_cast<const uint8_t*>(g_walkPawn) + 0x390);
    FVector before = *control;
    if (Call(g_walkPawn, "AddMovementInput")
            .set("WorldDirection", forward.get<FVector>("ReturnValue"))
            .set("ScaleValue", 1.f)
            .set("bForce", true)
            .invoke())
        ++g_walkSteps;
    // WalkConsume=1: run APawn::ConsumeMovementInputVector ourselves so Bend's CMC tick sees LastControlInputVector.
    if (g_walkConsume) Call(g_walkPawn, "ConsumeMovementInputVector").invoke();
    if (g_walkSteps == 1 || g_walkSteps == 40) {
        UObject* movement = nullptr;
        UObject* fromCall = nullptr;
        ue::GetProp(g_walkPawn, "CharacterMovement", movement);
        if (Call get(g_walkPawn, "GetMovementComponent"); get.invoke()) fromCall = get.get<UObject*>("ReturnValue");
        UObject* pawnOwner = nullptr;
        UObject* characterOwner = nullptr;
        UObject* updated = nullptr;
        uintptr_t addInput = 0, tickComponent = 0;
        if (IsLive(movement)) {
            ue::GetProp(movement, "PawnOwner", pawnOwner);
            ue::GetProp(movement, "CharacterOwner", characterOwner);
            ue::GetProp(movement, "UpdatedComponent", updated);
            addInput = reinterpret_cast<uintptr_t>(movement->vtable[0x528 / 8]) - g.imageBase;
            tickComponent = reinterpret_cast<uintptr_t>(movement->vtable[0x1e8 / 8]) - g.imageBase;
        }
        Log("research: net-walk step %llu %s ControlInputVector (%.2f %.2f %.2f) -> (%.2f %.2f %.2f) "
            "GetMovementComponent=%s CharacterMovement=%s PawnOwner=%s CharacterOwner=%s UpdatedComponent=%s "
            "AddInputVector=exe+0x%llx slot61=exe+0x%llx IsMoveInputIgnored=%s",
            static_cast<unsigned long long>(g_walkSteps), Name(g_walkPawn).c_str(), before.x, before.y, before.z,
            control->x, control->y, control->z, Describe(fromCall).c_str(), Describe(movement).c_str(),
            Describe(pawnOwner).c_str(), Describe(characterOwner).c_str(), Describe(updated).c_str(),
            static_cast<unsigned long long>(addInput), static_cast<unsigned long long>(tickComponent),
            CallBool(g_walkPawn, "IsMoveInputIgnored").c_str());
    }
}

bool NetConnect(const std::string& host, std::string& summary) {
    std::string patched;
    if (!PatchNetDriverDefinitions(patched)) {
        summary = patched;
        return false;
    }
    Log("research: %s", patched.c_str());
    if (!InstallLoadMapHook(summary)) return false;
    if (!InstallHandleClientPlayerHook(summary)) return false;
    if (g_pawnReplication) RestoreCharacterReplication();
    g_engineOwnsP2P = true;
    std::wstring command = L"open ";
    for (auto c : host) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == ':')) {
            summary = "ConnectUrl contains characters that are not allowed in a console command";
            return false;
        }
        command += static_cast<wchar_t>(c);
    }
    return NetConsole(command, summary);
}

void TraceStart() {
    g_trace.clear();
    g_traceStart = Seconds();
    g_tracing = true;
    Log("research: ProcessEvent trace started");
}

bool Tracing() { return g_tracing; }

void WatchJoinedController() {
    if (!g_watchedController) return;
    if (!IsLive(g_watchedController)) {
        Log("research: watch +%.3fs joined controller is gone", Seconds() - g_traceStart);
        g_watchedController = nullptr;
        return;
    }
    UObject* playerState = nullptr;
    UObject* pawn = nullptr;
    ue::GetProp(g_watchedController, "PlayerState", playerState);
    ue::GetProp(g_watchedController, "Pawn", pawn);
    auto describe = [](UObject* v) { return v ? (IsLive(v) ? FullName(v) : std::string("<stale>")) : std::string("null"); };
    if (playerState != g_watchedPlayerState)
        Log("research: watch +%.3fs %s.PlayerState %s -> %s", Seconds() - g_traceStart, Name(g_watchedController).c_str(),
            describe(g_watchedPlayerState).c_str(), describe(playerState).c_str());
    if (pawn != g_watchedPawn)
        Log("research: watch +%.3fs %s.Pawn %s -> %s Role=%s RemoteRole=%s", Seconds() - g_traceStart,
            Name(g_watchedController).c_str(), describe(g_watchedPawn).c_str(), describe(pawn).c_str(),
            IsLive(pawn) ? ByteProp(pawn, "Role").c_str() : "-", IsLive(pawn) ? ByteProp(pawn, "RemoteRole").c_str() : "-");
    uint8_t role = 0;
    if (IsLive(pawn) && ue::GetProp(pawn, "Role", role) && pawn == g_watchedPawn && role != g_watchedPawnRole)
        Log("research: watch +%.3fs %s.Role %u -> %u", Seconds() - g_traceStart, Name(pawn).c_str(), g_watchedPawnRole, role);
    g_watchedPawnRole = role;
    g_watchedPlayerState = playerState;
    g_watchedPawn = pawn;
}

void TraceEvent(UObject* obj, UObject* fn, void* parms) {
    if (!g_tracing || !obj || !fn) return;
    WatchJoinedController();
    if (g_trace.size() >= 200000) return;
    auto [perObject, inserted] = g_tracePerObject.try_emplace(fn, false);
    if (inserted && IsLive(fn)) {
        auto name = Name(fn);
        perObject->second = name.starts_with("OnRep_") || name == "ReceiveBeginPlay" || name == "ClientRestart";
    }
    auto& entry = g_trace[{obj->cls, fn, perObject->second ? obj : nullptr}];
    if (entry.count++ == 0) {
        entry.object = IsLive(obj) ? FullName(obj) : "<stale>";
        static UObject* playerStateClass = FindClass("PlayerState");
        static UObject* controllerClass = FindClass("Controller");
        if (perObject->second && IsLive(obj) &&
            ((playerStateClass && IsA(obj, playerStateClass)) || (controllerClass && IsA(obj, controllerClass))))
            Log("research: trace +%.3fs first %s on %s", Seconds() - g_traceStart, Name(fn).c_str(), Name(obj).c_str());
    }
    if (entry.samples.size() >= 3 || !parms || !IsLive(fn)) return;
    std::string sample;
    int count = 0;
    for (auto p = Children(fn); p && count < 24; p = Next(p), ++count) {
        if (!IsLive(p) || !IsLive(p->cls)) break;
        if (count) sample += ", ";
        sample += Name(p) + "=" + Value(p, static_cast<const uint8_t*>(parms));
    }
    if (!sample.empty()) entry.samples.push_back(std::move(sample));
}

bool TraceStop(const std::wstring& path, std::string& summary) {
    if (!g_tracing) {
        summary = "no trace is running; send trace-on first";
        return false;
    }
    g_tracing = false;
    FILE* file = _wfopen(path.c_str(), L"w");
    if (!file) {
        summary = "could not open the trace file";
        return false;
    }
    std::vector<std::pair<std::string, const TraceEntry*>> rows;
    for (auto& [key, entry] : g_trace) {
        std::string name = (IsLive(key.cls) ? Name(key.cls) : std::string("?")) + "." +
                           (IsLive(key.fn) ? Name(key.fn) : std::string("?"));
        rows.emplace_back(std::move(name), &entry);
    }
    std::sort(rows.begin(), rows.end(), [](auto& a, auto& b) { return a.first < b.first; });
    std::fprintf(file, "# %.1f s, %d unique class.function pairs (OnRep_*, ReceiveBeginPlay, ClientRestart: per object)\n",
                 Seconds() - g_traceStart, static_cast<int>(rows.size()));
    for (auto& [name, entry] : rows) {
        std::fprintf(file, "%8llu %s  on %s\n", static_cast<unsigned long long>(entry->count), name.c_str(),
                     entry->object.c_str());
        for (auto& sample : entry->samples) std::fprintf(file, "           > %s\n", sample.c_str());
    }
    std::fclose(file);
    summary = std::to_string(rows.size()) + " unique calls traced";
    g_trace.clear();
    return true;
}

// ---- Mission lab ---------------------------------------------------------------------------------------

namespace {

struct ObjectArray { UObject** data; int32_t count, capacity; };

struct JobRow {
    bool done = false, active = false;
    uint8_t state = 0;
    int32_t objective = -1; // CurrentJobObjectiveIndex: mid-mission progress
    bool operator==(const JobRow&) const = default;
};

struct MissionSnapshot {
    bool valid = false;
    std::map<std::string, JobRow> rows; // "job:<name>" / "mission:<name>"
    std::string tracked;
    int episode = -1;
};

struct WatchKey {
    UObject* cls;
    UObject* fn;
    bool operator==(const WatchKey&) const = default;
};
struct WatchHash {
    size_t operator()(const WatchKey& k) const { return std::hash<void*>()(k.cls) ^ (std::hash<void*>()(k.fn) << 1); }
};

constexpr int kWatchCallsLogged = 25; // per class.function, then only counted
constexpr double kWatchSampleInterval = 0.5;

bool g_missionWatch = false;
double g_missionWatchStart = 0, g_missionSampledAt = -1e9;
MissionSnapshot g_missionLast;
std::unordered_map<WatchKey, uint64_t, WatchHash> g_missionCalls;
std::unordered_map<int32_t, bool> g_missionRelevant;

bool IsMissionToken(int32_t nameIndex) {
    auto [it, inserted] = g_missionRelevant.try_emplace(nameIndex, false);
    if (!inserted) return it->second;
    std::string lower = NameString(nameIndex);
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    for (const char* token : {"job", "mission", "objective", "checkpoint", "quest", "story"})
        if (lower.find(token) != std::string::npos) return it->second = true;
    return false;
}

// Pure queries are polled by the game every frame and say nothing about transitions.
bool IsQueryName(const std::string& name) {
    for (const char* prefix : {"Get", "Is", "Can", "Has", "Should", "ReceiveTick", "Tick"})
        if (name.starts_with(prefix)) return true;
    return false;
}

std::string Parameters(UObject* fn, const void* parms) {
    std::string text;
    int count = 0;
    // Blueprint functions list locals after the parameters; the caller's buffer only holds ParmsSize bytes.
    int size = *reinterpret_cast<uint16_t*>(reinterpret_cast<uint8_t*>(fn) + off::FunctionParmsSize);
    for (auto p = Children(fn); p && count < 16; p = Next(p), ++count) {
        if (!IsLive(p) || !IsLive(p->cls)) break;
        int offset = PropertyOffset(p);
        if (offset < 0 || offset + ElementSize(p) > size) break;
        if (Name(p) == "ReturnValue") continue;
        if (!text.empty()) text += ", ";
        text += Name(p) + "=" + (parms ? Value(p, static_cast<const uint8_t*>(parms)) : std::string("?"));
        if (text.size() > 600) {
            text += " ...";
            break;
        }
    }
    return text;
}

bool CallBoolWithObject(UObject* target, const char* function, const char* param, UObject* value, bool& out) {
    Call call(target, function);
    call.set(param, value);
    if (!call.invoke()) return false;
    out = call.get<bool>("ReturnValue");
    return true;
}

MissionSnapshot ReadMissionSnapshot() {
    MissionSnapshot snapshot;
    auto jobs = FindInstance("BendJobManager");
    auto mission = FindInstance("BendGameMission");
    if (!IsLive(jobs)) return snapshot;
    ObjectArray refs{};
    if (!ue::GetProp(jobs, "JobRefsInOrder", refs) || refs.count < 0 || refs.count > 4096 || (refs.count && !refs.data))
        return snapshot;
    for (int32_t i = 0; i < refs.count; ++i) {
        UObject* job = refs.data[i];
        if (!IsLive(job)) continue;
        JobRow row;
        CallBoolWithObject(jobs, "GetIsJobComplete", "JobID", job, row.done);
        CallBoolWithObject(jobs, "GetIsJobActive", "JobID", job, row.active);
        ue::GetProp(job, "JobCurrentState", row.state);
        ue::GetProp(job, "CurrentJobObjectiveIndex", row.objective);
        snapshot.rows["job:" + Name(job)] = row;
    }
    UObject* tracked = nullptr;
    if (ue::GetProp(jobs, "CurrentTrackedJob", tracked) && IsLive(tracked)) snapshot.tracked = Name(tracked);
    if (IsLive(mission)) {
        Call episode(mission, "GetCurrentEpisode");
        if (episode.invoke()) snapshot.episode = episode.get<uint8_t>("ReturnValue");
        Call datas(mission, "GetAllActiveOrUnlockedMissionDatas");
        if (datas.invoke()) {
            auto list = datas.get<ObjectArray>("ReturnValue");
            for (int32_t i = 0; list.data && i < list.count && list.count < 4096; ++i) {
                UObject* data = list.data[i];
                if (!IsLive(data)) continue;
                JobRow row;
                row.active = true;
                CallBoolWithObject(mission, "GetIsMissionCompleted", "MissionData", data, row.done);
                snapshot.rows["mission:" + Name(data)] = row;
            }
        }
    }
    snapshot.valid = true;
    return snapshot;
}

std::string RowText(const JobRow& row) {
    char text[64];
    std::snprintf(text, sizeof text, "done=%d active=%d state=%u objective=%d", row.done, row.active, row.state,
                  row.objective);
    return text;
}

int LogMissionDiff(const MissionSnapshot& before, const MissionSnapshot& after, const char* label, double at) {
    int changes = 0;
    for (auto& [name, row] : after.rows) {
        auto old = before.rows.find(name);
        if (old == before.rows.end()) {
            Log("mission %s +%.2fs %s appeared (%s)", label, at, name.c_str(), RowText(row).c_str());
            ++changes;
        } else if (!(old->second == row)) {
            Log("mission %s +%.2fs %s %s -> %s", label, at, name.c_str(), RowText(old->second).c_str(), RowText(row).c_str());
            ++changes;
        }
    }
    for (auto& [name, row] : before.rows)
        if (!after.rows.contains(name)) {
            Log("mission %s +%.2fs %s disappeared (was %s)", label, at, name.c_str(), RowText(row).c_str());
            ++changes;
        }
    if (before.tracked != after.tracked) {
        Log("mission %s +%.2fs tracked job '%s' -> '%s'", label, at, before.tracked.c_str(), after.tracked.c_str());
        ++changes;
    }
    if (before.episode != after.episode) {
        Log("mission %s +%.2fs episode %d -> %d", label, at, before.episode, after.episode);
        ++changes;
    }
    return changes;
}

void DumpFunctions(FILE* file, UObject* cls, std::unordered_set<UObject*>& seen) {
    for (auto c = cls; IsLive(c); c = Super(c)) {
        if (!seen.insert(c).second) return;
        auto name = Name(c);
        if (name == "Object" || name == "Actor" || name == "ActorComponent" || name == "Info") return;
        std::fprintf(file, "  class %s\n", FullName(c).c_str());
        int budget = 0;
        for (auto field = Children(c); field && budget < 4096; field = Next(field), ++budget) {
            if (!IsLive(field) || !IsLive(field->cls)) break;
            if (IsFunction(field)) std::fprintf(file, "    FN %s%s\n", Name(field).c_str(), Signature(field).c_str());
        }
    }
}

void DumpPropertyValues(FILE* file, UObject* obj) {
    int budget = 0;
    for (auto c = obj->cls; IsLive(c) && budget < 512; c = Super(c)) {
        auto name = Name(c);
        if (name == "Object" || name == "Actor") break;
        for (auto field = Children(c); field && budget < 512; field = Next(field), ++budget) {
            if (!IsLive(field) || !IsLive(field->cls) || IsFunction(field)) continue;
            std::fprintf(file, "      %s %s = %s\n", Name(field->cls).c_str(), Name(field).c_str(),
                         Value(field, reinterpret_cast<const uint8_t*>(obj)).c_str());
        }
    }
}

UObject* PropertySubclassObject(UObject* property, const char* expectedClass) {
    constexpr int kPropertySubclassOffset = 0x78;
    if (!IsLive(property) ||
        !ReadableMemory(reinterpret_cast<uint8_t*>(property) + kPropertySubclassOffset, sizeof(UObject*))) return nullptr;
    UObject* value = nullptr;
    std::memcpy(&value, reinterpret_cast<uint8_t*>(property) + kPropertySubclassOffset, sizeof value);
    return IsLive(value) && IsLive(value->cls) && Name(value->cls) == expectedClass ? value : nullptr;
}

struct FTextRaw { uint8_t bytes[24]; };

// Diagnostics only: the engine string returned by Conv_TextToString is leaked. Not for the ProcessEvent hook.
std::string TextValue(const uint8_t* at) {
    static UObject* library = nullptr;
    if (!IsLive(library)) library = FindDefaultObject("KismetTextLibrary");
    FTextRaw text;
    void* shared = nullptr;
    if (!library || !ReadableMemory(at, sizeof text)) return {};
    std::memcpy(&text, at, sizeof text);
    std::memcpy(&shared, text.bytes, sizeof shared);
    if (!shared) return {};
    Call call(library, "Conv_TextToString");
    if (!call.set("InText", text).invoke()) return {};
    struct { const wchar_t* data; int32_t num, max; } value{};
    value = call.get<decltype(value)>("ReturnValue");
    if (!value.data || value.num <= 1 || value.num > 4096 || !ReadableMemory(value.data, value.num * sizeof(wchar_t)))
        return {};
    int length = value.num - 1;
    int n = WideCharToMultiByte(CP_UTF8, 0, value.data, length, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(std::max(n, 0)), '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, value.data, length, out.data(), n, nullptr, nullptr);
    for (auto& c : out)
        if (c == '\n' || c == '\r') c = ' ';
    return out;
}

// Value() plus localized text, for report files written outside the ProcessEvent hook.
std::string DisplayValue(UObject* property, const uint8_t* data) {
    int offset = PropertyOffset(property), size = ElementSize(property);
    if (Name(property->cls) == "TextProperty" && offset >= 0 && size == sizeof(FTextRaw))
        return "\"" + TextValue(data + offset) + "\"";
    return Value(property, data);
}

std::string FieldValue(UObject* obj, UObject* structure, const uint8_t* data, const char* name) {
    auto property = FindChild(structure ? structure : obj->cls, name);
    return IsLive(property) ? DisplayValue(property, data ? data : reinterpret_cast<const uint8_t*>(obj)) : "?";
}

// Names of an object or byte (enum ID) array property, or an empty string.
std::string ArrayValue(UObject* obj, const char* name) {
    auto property = FindChild(obj->cls, name);
    if (!IsLive(property) || Name(property->cls) != "ArrayProperty") return {};
    auto objects = PropertySubclassObject(property, "ObjectProperty");
    auto bytes = objects ? nullptr : PropertySubclassObject(property, "ByteProperty");
    struct { uint8_t* data; int32_t count, capacity; } values{};
    auto at = reinterpret_cast<uint8_t*>(obj) + PropertyOffset(property);
    if ((!objects && !bytes) || !ReadableMemory(at, sizeof values)) return {};
    std::memcpy(&values, at, sizeof values);
    size_t stride = objects ? sizeof(UObject*) : 1;
    if (values.count <= 0 || values.count > 64 || !values.data || !ReadableMemory(values.data, values.count * stride))
        return {};
    std::string out;
    for (int32_t i = 0; i < values.count; ++i) {
        if (!out.empty()) out += ',';
        if (objects) {
            UObject* item = reinterpret_cast<UObject**>(values.data)[i];
            out += IsLive(item) ? Name(item) : "?";
        } else {
            out += std::to_string(values.data[i]);
        }
    }
    return out;
}

struct ObjectiveArrayView {
    uint8_t* data = nullptr;
    int32_t count = 0;
    int stride = 0;
    UObject* structure = nullptr;
};

bool ReadObjectiveArray(UObject* job, const char* name, ObjectiveArrayView& out, std::string& error) {
    auto property = IsLive(job) ? FindChild(job->cls, name) : nullptr;
    if (!IsLive(property) || Name(property->cls) != "ArrayProperty") {
        error = "array property not found";
        return false;
    }
    auto inner = PropertySubclassObject(property, "StructProperty");
    if (!IsLive(inner)) {
        error = "array inner struct property not found";
        return false;
    }
    auto structure = PropertySubclassObject(inner, "ScriptStruct");
    if (!IsLive(structure)) structure = PropertySubclassObject(inner, "UserDefinedStruct");
    if (!IsLive(structure)) {
        error = "struct metadata not found";
        return false;
    }
    int offset = PropertyOffset(property), propertySize = ElementSize(property);
    struct Array { uint8_t* data; int32_t count, capacity; } values{};
    if (offset < 0 || propertySize != sizeof values ||
        !ReadableMemory(reinterpret_cast<uint8_t*>(job) + offset, sizeof values)) {
        error = "invalid array bounds";
        return false;
    }
    std::memcpy(&values, reinterpret_cast<uint8_t*>(job) + offset, sizeof values);
    int stride = ElementSize(inner);
    if (values.count < 0 || values.count > 256 || values.capacity < values.count ||
        stride <= 0 || stride > 1024 ||
        (values.count && (!values.data ||
         static_cast<size_t>(values.count) > SIZE_MAX / static_cast<size_t>(stride) ||
         !ReadableMemory(values.data, static_cast<size_t>(values.count) * stride)))) {
        error = "invalid array storage";
        return false;
    }
    out = {values.data, values.count, stride, structure};
    return true;
}

void DumpObjectiveList(FILE* file, UObject* job, const char* name) {
    ObjectiveArrayView values;
    std::string error;
    if (!ReadObjectiveArray(job, name, values, error)) {
        std::fprintf(file, "      OBJECTIVES %s unavailable: %s\n", name, error.c_str());
        return;
    }
    std::fprintf(file, "      OBJECTIVES %s count=%d struct=%s stride=%d\n", name, values.count,
                 Name(values.structure).c_str(), values.stride);
    for (int32_t i = 0; i < values.count; ++i) {
        const auto* element = values.data + static_cast<size_t>(i) * values.stride;
        std::fprintf(file, "        [%d]", i);
        int budget = 0;
        for (auto c = values.structure; IsLive(c) && budget < 128; c = Super(c)) {
            for (auto field = Children(c); field && budget < 128; field = Next(field), ++budget) {
                if (!IsLive(field) || !IsLive(field->cls) || IsFunction(field)) continue;
                int fieldOffset = PropertyOffset(field), fieldSize = ElementSize(field);
                if (fieldOffset < 0 || fieldSize <= 0 || fieldOffset > values.stride ||
                    fieldSize > values.stride - fieldOffset) continue;
                std::fprintf(file, " %s=%s", Name(field).c_str(), DisplayValue(field, element).c_str());
            }
        }
        std::fputc('\n', file);
    }
}

UObject* FindActiveJobScript(const std::string& jobName) {
    auto jobs = FindInstance("BendJobManager");
    ObjectArray refs{};
    if (!IsLive(jobs) || !ue::GetProp(jobs, "ActiveJobRefsInOrder", refs) ||
        refs.count < 0 || refs.count > 256 ||
        (refs.count && (!refs.data || !ReadableMemory(refs.data, static_cast<size_t>(refs.count) * sizeof(UObject*)))))
        return nullptr;
    for (int32_t i = 0; i < refs.count; ++i) {
        auto job = refs.data[i];
        if (!IsLive(job) || Name(job) != jobName) continue;
        UObject* script = nullptr;
        return ue::GetProp(job, "JobScriptReference", script) && IsLive(script) ? script : nullptr;
    }
    return nullptr;
}

bool ReadObjectiveState(UObject* script, const std::string& identifier, uint8_t& state) {
    int32_t index = FindNameIndex(identifier.c_str());
    if (!IsLive(script) || index < 0) return false;
    FName name{index, 0};
    Call get(script, "GetObjectiveState");
    get.set("ObjIdentifier", name);
    if (!get.invoke() || !get.has("CurrentState")) return false;
    state = get.get<uint8_t>("CurrentState");
    return state <= 2;
}

bool ReadJobObjectiveProgress(UObject* job, std::vector<ObjectiveProgress>& result) {
    result.clear();
    ObjectiveArrayView objectives;
    std::string error;
    if (!ReadObjectiveArray(job, "CurrentJobObjectiveList", objectives, error)) return false;
    auto identifier = FindChild(objectives.structure, "ObjectiveIdentifier");
    auto state = FindChild(objectives.structure, "ObjectiveState");
    if (!IsLive(identifier) || !IsLive(state) || !IsLive(identifier->cls) || !IsLive(state->cls) ||
        Name(identifier->cls) != "NameProperty" ||
        Name(state->cls) != "ByteProperty" || ElementSize(identifier) != sizeof(FName) || ElementSize(state) != 1)
        return false;
    int identifierOffset = PropertyOffset(identifier), stateOffset = PropertyOffset(state);
    if (identifierOffset < 0 || identifierOffset > objectives.stride - static_cast<int>(sizeof(FName)) ||
        stateOffset < 0 || stateOffset >= objectives.stride) return false;
    std::unordered_map<std::string, size_t> occurrences;
    for (int32_t index = 0; index < objectives.count; ++index) {
        const auto* element = objectives.data + static_cast<size_t>(index) * objectives.stride;
        FName name{};
        std::memcpy(&name, element + identifierOffset, sizeof name);
        if (name.number != 0) continue;
        std::string value = NameString(name.index);
        uint8_t currentState = 0;
        std::memcpy(&currentState, element + stateOffset, sizeof currentState);
        result.push_back({Name(job), value, static_cast<uint16_t>(index), currentState, false});
        ++occurrences[value];
    }
    for (auto& objective : result)
        objective.unique = occurrences[objective.identifier] == 1;
    return true;
}

std::vector<std::string> Split(const std::string& text) {
    std::vector<std::string> parts;
    size_t at = 0;
    while (at < text.size()) {
        while (at < text.size() && text[at] == ' ') ++at;
        size_t end = text.find(' ', at);
        if (end == std::string::npos) end = text.size();
        if (end > at) parts.push_back(text.substr(at, end - at));
        at = end;
    }
    return parts;
}

UObject* FindNamedInstance(const std::string& name) {
    int32_t index = FindNameIndex(name.c_str());
    if (index < 0) return nullptr;
    for (int32_t i = 0, n = ObjectCount(); i < n; ++i) {
        auto obj = ObjectAt(i);
        if (obj && obj->name.index == index && obj->name.number == 0 && IsLive(obj) && !IsDefaultOrArchetype(obj))
            return obj;
    }
    return nullptr;
}

} // namespace

bool ReadActiveObjectiveProgress(std::vector<ObjectiveProgress>& result) {
    result.clear();
    auto jobs = FindInstance("BendJobManager");
    ObjectArray refs{};
    if (!IsLive(jobs) || !ue::GetProp(jobs, "ActiveJobRefsInOrder", refs) ||
        refs.count < 0 || refs.count > 256 ||
        (refs.count && (!refs.data || !ReadableMemory(refs.data, static_cast<size_t>(refs.count) * sizeof(UObject*))))
        )
        return false;
    for (int32_t i = 0; i < refs.count; ++i) {
        auto job = refs.data[i];
        if (!IsLive(job)) continue;
        std::vector<ObjectiveProgress> objectives;
        if (!ReadJobObjectiveProgress(job, objectives)) return false;
        result.insert(result.end(), objectives.begin(), objectives.end());
    }
    return true;
}

std::vector<ObjectiveProgress> CompletedObjectiveProgress() {
    std::vector<ObjectiveProgress> result;
    auto jobs = FindInstance("BendJobManager");
    ObjectArray refs{};
    if (!IsLive(jobs) || !ue::GetProp(jobs, "ActiveJobRefsInOrder", refs) ||
        refs.count < 0 || refs.count > 256 ||
        (refs.count && (!refs.data || !ReadableMemory(refs.data, static_cast<size_t>(refs.count) * sizeof(UObject*)))))
        return result;
    for (int32_t i = 0; i < refs.count && result.size() < 128; ++i) {
        auto job = refs.data[i];
        if (!IsLive(job)) continue;
        UObject* script = nullptr;
        if (!ue::GetProp(job, "JobScriptReference", script) || !IsLive(script)) continue;
        std::vector<ObjectiveProgress> objectives;
        if (!ReadJobObjectiveProgress(job, objectives)) continue;
        for (const auto& objective : objectives) {
            if (objective.state == 2 && objective.unique && StoryNameIsValid(objective.identifier)) {
                result.push_back({objective.jobName, objective.identifier});
                if (result.size() == 128) break;
            }
        }
    }
    return result;
}

bool ApplyObjectiveProgress(const std::string& jobName, const std::string& identifier, bool& changed) {
    changed = false;
    auto script = FindActiveJobScript(jobName);
    uint8_t state = 0;
    if (!IsLive(script) || !ReadObjectiveState(script, identifier, state)) return false;
    if (state == 2) return true;
    if (state > 1) return false;
    int32_t index = FindNameIndex(identifier.c_str());
    if (index < 0) return false;
    FName name{index, 0};
    Call update(script, "UpdateJobObjectiveStatusAndProgression");
    update.set("ObjIdentifier", name).set<uint8_t>("ObjState", 2);
    if (!update.invoke() || !ReadObjectiveState(script, identifier, state) || state != 2) return false;
    changed = true;
    return true;
}

void MissionWatch(bool on) {
    g_missionWatch = on;
    if (on) {
        g_missionCalls.clear();
        g_missionWatchStart = Seconds();
        g_missionSampledAt = -1e9;
        g_missionLast = ReadMissionSnapshot();
        Log("mission watch: started, %d jobs/missions, tracked '%s', episode %d", static_cast<int>(g_missionLast.rows.size()),
            g_missionLast.tracked.c_str(), g_missionLast.episode);
        return;
    }
    std::vector<std::pair<std::string, uint64_t>> rows;
    for (auto& [key, count] : g_missionCalls)
        rows.emplace_back((IsLive(key.cls) ? Name(key.cls) : std::string("?")) + "." +
                          (IsLive(key.fn) ? Name(key.fn) : std::string("?")), count);
    std::sort(rows.begin(), rows.end());
    Log("mission watch: stopped after %.1f s, %d distinct calls", Seconds() - g_missionWatchStart, static_cast<int>(rows.size()));
    for (auto& [name, count] : rows) Log("mission watch: %8llu %s", static_cast<unsigned long long>(count), name.c_str());
}

bool MissionWatching() { return g_missionWatch; }

void MissionWatchEvent(UObject* obj, UObject* fn, void* parms) {
    if (!g_missionWatch || !IsLive(obj) || !IsLive(obj->cls) || !IsLive(fn)) return;
    if (!IsMissionToken(obj->cls->name.index) && !IsMissionToken(fn->name.index)) return;
    auto name = Name(fn);
    if (IsQueryName(name)) return;
    auto count = ++g_missionCalls[{obj->cls, fn}];
    if (count > kWatchCallsLogged) return;
    Log("mission call +%.2fs %s.%s on %s (%s)%s", Seconds() - g_missionWatchStart, Name(obj->cls).c_str(), name.c_str(),
        Name(obj).c_str(), Parameters(fn, parms).c_str(), count == kWatchCallsLogged ? " [further calls counted only]" : "");
}

void MissionWatchTick(double) {
    if (!g_missionWatch) return;
    double now = Seconds();
    if (now - g_missionSampledAt < kWatchSampleInterval) return;
    g_missionSampledAt = now;
    auto snapshot = ReadMissionSnapshot();
    if (!snapshot.valid) return;
    if (g_missionLast.valid) LogMissionDiff(g_missionLast, snapshot, "state", now - g_missionWatchStart);
    g_missionLast = std::move(snapshot);
}

void DescribeMission(FILE* file, UObject* data, int lookupId) {
    std::fprintf(file, "mission %s lookup=%d id=%s act=%s state=%s category=%s storyline=%s storylines=[%s] name=%s\n",
                 Name(data).c_str(), lookupId, FieldValue(data, nullptr, nullptr, "MissionID").c_str(),
                 FieldValue(data, nullptr, nullptr, "MissionsAct").c_str(),
                 FieldValue(data, nullptr, nullptr, "CurrentMissionState").c_str(),
                 FieldValue(data, nullptr, nullptr, "MissionCategory").c_str(),
                 FieldValue(data, nullptr, nullptr, "PrimaryStoryLine").c_str(), ArrayValue(data, "StoryLines").c_str(),
                 FieldValue(data, nullptr, nullptr, "MissionNameText").c_str());
    std::fprintf(file, "    map=%s weather=%s lastInEpisode=%s newEpisode=%s chain=%s refuel=%s ambient=%s deprecated=%s\n",
                 FieldValue(data, nullptr, nullptr, "MissionMap").c_str(),
                 FieldValue(data, nullptr, nullptr, "WeatherMap").c_str(),
                 FieldValue(data, nullptr, nullptr, "IsLastMissionInEpisode").c_str(),
                 FieldValue(data, nullptr, nullptr, "NewEpisodeUnlocked").c_str(),
                 FieldValue(data, nullptr, nullptr, "MissionToChainTo").c_str(),
                 FieldValue(data, nullptr, nullptr, "CanRefuelRepairBikeDuringMission").c_str(),
                 FieldValue(data, nullptr, nullptr, "AllowAmbientEvents").c_str(),
                 FieldValue(data, nullptr, nullptr, "Deprecated").c_str());
    std::fprintf(file, "    prereqMissions=[%s] prereqMissionData=[%s] prereqJobs=[%s]\n",
                 ArrayValue(data, "PreReqMissions").c_str(), ArrayValue(data, "PreReqMissionsData").c_str(),
                 ArrayValue(data, "PreReqJobs").c_str());
    std::fprintf(file, "    description=%s\n    comments=%s\n",
                 FieldValue(data, nullptr, nullptr, "MissionDescriptionText").c_str(),
                 FieldValue(data, nullptr, nullptr, "DeveloperComments").c_str());
    DumpObjectiveList(file, data, "MissionObjectivesData");
}

// Display names, acts and objective texts for every job and story mission, to map internal codes
// (OBR1-MissionData, IND-JobData) to the names the game shows.
void DumpCatalog(FILE* file, UObject* jobs, UObject* mission, int& jobCount, int& missionCount) {
    std::fprintf(file, "\n# job catalog\n");
    ObjectArray refs{};
    if (ue::GetProp(jobs, "JobRefsInOrder", refs) && refs.data && refs.count > 0 && refs.count < 4096) {
        for (int32_t i = 0; i < refs.count; ++i) {
            UObject* job = refs.data[i];
            if (!IsLive(job)) continue;
            ++jobCount;
            auto stored = FindChild(job->cls, "StoredJobData");
            auto structure = IsLive(stored) ? PropertySubclassObject(stored, "ScriptStruct") : nullptr;
            const uint8_t* row = structure ? reinterpret_cast<const uint8_t*>(job) + PropertyOffset(stored) : nullptr;
            auto rowField = [&](const char* name) { return row ? FieldValue(job, structure, row, name) : std::string("?"); };
            std::fprintf(file, "job %s row=%s act=%s type=%s category=%s storyline=%s name=%s missions=[%s]\n",
                         Name(job).c_str(), FieldValue(job, nullptr, nullptr, "StoredRowName").c_str(),
                         rowField("JobsAct").c_str(), rowField("JobsType").c_str(), rowField("JobCategory").c_str(),
                         rowField("PrimaryStoryLine").c_str(), rowField("JobNameText").c_str(),
                         ArrayValue(job, "MissionDataRefs").c_str());
            DumpObjectiveList(file, job, "DefaultJobObjectiveList");
        }
    }
    std::fprintf(file, "\n# mission catalog\n");
    if (!IsLive(mission)) {
        std::fprintf(file, "no live BendGameMission\n");
        return;
    }
    std::fprintf(file, "active=%s tracked=%s\n", FieldValue(mission, nullptr, nullptr, "ActiveMissionData").c_str(),
                 FieldValue(mission, nullptr, nullptr, "CurrentTrackedMission").c_str());
    for (int act = 0; act < 10; ++act) {
        Call chapter(mission, "GetChapterNameFromEpisode");
        if (!chapter.set<uint8_t>("Act", static_cast<uint8_t>(act)).invoke()) break;
        auto text = chapter.get<FTextRaw>("ReturnValue");
        std::fprintf(file, "chapter %d \"%s\"\n", act, TextValue(text.bytes).c_str());
    }
    std::unordered_set<UObject*> listed;
    for (int id = 0; id < 256; ++id) {
        Call lookup(mission, "GetMissionDataFromIDNew");
        if (!lookup.set<uint8_t>("MissionID", static_cast<uint8_t>(id)).invoke()) break;
        auto data = lookup.get<UObject*>("ReturnValue");
        if (!IsLive(data) || !listed.insert(data).second) continue;
        ++missionCount;
        DescribeMission(file, data, id);
    }
    // Loaded mission data the ID lookup does not return (DLC, arcade, deprecated).
    auto missionClass = FindClass("BendMissionData");
    for (int32_t i = 0, n = ObjectCount(); missionClass && i < n; ++i) {
        auto obj = ObjectAt(i);
        if (!IsLive(obj) || IsDefaultOrArchetype(obj) || !IsA(obj, missionClass) || !listed.insert(obj).second) continue;
        ++missionCount;
        DescribeMission(file, obj, -1);
    }
}

std::string ActiveMissionProgress() {
    auto mission = FindInstance("BendGameMission");
    UObject* script = nullptr;
    if (!IsLive(mission) || !ue::GetProp(mission, "ActiveMissionScript", script) || !IsLive(script)) return {};
    std::string out;
    Call checkpoint(script, "I_GetCurrentCheckpointCount");
    if (checkpoint.invoke())
        out = "checkpoint " + std::to_string(checkpoint.get<int32_t>("Count")) + "/" +
              std::to_string(checkpoint.get<int32_t>("MaxCount"));
    Call all(script, "GetAllObjectives");
    if (!all.invoke()) return out;
    auto list = all.get<ObjectArray>("ReturnValue");
    if (list.count < 0 || list.count > 128 || (list.count && !list.data) ||
        (list.count && !ReadableMemory(list.data, static_cast<size_t>(list.count) * sizeof(UObject*))))
        return out;
    for (int32_t i = 0; i < list.count; ++i) {
        UObject* objective = list.data[i];
        FName id{};
        if (!IsLive(objective) || !ue::GetProp(objective, "ObjectiveIdentifierID", id)) continue;
        Call state(objective, "GetCurrentObjectiveState");
        auto name = NameString(id.index) + (id.number ? "_" + std::to_string(id.number - 1) : std::string());
        out += " " + name + "=" + (state.invoke() ? std::to_string(state.get<uint8_t>("ReturnValue")) : "?");
    }
    return out;
}

bool LevelProbe(const std::wstring& path, const std::string& filter, std::string& summary) {
    auto levelClass = FindClass("LevelStreaming");
    if (!levelClass) {
        summary = "LevelStreaming is not loaded";
        return false;
    }
    FILE* file = _wfopen(path.c_str(), L"w");
    if (!file) {
        summary = "could not open the level report";
        return false;
    }
    auto lower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    };
    std::string wanted = lower(filter);
    int total = 0, matched = 0;
    for (int32_t i = 0, n = ObjectCount(); i < n; ++i) {
        auto level = ObjectAt(i);
        if (!IsLive(level) || IsDefaultOrArchetype(level) || !IsA(level, levelClass)) continue;
        ++total;
        FName package{};
        ue::GetProp(level, "PackageNameToLoad", package);
        if (!package.index) ue::GetProp(level, "PackageName", package);
        std::string name = NameString(package.index);
        if (!wanted.empty() && lower(name).find(wanted) == std::string::npos) continue;
        ++matched;
        bool load = false, visible = false;
        ue::GetBoolProp(level, "bShouldBeLoaded", load);
        ue::GetBoolProp(level, "bShouldBeVisible", visible);
        Call loaded(level, "IsLevelLoaded"), shown(level, "IsLevelVisible");
        std::fprintf(file, "%s %s shouldLoad=%d shouldShow=%d loaded=%d visible=%d\n", Name(level).c_str(), name.c_str(),
                     load, visible, loaded.invoke() && loaded.get<bool>("ReturnValue"),
                     shown.invoke() && shown.get<bool>("ReturnValue"));
    }
    std::fclose(file);
    summary = std::to_string(matched) + " of " + std::to_string(total) + " streaming levels match '" + filter + "'";
    return true;
}

bool MinimapProbe(const std::wstring& path, std::string& summary) {
    auto componentClass = FindClass("MiniMapLocationComponent");
    if (!componentClass) {
        summary = "MiniMapLocationComponent is not loaded";
        return false;
    }
    FILE* file = _wfopen(path.c_str(), L"w");
    if (!file) {
        summary = "could not open the minimap report";
        return false;
    }
    std::unordered_set<UObject*> registered, onMap;
    auto collect = [](UObject* manager, const char* function, std::unordered_set<UObject*>& out) {
        Call list(manager, function);
        if (!IsLive(manager) || !list.invoke()) return;
        auto items = list.get<ObjectArray>("OutLocations");
        for (int32_t i = 0; items.data && i < items.count && items.count < 65536; ++i)
            if (ReadableMemory(items.data + i, sizeof(UObject*)) && IsLive(items.data[i])) out.insert(items.data[i]);
    };
    auto manager = FindInstance("MapLocationMgr");
    collect(manager, "GetMiniMapLocations", registered);
    collect(manager, "GetPingLocations", onMap);
    std::fprintf(file, "# manager %s: %zu minimap locations, %zu ping locations\n",
                 IsLive(manager) ? Name(manager).c_str() : "missing", registered.size(), onMap.size());
    // Every MapLocation (the objects the minimap and map draw) with the component it belongs to.
    std::unordered_map<UObject*, std::string> locationOf;
    if (auto locationClass = FindClass("MapLocation")) {
        for (int32_t i = 0, n = ObjectCount(); i < n; ++i) {
            auto location = ObjectAt(i);
            if (!IsLive(location) || IsDefaultOrArchetype(location) || !IsA(location, locationClass)) continue;
            UObject* owner = nullptr;
            ue::GetProp(location, "Component", owner);
            Call shown(location, "DisplayOnMiniMap"), texture(location, "IconTexture"), tint(location, "IconTextureColor"),
                where(location, "Position");
            UObject* drawn = texture.invoke() ? texture.get<UObject*>("ReturnValue") : nullptr;
            struct { float r, g, b, a; } color = tint.invoke() ? tint.get<decltype(color)>("ReturnValue") : decltype(color){};
            FVector at = where.invoke() ? where.get<FVector>("ReturnValue") : FVector{};
            char text[256];
            std::snprintf(text, sizeof text,
                          "%s inMinimapList=%d displayOnMinimap=%d drawnIcon=%s tint=(%.2f,%.2f,%.2f,%.2f) at=(%.0f,%.0f,%.0f)",
                          Name(location).c_str(), static_cast<int>(registered.contains(location)),
                          shown.invoke() ? static_cast<int>(shown.get<bool>("ReturnValue")) : -1,
                          IsLive(drawn) ? Name(drawn).c_str() : "null", color.r, color.g, color.b, color.a, at.x, at.y, at.z);
            if (IsLive(owner)) locationOf[owner] = text;
        }
    }
    std::map<std::string, int> byIcon;
    int count = 0;
    for (int32_t i = 0, n = ObjectCount(); i < n; ++i) {
        auto component = ObjectAt(i);
        if (!IsLive(component) || IsDefaultOrArchetype(component) || !IsA(component, componentClass)) continue;
        ++count;
        bool hidden = false, fromOwner = false, onMinimap = false;
        ue::GetBoolProp(component, "bHidden", hidden);
        ue::GetBoolProp(component, "bGetPositionFromOwner", fromOwner);
        ue::GetBoolProp(component, "bRenderedOnMiniMap", onMinimap);
        float color[4]{};
        if (auto property = FindChild(component->cls, "MinimapColor"))
            std::memcpy(color, reinterpret_cast<uint8_t*>(component) + PropertyOffset(property), sizeof color);
        UObject* icon = nullptr;
        ue::GetProp(component, "MapIcon", icon);
        std::string iconName = IsLive(icon) ? Name(icon) : "null";
        ++byIcon[iconName];
        auto location = locationOf.find(component);
        std::string details;
        if (auto property = FindChild(component->cls, "MapLocation")) {
            const uint8_t* at = reinterpret_cast<const uint8_t*>(component) + PropertyOffset(property);
            char hex[4];
            for (int b = 0; b < ElementSize(property) && b < 32; ++b) {
                std::snprintf(hex, sizeof hex, "%02x", at[b]);
                details += hex;
            }
        }
        std::fprintf(file, "%s location=[%s] owner=%s icon=%s type=%s display=%s edge=%s scale=%s hidden=%d fromOwner=%d "
                     "onMinimap=%d color=(%.2f,%.2f,%.2f,%.2f) details=%s name=%s\n",
                     Name(component->cls).c_str(), location == locationOf.end() ? "none" : location->second.c_str(), IsLive(component->outer) ? FullName(component->outer).c_str() : "?",
                     iconName.c_str(), FieldValue(component, nullptr, nullptr, "LocationType").c_str(),
                     FieldValue(component, nullptr, nullptr, "DisplayRule").c_str(),
                     FieldValue(component, nullptr, nullptr, "EdgeDisplay").c_str(),
                     FieldValue(component, nullptr, nullptr, "IconScale").c_str(), hidden, fromOwner, onMinimap,
                     color[0], color[1], color[2], color[3], details.c_str(),
                     FieldValue(component, nullptr, nullptr, "DisplayName").c_str());
    }
    std::fprintf(file, "\n# icons\n");
    for (auto& [name, uses] : byIcon) std::fprintf(file, "%5d %s\n", uses, name.c_str());
    std::fclose(file);
    summary = std::to_string(count) + " minimap components, " + std::to_string(byIcon.size()) + " icons";
    return true;
}

bool MissionState(const std::wstring& path, std::string& summary) {
    auto jobs = FindInstance("BendJobManager");
    auto mission = FindInstance("BendGameMission");
    if (!IsLive(jobs)) {
        summary = "no live BendJobManager; load into gameplay first";
        return false;
    }
    FILE* file = _wfopen(path.c_str(), L"w");
    if (!file) {
        summary = "could not open the mission state file";
        return false;
    }
    std::unordered_set<UObject*> seen;
    std::fprintf(file, "# managers\n%s\n", FullName(jobs).c_str());
    DumpFunctions(file, jobs->cls, seen);
    if (IsLive(mission)) {
        std::fprintf(file, "%s\n", FullName(mission).c_str());
        DumpFunctions(file, mission->cls, seen);
    }
    auto snapshot = ReadMissionSnapshot();
    int active = 0, done = 0;
    std::fprintf(file, "\n# jobs and missions (tracked '%s', episode %d)\n", snapshot.tracked.c_str(), snapshot.episode);
    for (auto& [name, row] : snapshot.rows) {
        active += row.active;
        done += row.done;
        std::fprintf(file, "%s %s\n", RowText(row).c_str(), name.c_str());
    }
    ObjectArray refs{};
    std::fprintf(file, "\n# active job objects\n");
    if (ue::GetProp(jobs, "ActiveJobRefsInOrder", refs) && refs.data && refs.count > 0 && refs.count < 256) {
        for (int32_t i = 0; i < refs.count; ++i) {
            UObject* job = refs.data[i];
            if (!IsLive(job)) continue;
            std::fprintf(file, "  %s\n", FullName(job).c_str());
            DumpPropertyValues(file, job);
            DumpObjectiveList(file, job, "CurrentJobObjectiveList");
            DumpObjectiveList(file, job, "DefaultJobObjectiveList");
            DumpFunctions(file, job->cls, seen);
        }
    }
    // Blueprint job scripts (IND_Jobscript_C and friends) carry the per-mission objective logic.
    std::fprintf(file, "\n# live job/mission script objects\n");
    int scripts = 0;
    for (int32_t i = 0, n = ObjectCount(); i < n && scripts < 64; ++i) {
        auto obj = ObjectAt(i);
        if (!IsLive(obj) || !IsLive(obj->cls) || IsDefaultOrArchetype(obj)) continue;
        auto cls = Name(obj->cls);
        std::string lower = cls;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (lower.find("jobscript") == std::string::npos && lower.find("missionscript") == std::string::npos) continue;
        ++scripts;
        std::fprintf(file, "  %s\n", FullName(obj).c_str());
        DumpFunctions(file, obj->cls, seen);
    }
    int catalogJobs = 0, catalogMissions = 0;
    DumpCatalog(file, jobs, mission, catalogJobs, catalogMissions);
    std::fclose(file);
    char text[200];
    std::snprintf(text, sizeof text,
                  "%d jobs/missions (%d active, %d done), tracked '%s', %d script objects, catalog %d jobs %d missions",
                  static_cast<int>(snapshot.rows.size()), active, done, snapshot.tracked.c_str(), scripts, catalogJobs,
                  catalogMissions);
    summary = text;
    return true;
}

bool MissionAct(const std::string& spec, bool allowed, std::string& summary) {
    if (!allowed) {
        summary = "mission-act needs [Research] MissionActions=1 in the INI (isolated test copies only)";
        return false;
    }
    auto parts = Split(spec);
    if (parts.empty()) {
        summary = "mission-act needs Arg=<Function> [ObjectName|-] [Param=Value ...]";
        return false;
    }
    UObject* target = nullptr;
    UObject* fn = nullptr;
    std::string function = parts[0];
    auto dot = function.find('.');
    std::vector<const char*> owners = {"BendJobManager", "BendGameMission"};
    std::string ownerName;
    if (dot != std::string::npos) {
        ownerName = function.substr(0, dot);
        function = function.substr(dot + 1);
        owners = {ownerName.c_str()};
    }
    constexpr uint32_t kFuncStatic = 0x2000;
    for (auto owner : owners) {
        auto instance = FindInstance(owner);
        bool defaults = false;
        // Function libraries such as BendGameplayStatics have no instances; static functions run on the CDO.
        if (!IsLive(instance)) {
            instance = FindDefaultObject(owner);
            defaults = true;
        }
        if (!IsLive(instance)) continue;
        if (auto found = FindFunction(instance->cls, function.c_str())) {
            uint32_t flags = *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(found) + off::FunctionFlags);
            if (defaults && !(flags & kFuncStatic)) continue;
            target = instance;
            fn = found;
            break;
        }
    }
    if (!fn) {
        summary = "function not found on BendJobManager/BendGameMission (or the named class): " + parts[0];
        return false;
    }
    uint16_t size = *reinterpret_cast<uint16_t*>(reinterpret_cast<uint8_t*>(fn) + off::FunctionParmsSize);
    std::vector<uint8_t> parms(std::max<size_t>(size, 1), 0);
    bool objectFilled = parts.size() < 2 || parts[1] == "-";
    std::unordered_map<std::string, std::string> named;
    for (size_t i = 2; i < parts.size(); ++i) {
        auto eq = parts[i].find('=');
        if (eq == std::string::npos || !eq) {
            summary = "expected Param=Value, got " + parts[i];
            return false;
        }
        named[parts[i].substr(0, eq)] = parts[i].substr(eq + 1);
    }
    int count = 0;
    for (auto p = Children(fn); p && count < 32; p = Next(p), ++count) {
        if (!IsLive(p) || !IsLive(p->cls)) break;
        auto kind = Name(p->cls), param = Name(p);
        int offset = PropertyOffset(p), elementSize = ElementSize(p);
        if (param == "ReturnValue" || offset < 0 || offset + elementSize > size) continue;
        auto at = parms.data() + offset;
        if (!objectFilled && kind == "ObjectProperty" && !named.contains(param)) {
            auto object = FindNamedInstance(parts[1]);
            if (!object) {
                summary = "no live object named " + parts[1];
                return false;
            }
            std::memcpy(at, &object, sizeof object);
            objectFilled = true;
            continue;
        }
        auto value = named.find(param);
        if (value == named.end()) continue;
        char* end = nullptr;
        const char* text = value->second.c_str();
        if (kind == "BoolProperty") *at = (value->second == "1" || value->second == "true") ? 1 : 0;
        else if (kind == "ByteProperty" && elementSize == 1) *at = static_cast<uint8_t>(std::strtoul(text, &end, 10));
        else if (kind == "IntProperty" && elementSize == 4) {
            int32_t v = static_cast<int32_t>(std::strtol(text, &end, 10));
            std::memcpy(at, &v, 4);
        } else if (kind == "FloatProperty" && elementSize == 4) {
            float v = std::strtof(text, &end);
            std::memcpy(at, &v, 4);
        } else if (kind == "NameProperty" && elementSize == sizeof(ue::FName)) {
            ue::FName name{ue::FindNameIndex(text), 0};
            if (name.index < 0) {
                summary = "no FName '" + value->second + "' exists for " + param;
                return false;
            }
            std::memcpy(at, &name, sizeof name);
        } else if (kind == "ObjectProperty") {
            auto object = FindNamedInstance(value->second);
            if (!object) {
                summary = "no live object named " + value->second;
                return false;
            }
            std::memcpy(at, &object, sizeof object);
        } else {
            summary = "parameter " + param + " has unsupported type " + kind;
            return false;
        }
        if (end && *end) {
            summary = "invalid number for " + param + ": " + value->second;
            return false;
        }
        named.erase(value);
    }
    if (!objectFilled) {
        summary = "the function has no object parameter for " + parts[1];
        return false;
    }
    if (!named.empty()) {
        summary = "unknown parameter " + named.begin()->first;
        return false;
    }
    auto before = ReadMissionSnapshot();
    Log("mission act: %s.%s on %s (%s)", Name(target->cls).c_str(), function.c_str(), Name(target).c_str(),
        Parameters(fn, parms.data()).c_str());
    g.processEvent(target, fn, parms.data());
    std::string result = "invoked";
    if (auto ret = FindChild(fn, "ReturnValue"))
        result = "returned " + Value(ret, parms.data());
    Log("mission act: after %s", Parameters(fn, parms.data()).c_str());
    auto after = ReadMissionSnapshot();
    int changes = LogMissionDiff(before, after, "act", 0);
    if (g_missionWatch) g_missionLast = after;
    summary = function + " " + result + ", " + std::to_string(changes) + " immediate state changes";
    Log("mission act: %s", summary.c_str());
    return true;
}

// Anim mirror: can an AI anim instance's reflected state drive a second mesh's AnimBP? A clone of the nearest AI
// pawn's body mesh runs the same anim class 3 m away, without a pawn or controller. Each frame the source
// instance's state is copied into the clone (variables, optionally anim-graph node state too, or nothing as a
// control), and twice a second the clone's component-space bone rotations are compared with the source's.
namespace {

struct MirrorField { int offset, size; bool object; std::string name; bool real = false; };

struct AnimMirror {
    bool active = false;
    AnimMirrorMode mode = AnimMirrorMode::bare;
    UObject* pawn = nullptr;      // source AI pawn
    UObject* mesh = nullptr;      // its body mesh component
    UObject* source = nullptr;    // its anim instance
    UObject* actor = nullptr;     // clone SkeletalMeshActor
    UObject* component = nullptr; // clone mesh component
    UObject* clone = nullptr;     // clone anim instance
    std::vector<MirrorField> fields;
    int bytes = 0;
    // Anim-graph node state: state machines' current state and elapsed time, asset players' time, asset and
    // blend inputs. Copied in nodes mode only. weights are the asset players' BlendWeight offsets.
    std::vector<MirrorField> nodes;
    std::vector<int> weights;
    std::vector<int> playerAssets; // per weights entry: offset of the player's Sequence/BlendSpace, or -1
    int nodeBytes = 0, machines = 0, players = 0;
    std::vector<int> activePlayers;
    int actionSamples = 0, actionMatches = 0;
    bool copyVars = false; // pawn mode: also copy the anim variables over what the clone derives itself
    std::vector<MirrorField> intent; // pawn mode with intent: the AI's locomotion intent variables only
    // Pawn mode with state: Bend's gameplay anim-state machines (BendPawn.AnimStateMachine) of both pawns; the
    // source's state is forwarded to the clone with RequestStateChange when it changes.
    bool forwardState = false;
    bool attackData = false; // with state: also send the attack/grapple animation data when those states start
    // With start-attack: the clone starts its own attack (BendPawn.StartMeleeAttack) with an attack name from
    // the source AI's behavior tree, as a replicated attack event would.
    bool startAttack = false;
    std::vector<FName> attackNames; // distinct BendBTTask_Attack names, tried in order
    // With play-oneshot: attacks and grapples are replayed on the clone as slot animations (dynamic montages),
    // the way native Unreal replicates one-off animations.
    bool playOneShot = false;
    std::vector<FName> slots; // SlotName of every AnimNode_Slot in the graph
    FName slot{};             // the slot played into
    UObject* lastOneShot = nullptr;
    UObject* lastMontage = nullptr; // the clone's dynamic montage for lastOneShot
    int oneShots = 0, oneShotsPlayed = 0;
    int oneShotSamples = 0, oneShotMatches = 0; // samples with the source in a replayed one-shot
    int clawsDisabled = -1;         // clone weapons with collision turned off; -1 until looked up
    // Damage check: the player's ReceiveAnyDamage events, attributed to the source, the clone or neither, and
    // both meshes' anim notifies (MeleeArmed opens a native swing). With heal, the player's health is held at
    // a buffer, so a long run measures hits instead of ending in death.
    UObject* player = nullptr;
    bool heal = false;
    bool keepClaws = false; // keep-claws: leave the clone's weapon collision on (control for the claw fix)
    float offset = 300.f;   // clone distance beside the source (cm); no-offset puts it on the source, claws included
    float playerHealth = 0; // at start
    int heals = 0;
    struct DamageTally { int hits = 0; float total = 0; };
    std::map<std::string, DamageTally> damage; // "source", "clone", "player", "other"
    std::map<std::string, int> sourceNotifies, cloneNotifies;
    // Notify trace: the notify events in progress, and per "owner notify" the blueprint calls made inside them.
    struct NotifyFrame { UObject* obj; UObject* fn; std::string label; };
    std::vector<NotifyFrame> notifyStack;
    std::map<std::string, std::map<std::string, int>> notifyCalls;
    bool muteNotifies = false; // mute-notifies: the clone's notify events are swallowed
    bool muteGameplay = false; // mute-gameplay: only its gameplay notifies (damage, counter, grapple)
    // counter-for-target (with mute-gameplay): counter notifies pass when the attack targets the local player.
    // pretend-other-target treats every attack as aimed at someone else, to exercise the muting branch.
    bool counterForTarget = false, pretendOtherTarget = false;
    bool oneShotForPlayer = false; // the latest replayed one-shot's target is the local player
    int countersPassed = 0, countersMuted = 0;
    // Counter test. auto-counter: when a counter window opens (a counter notify begins), the player controller's
    // TryToCounterAttack is called after kCounterDelay, as a timed button press would. mute-source-counter mutes
    // the source's own counter notifies, so only the clone's window is open (as on a guest). Every event named
    // *Counter* on the player, its controller and anim instance, the source and the clone is logged.
    bool autoCounter = false, muteSourceCounter = false;
    // counter-key (implies auto-counter): press the Melee key (F) with SendInput instead of TryToCounterAttack.
    bool counterKey = false;
    // counter-click (implies auto-counter): click the left mouse button (WeaponFire, the keyboard melee swing).
    bool counterClick = false;
    // watch-death: log damage and death events on the source and the clone (first 3 of each).
    bool watchDeath = false;
    // loop-oneshot: replay a one-shot whose montage ended while the source still plays that sequence.
    bool loopOneShots = false;
    int loopReplays = 0;
    // kill-clone: 12 s in, kill the clone with damage of its health + 10, as the game killed it when its source died.
    double killCloneAt = 0;
    std::map<std::string, int> deathEvents;
    // watch-counter: log the counter and melee events without pressing anything (for input sent from outside).
    bool watchCounter = false;
    double keyUpAt = 0;
    double counterAt = 0;
    std::string counterWindow; // whose window the pending press answers
    int counterTries = 0, counterSuccesses = 0;
    std::map<std::string, int> counterEvents;
    int mutedNotifies = 0;
    bool keepAi = false; // pawn mode with ai: the clone keeps its AI and is not fed (control)
    // Pawn mode with move-request (implied by intent): copy the movement component's AI move request.
    bool moveRequest = false;
    UObject* sourceStates = nullptr;
    UObject* cloneStates = nullptr;
    int32_t lastState = -1, stateChanges = 0, stateRequestsOk = 0;
    std::map<std::string, int> divergent; // pawn mode: variable name -> samples where source and clone differ
    // Pawn mode: the movement components whose Velocity and MovementMode are copied.
    UObject* sourceMove = nullptr;
    UObject* cloneMove = nullptr;
    coop::BoneTable bones;
    double started = 0, nextSample = 0;
    std::vector<float> meanErrors;
    uint64_t copies = 0;
    // The source mesh's LOD and tick settings, restored on stop.
    int32_t sourceForcedLod = 0;
    uint8_t sourceUpdateFlag = 0;
    bool sourceUro = false;
} g_mirror;

// Both meshes at LOD 0, posed every frame without update-rate optimization, so every bone is evaluated
// whether or not it is on screen: lower LODs drop bones, which then read back as reference poses.
void PinFullPose(UObject* mesh, int32_t forcedLod, uint8_t updateFlag, bool uro) {
    Call(mesh, "SetForcedLOD").set("InNewForcedLOD", forcedLod).invoke(); // 1 = LOD 0; 0 = automatic
    SetByteProp(mesh, "MeshComponentUpdateFlag", updateFlag); // 0 = AlwaysTickPoseAndRefreshBones
    SetBoolProp(mesh, "bEnableUpdateRateOptimizations", uro);
}

bool IsAnimAsset(UObject* obj) {
    if (!IsLive(obj) || !IsLive(obj->cls)) return false;
    auto kind = Name(obj->cls);
    return kind == "AnimSequence" || kind == "AnimMontage" || kind == "AnimComposite" || kind == "BlendSpace" ||
           kind == "BlendSpace1D" || kind == "AimOffsetBlendSpace" || kind == "AimOffsetBlendSpace1D";
}

// The variables BendCharacterAnimInstance and its subclasses (including the Blueprint class) declare: plain
// values, small structs and anim-asset references. Pawn/controller links, arrays, delegates and anim-graph node
// structs are left alone, so the clone never reads the source pawn.
void CollectMirrorFields(UObject* animClass, std::vector<MirrorField>& out, int& bytes) {
    auto stop = FindClass("BendAnimInstance");
    std::unordered_set<int> seen;
    for (auto c = animClass; IsLive(c) && c != stop; c = Super(c)) {
        int budget = 0;
        for (auto field = Children(c); field && budget < 4096; field = Next(field), ++budget) {
            if (!IsLive(field) || IsFunction(field)) continue;
            auto kind = Name(field->cls);
            int offset = PropertyOffset(field), size = ElementSize(field);
            if (offset < 0 || size <= 0 || !seen.insert(offset).second) continue;
            bool plain = kind == "BoolProperty" || kind == "ByteProperty" || kind == "IntProperty" ||
                         kind == "FloatProperty" || kind == "NameProperty" ||
                         (kind == "StructProperty" && size <= 48);
            bool object = kind == "ObjectProperty" && size == 8;
            if (!plain && !object) continue;
            out.push_back({offset, size, object, Name(field), kind == "FloatProperty"});
            bytes += size;
        }
    }
}

bool StructDerives(UObject* structure, UObject* base) {
    for (int depth = 0; IsLive(structure) && depth < 16; structure = Super(structure), ++depth)
        if (structure == base) return true;
    return false;
}

void AddNodeField(std::vector<MirrorField>& out, int& bytes, UObject* structure, int base, const char* name, bool object) {
    auto p = FindChild(structure, name);
    if (!p) return;
    out.push_back({base + PropertyOffset(p), ElementSize(p), object, name});
    bytes += ElementSize(p);
}

// The anim-graph nodes the Blueprint class declares (AnimGraphNode_* struct properties). State machines carry
// CurrentState/ElapsedTime and asset players InternalTimeAccumulator plus their asset, rate and blend inputs:
// together the runtime state a Source-style replicator would send.
void CollectNodeFields(AnimMirror& m) {
    auto scriptStruct = FindClass("ScriptStruct");
    auto machine = FindObject("AnimNode_StateMachine", scriptStruct);
    auto player = FindObject("AnimNode_AssetPlayerBase", scriptStruct);
    auto slotNode = FindObject("AnimNode_Slot", scriptStruct);
    auto stop = FindClass("BendAnimInstance");
    if (!machine || !player) return;
    for (auto c = m.source->cls; IsLive(c) && c != stop; c = Super(c)) {
        int budget = 0;
        for (auto field = Children(c); field && budget < 8192; field = Next(field), ++budget) {
            if (!IsLive(field) || Name(field->cls) != "StructProperty") continue;
            auto structure = PropertySubclassObject(field, "ScriptStruct");
            int base = PropertyOffset(field);
            if (!structure || base < 0) continue;
            if (slotNode && StructDerives(structure, slotNode)) {
                if (auto name = FindChild(structure, "SlotName")) {
                    FName value{};
                    std::memcpy(&value, reinterpret_cast<const uint8_t*>(m.source) + base + PropertyOffset(name), sizeof value);
                    bool known = false;
                    for (auto s : m.slots) known |= s.index == value.index && s.number == value.number;
                    if (value.index && !known) m.slots.push_back(value);
                }
            } else if (StructDerives(structure, machine)) {
                AddNodeField(m.nodes, m.nodeBytes, structure, base, "CurrentState", false);
                AddNodeField(m.nodes, m.nodeBytes, structure, base, "ElapsedTime", false);
                ++m.machines;
            } else if (StructDerives(structure, player)) {
                for (auto name : {"InternalTimeAccumulator", "PlayRate", "X", "Y", "Z"})
                    AddNodeField(m.nodes, m.nodeBytes, structure, base, name, false);
                AddNodeField(m.nodes, m.nodeBytes, structure, base, "Sequence", true);
                AddNodeField(m.nodes, m.nodeBytes, structure, base, "BlendSpace", true);
                if (auto weight = FindChild(structure, "BlendWeight")) {
                    m.weights.push_back(base + PropertyOffset(weight));
                    auto asset = FindChild(structure, "Sequence");
                    if (!asset) asset = FindChild(structure, "BlendSpace");
                    m.playerAssets.push_back(asset ? base + PropertyOffset(asset) : -1);
                }
                ++m.players;
            }
        }
    }
}

void CopyFields(const std::vector<MirrorField>& fields) {
    auto from = reinterpret_cast<const uint8_t*>(g_mirror.source);
    auto to = reinterpret_cast<uint8_t*>(g_mirror.clone);
    for (const auto& f : fields) {
        if (f.object) {
            UObject* value = nullptr;
            std::memcpy(&value, from + f.offset, 8);
            if (value && !IsAnimAsset(value)) continue;
        }
        std::memcpy(to + f.offset, from + f.offset, static_cast<size_t>(f.size));
    }
}

// Phase-insensitive check: the animation asset of the highest-weighted asset player. Native-style sync lets each
// machine run its own walk cycle, so bone error can stay high while the action is right; matching dominant
// assets means both play the same action.
UObject* DominantAsset(UObject* instance) {
    auto base = reinterpret_cast<const uint8_t*>(instance);
    float best = 0.001f;
    UObject* asset = nullptr;
    for (size_t i = 0; i < g_mirror.weights.size(); ++i) {
        float weight = 0;
        std::memcpy(&weight, base + g_mirror.weights[i], sizeof weight);
        if (weight <= best || g_mirror.playerAssets[i] < 0) continue;
        UObject* candidate = nullptr;
        std::memcpy(&candidate, base + g_mirror.playerAssets[i], sizeof candidate);
        // Facial layers blend at full weight over any body action; only body animation counts.
        if (!IsLive(candidate) || Name(candidate).find("_face_") != std::string::npos) continue;
        best = weight;
        asset = candidate;
    }
    return asset;
}

int ActivePlayers() {
    int active = 0;
    for (int offset : g_mirror.weights) {
        float weight = 0;
        std::memcpy(&weight, reinterpret_cast<const uint8_t*>(g_mirror.source) + offset, sizeof weight);
        if (weight > 0.001f) ++active;
    }
    return active;
}

const char* ModeName(AnimMirrorMode mode) {
    switch (mode) {
    case AnimMirrorMode::nodes: return "nodes";
    case AnimMirrorMode::variables: return "variables";
    case AnimMirrorMode::pawn: return "pawn";
    default: return "bare";
    }
}

// Copies one reflected property by raw bytes between two objects of the same class.
void CopyProp(UObject* from, UObject* to, const char* name) {
    if (!IsLive(from) || !IsLive(to) || from->cls != to->cls) return;
    if (auto p = FindChild(from->cls, name); p && PropertyOffset(p) >= 0 && ElementSize(p) > 0)
        std::memcpy(reinterpret_cast<uint8_t*>(to) + PropertyOffset(p),
                    reinterpret_cast<const uint8_t*>(from) + PropertyOffset(p), static_cast<size_t>(ElementSize(p)));
}

// A pawn of the source's class with the source's look, its AI controller removed and collision off, the way a
// native client holds another machine's enemy. Returns the pawn or nullptr.
UObject* SpawnPawnClone(UObject* context, UObject* source, UObject* asset, bool keepController, bool keepAi,
                        std::string& summary) {
    auto library = FindDefaultObject("AIBlueprintHelperLibrary");
    Call where(source, "K2_GetActorLocation"), facing(source, "K2_GetActorRotation");
    if (!library || !where.invoke() || !facing.invoke()) { summary = "spawn library or source transform unavailable"; return nullptr; }
    auto at = where.get<FVector>("ReturnValue");
    at.y += 300.f;
    Call spawn(library, "SpawnAIFromClass");
    if (!spawn.set("WorldContextObject", context).set("PawnClass", source->cls)
             .set("BehaviorTree", static_cast<UObject*>(nullptr)).set("Location", at)
             .set("Rotation", facing.get<FRotator>("ReturnValue")).setBool("bNoCollisionFail", true).invoke()) {
        summary = "SpawnAIFromClass failed";
        return nullptr;
    }
    auto pawn = spawn.get<UObject*>("ReturnValue");
    if (!IsLive(pawn)) { summary = "SpawnAIFromClass returned no pawn"; return nullptr; }
    UObject* body = nullptr;
    if (GetProp(pawn, "Mesh", body) && IsLive(body)) Call(body, "SetSkeletalMesh").set("NewMesh", asset).invoke();
    UObject* controller = nullptr;
    if (keepAi) return pawn; // control: an ordinary AI swarmer, to show the clone's anim graph can leave idle
    if (GetProp(pawn, "Controller", controller) && IsLive(controller)) {
        UObject* brain = nullptr;
        if (GetProp(controller, "BrainComponent", brain) && IsLive(brain))
            Call(brain, "StopLogic").set("Reason", FStringRaw{}).invoke();
        if (!keepController) {
            Call(controller, "UnPossess").invoke();
            Call(controller, "K2_DestroyActor").invoke();
        }
    }
    Call(pawn, "SetActorEnableCollision").setBool("bNewActorEnableCollision", false).invoke();
    return pawn;
}

bool HasOption(const std::string& options, const char* name) {
    size_t start = 0;
    while (start <= options.size()) {
        size_t end = options.find(',', start);
        if (end == std::string::npos) end = options.size();
        if (options.compare(start, end - start, name) == 0) return true;
        start = end + 1;
    }
    return false;
}

std::string CurrentStateName(UObject* machine) {
    Call name(machine, "GetCurrentStateFName");
    if (!IsLive(machine) || !name.invoke()) return "?";
    auto value = name.get<FName>("ReturnValue");
    return value.index ? NameString(value.index) : "-";
}

// The enum value of a Bend anim-state machine's current state, found with IsInStateEnum.
int32_t CurrentStateEnum(UObject* machine) {
    for (int32_t state = 0; state < 64; ++state) {
        Call test(machine, "IsInStateEnum");
        if (!test.set("State", static_cast<uint8_t>(state)).invoke()) return -1;
        if (test.get<bool>("ReturnValue")) return state;
    }
    return -1;
}

// The animation data an attack or grapple state plays, sent once when the state starts (an event, like an RPC).
// The AI starts attacks natively (BendPawn.StartMeleeAttack is not seen in ProcessEvent), so the data is read
// from the source's anim instance. BendAttackData's TArray at +24 (16 bytes) is skipped: copying an array
// header would share the source's allocation.
void SendAttackData(int32_t state) {
    constexpr int32_t kMeleeAttack = 5, kGrapple = 6;
    auto from = reinterpret_cast<const uint8_t*>(g_mirror.source);
    auto to = reinterpret_cast<uint8_t*>(g_mirror.clone);
    std::string what;
    if (state == kMeleeAttack) {
        auto p = FindChild(g_mirror.source->cls, "AttackData");
        if (!p || ElementSize(p) != 96) return;
        int base = PropertyOffset(p);
        std::memcpy(to + base, from + base, 24);
        std::memcpy(to + base + 40, from + base + 40, 56);
        UObject* anim = nullptr;
        std::memcpy(&anim, from + base, sizeof anim);
        what = "attack " + (IsLive(anim) ? Name(anim) : std::string("-"));
        CopyProp(g_mirror.source, g_mirror.clone, "MeleeAnimLength");
    } else if (state == kGrapple) {
        for (auto name : {"GrappleAnimSeq", "GrappleEnterAnimSeq", "GrappleBreakingOutAnimSeq", "GrappleBlendspace"})
            CopyProp(g_mirror.source, g_mirror.clone, name);
        UObject* anim = nullptr;
        GetProp(g_mirror.source, "GrappleAnimSeq", anim);
        what = "grapple " + (IsLive(anim) ? Name(anim) : std::string("-"));
    } else {
        return;
    }
    Log("anim mirror event: %s", what.c_str());
}

// Pawn mode with state: forwards the source's gameplay anim state to the clone, as a replicated state event.
void ForwardState() {
    int32_t state = CurrentStateEnum(g_mirror.sourceStates);
    if (state < 0 || state == g_mirror.lastState) return;
    g_mirror.lastState = state;
    ++g_mirror.stateChanges;
    // PerformAction (4) plays an AI action that the clone lacks; entered without it, the clone stays locked in
    // that state and refuses every later request (lab, 2026-11-06). Not forwarded until its payload is known.
    constexpr int32_t kPerformAction = 4;
    if (state == kPerformAction) {
        Log("anim mirror state: source PerformAction (4) not forwarded");
        return;
    }
    if (g_mirror.attackData) SendAttackData(state);
    constexpr int32_t kMeleeAttack = 5;
    if (g_mirror.startAttack && state == kMeleeAttack) {
        uint64_t weakTarget = 0;
        GetProp(g_mirror.source, "LastValidAttackTarget", weakTarget);
        auto target = ResolveWeak(weakTarget);
        auto attack = FindChild(g_mirror.source->cls, "AttackData");
        UObject* sourceAnim = nullptr;
        if (attack) std::memcpy(&sourceAnim, reinterpret_cast<const uint8_t*>(g_mirror.source) + PropertyOffset(attack), 8);
        std::string tried;
        UObject* anim = nullptr;
        for (auto name : g_mirror.attackNames) {
            Call start(g_mirror.actor, "StartMeleeAttack");
            start.set("AttackTarget", target).set("AttackName", name).setBool("IgnoreDistance", true);
            anim = start.invoke() ? start.get<UObject*>("ReturnValue") : nullptr; // BendAttackData.AttackAnim at +0
            tried += (tried.empty() ? "" : ",") + NameString(name.index);
            if (IsLive(anim)) break;
        }
        Log("anim mirror event: source %s; clone StartMeleeAttack(%s) tried %s -> %s",
            IsLive(sourceAnim) ? Name(sourceAnim).c_str() : "-", target ? Name(target).c_str() : "none",
            tried.c_str(), IsLive(anim) ? Name(anim).c_str() : "no attack");
    }
    Call request(g_mirror.cloneStates, "RequestStateChange");
    bool ok = request.set("State", static_cast<uint8_t>(state)).setBool("ForceReenter", false).invoke() &&
              request.get<bool>("ReturnValue");
    if (ok) ++g_mirror.stateRequestsOk;
    Log("anim mirror state: source %s (%d) -> clone request %s, clone now %s", CurrentStateName(g_mirror.sourceStates).c_str(),
        state, ok ? "accepted" : "refused", CurrentStateName(g_mirror.cloneStates).c_str());
}

// A pawn clone's weapons (such as the swarmer's claw, a separate actor whose overlap deals melee damage) keep
// their collision when the pawn's is turned off; this turns it off on every weapon the clone owns.
int DisableCloneWeapons(UObject* clone) {
    static UObject* weaponClass = FindClass("BendWeapon");
    int disabled = 0;
    for (int32_t i = 0; weaponClass && i < ObjectCount(); ++i) {
        auto weapon = ObjectAt(i);
        if (!IsLive(weapon) || !IsA(weapon, weaponClass) || IsDefaultOrArchetype(weapon)) continue;
        UObject* owner = nullptr;
        UObject* instigator = nullptr;
        GetProp(weapon, "Owner", owner);
        GetProp(weapon, "Instigator", instigator);
        if (owner != clone && instigator != clone) continue;
        Call(weapon, "SetActorEnableCollision").setBool("bNewActorEnableCollision", false).invoke();
        ++disabled;
    }
    return disabled;
}

// Pawn mode with play-oneshot: while the source is attacking or grappling, each new body animation it plays is
// played on the clone through a graph slot, as a replicated montage would be (an asset plus a start).
void FollowOneShots() {
    constexpr int32_t kMeleeAttack = 5, kGrapple = 6;
    int32_t state = g_mirror.forwardState ? g_mirror.lastState : CurrentStateEnum(g_mirror.sourceStates);
    if (state != kMeleeAttack && state != kGrapple) {
        g_mirror.lastOneShot = nullptr;
        return;
    }
    auto asset = DominantAsset(g_mirror.source);
    if (!asset || !g_mirror.slot.index) return;
    if (asset == g_mirror.lastOneShot) {
        // A looping sequence (a grapple loop) keeps playing on the source after the clone's one-pass montage
        // ends; replay it, as a looping montage section would keep going.
        if (!g_mirror.loopOneShots || !IsLive(g_mirror.lastMontage)) return;
        Call playing(g_mirror.clone, "Montage_IsPlaying");
        if (!playing.set("Montage", g_mirror.lastMontage).invoke() || playing.get<bool>("ReturnValue")) return;
        if (++g_mirror.loopReplays <= 6) Log("anim mirror one-shot: %s still playing on the source, replayed", Name(asset).c_str());
    } else {
        g_mirror.lastOneShot = asset;
        ++g_mirror.oneShots;
    }
    // Blend spaces cannot play in a slot; only sequences are replayed.
    if (Name(asset->cls) != "AnimSequence") {
        Log("anim mirror one-shot: %s is a %s, not replayed", Name(asset).c_str(), Name(asset->cls).c_str());
        return;
    }
    Call play(g_mirror.clone, "PlaySlotAnimationAsDynamicMontage");
    play.set("Asset", asset).set("SlotNodeName", g_mirror.slot).set("BlendInTime", 0.1f).set("BlendOutTime", 0.2f)
        .set("InPlayRate", 1.f).set("LoopCount", 1).set("BlendOutTriggerTime", -1.f);
    UObject* montage = play.invoke() ? play.get<UObject*>("ReturnValue") : nullptr;
    if (IsLive(montage)) ++g_mirror.oneShotsPlayed;
    g_mirror.lastMontage = montage;
    // The attack's target, latched per replayed one-shot as an attack event would carry it: the source AI's
    // current target pawn, compared with the local player (the guest on a real stand-in).
    UObject* controller = nullptr;
    UObject* target = nullptr;
    if (GetProp(g_mirror.pawn, "Controller", controller) && IsLive(controller)) {
        Call current(controller, "GetCurrentTargetPawn");
        if (current.invoke()) target = current.get<UObject*>("ReturnValue");
    }
    g_mirror.oneShotForPlayer = target == g_mirror.player && !g_mirror.pretendOtherTarget;
    Log("anim mirror one-shot: %s in state %d -> %s; target %s", Name(asset).c_str(), state,
        IsLive(montage) ? "playing" : "failed", g_mirror.oneShotForPlayer ? "local player" : "other");
}

// Pawn mode: the clone follows the source 3 m to the side (offset) with the source's velocity and movement mode, and its
// own movement tick stays off so the copied values are what its anim instance reads.
void FeedPawnClone() {
    Call where(g_mirror.pawn, "K2_GetActorLocation"), facing(g_mirror.pawn, "K2_GetActorRotation");
    if (!where.invoke() || !facing.invoke()) return;
    auto at = where.get<FVector>("ReturnValue");
    at.y += g_mirror.offset;
    // Not a teleport: a teleport every frame keeps the anim graph in its post-teleport reset (it gates
    // locomotion on bHasTouchedGroundAfterSpawningOrTeleporting), which a native proxy's smooth moves never do.
    Call(g_mirror.actor, "K2_SetActorLocationAndRotation").set("NewLocation", at)
        .set("NewRotation", facing.get<FRotator>("ReturnValue")).setBool("bSweep", false).setBool("bTeleport", false).invoke();
    CopyProp(g_mirror.sourceMove, g_mirror.cloneMove, "Velocity");
    CopyProp(g_mirror.sourceMove, g_mirror.cloneMove, "MovementMode");
    // With collision off the clone never lands on its own; the source's grounded flag stands in for it.
    CopyProp(g_mirror.source, g_mirror.clone, "bHasTouchedGroundAfterSpawningOrTeleporting");
    if (g_mirror.moveRequest) {
        // The movement request the AI's path following makes: what the stopped clone's movement lacks.
        for (auto name : {"Acceleration", "RequestedVelocity", "bHasRequestedVelocity", "desiredSpeed", "DesiredRotation",
                          "DeltaToDesiredRotation", "MaxWalkSpeed"})
            CopyProp(g_mirror.sourceMove, g_mirror.cloneMove, name);
    }
}

FQuat BoneRotation(UObject* component, FName bone) {
    Call socket(component, "GetSocketTransform");
    socket.set("InSocketName", bone).set("TransformSpace", static_cast<uint8_t>(2)); // RTS_Component
    return socket.invoke() ? socket.get<FTransform>("ReturnValue").rotation : FQuat{0, 0, 0, 1};
}

float AngleDegrees(const FQuat& a, const FQuat& b) {
    float dot = std::fabs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w);
    return 2.f * std::acos(std::min(dot, 1.f)) * 57.2957795f;
}

float Percentile(std::vector<float> values, float p) {
    if (values.empty()) return -1;
    std::sort(values.begin(), values.end());
    return values[std::min(values.size() - 1, static_cast<size_t>(p * static_cast<float>(values.size())))];
}

void PlaceMirror() {
    Call location(g_mirror.mesh, "K2_GetComponentLocation");
    Call rotation(g_mirror.mesh, "K2_GetComponentRotation");
    if (!location.invoke() || !rotation.invoke()) return;
    auto at = location.get<FVector>("ReturnValue");
    at.y += 300.f;
    Call(g_mirror.actor, "K2_SetActorLocationAndRotation").set("NewLocation", at)
        .set("NewRotation", rotation.get<FRotator>("ReturnValue")).setBool("bSweep", false).setBool("bTeleport", true).invoke();
}

} // namespace

namespace {
// Which mirror pawn an actor belongs to: itself, its owner or instigator chain, or a controller's pawn.
const char* MirrorParty(UObject* actor) {
    for (int depth = 0; depth < 4 && IsLive(actor); ++depth) {
        if (actor == g_mirror.pawn) return "source";
        if (actor == g_mirror.actor) return "clone";
        if (actor == g_mirror.player) return "player";
        UObject* next = nullptr;
        if (!GetProp(actor, "Pawn", next) || !IsLive(next)) next = nullptr; // controllers
        if (!next && (!GetProp(actor, "Owner", next) || !IsLive(next))) next = nullptr;
        if (!next && (!GetProp(actor, "Instigator", next) || !IsLive(next))) next = nullptr;
        actor = next;
    }
    return nullptr;
}

void SetPlayerHealth(float value) {
    auto prop = FindChild(g_mirror.player->cls, "Health");
    int offset = prop ? PropertyOffset(prop) : -1;
    if (offset > 0) std::memcpy(reinterpret_cast<uint8_t*>(g_mirror.player) + offset, &value, sizeof value);
}
} // namespace

// Which mirror mesh a notify event belongs to: "source", "clone" or null. Blueprint notifies arrive as
// Received_Notify/NotifyBegin/NotifyTick/NotifyEnd on the notify object with the mesh as first parameter;
// skeleton notifies as AnimNotify_* events on the anim instance.
const char* NotifyOwner(UObject* obj, UObject* fn, void* parms, std::string& label) {
    static int names[4] = {FindNameIndex("Received_Notify"), FindNameIndex("Received_NotifyBegin"),
                           FindNameIndex("Received_NotifyTick"), FindNameIndex("Received_NotifyEnd")};
    for (int name : names) {
        if (name <= 0 || fn->name.index != name || !parms) continue;
        auto mesh = *static_cast<UObject**>(parms);
        const char* owner = mesh == g_mirror.mesh ? "source" : mesh == g_mirror.component ? "clone" : nullptr;
        if (owner) label = Name(obj->cls);
        return owner;
    }
    if (obj != g_mirror.source && obj != g_mirror.clone) return nullptr;
    std::string function = Name(fn);
    if (!function.starts_with("AnimNotify_")) return nullptr;
    label = function;
    return obj == g_mirror.source ? "source" : "clone";
}

bool AnimMirrorEvent(UObject* obj, UObject* fn, void* parms) {
    if (!g_mirror.active || !IsLive(fn) || !IsLive(obj)) return false;
    static int anyDamage = FindNameIndex("ReceiveAnyDamage");
    if (fn->name.index == anyDamage && obj == g_mirror.player && parms) {
        float amount = 0, health = 0;
        UObject *instigator = nullptr, *causer = nullptr;
        ReadEventParam(fn, parms, "Damage", amount);
        ReadEventParam(fn, parms, "InstigatedBy", instigator);
        ReadEventParam(fn, parms, "DamageCauser", causer);
        GetProp(g_mirror.player, "Health", health);
        const char* party = MirrorParty(causer);
        if (!party) party = MirrorParty(instigator);
        if (!party) party = "other";
        auto& tally = g_mirror.damage[party];
        ++tally.hits;
        tally.total += amount;
        Log("anim mirror damage: %.1f from %s (causer %s, instigator %s); player health %.1f", amount, party,
            IsLive(causer) ? Name(causer).c_str() : "null", IsLive(instigator) ? Name(instigator).c_str() : "null",
            health);
    }
    // Inside a notify: record the blueprint calls it makes (native calls are not visible here).
    if (!g_mirror.notifyStack.empty()) {
        auto& calls = g_mirror.notifyCalls[g_mirror.notifyStack.back().label];
        std::string call = Name(obj->cls) + "." + Name(fn);
        if (calls.size() < 48 || calls.contains(call)) ++calls[call];
    }
    // watch-death: how each pawn takes damage and dies, to find what a guest-side death event should call.
    if (g_mirror.watchDeath && (obj == g_mirror.pawn || obj == g_mirror.actor)) {
        std::string function = Name(fn);
        for (const char* word : {"Death", "Dead", "Die", "Kill", "Damage", "Ragdoll", "Hit"}) {
            if (function.find(word) == std::string::npos) continue;
            float sourceHealth = -1, cloneHealth = -1;
            GetProp(g_mirror.pawn, "Health", sourceHealth);
            GetProp(g_mirror.actor, "Health", cloneHealth);
            std::string detail;
            if (function == "ReceiveAnyDamage") {
                float damage = 0;
                UObject *instigatedBy = nullptr, *causer = nullptr;
                ReadEventParam(fn, parms, "Damage", damage);
                ReadEventParam(fn, parms, "InstigatedBy", instigatedBy);
                ReadEventParam(fn, parms, "DamageCauser", causer);
                char text[200];
                std::snprintf(text, sizeof text, " %.1f by %s (causer %s)", damage,
                              IsLive(instigatedBy) ? Name(instigatedBy).c_str() : "none",
                              IsLive(causer) ? Name(causer).c_str() : "none");
                detail = text;
            }
            if (++g_mirror.deathEvents[std::string(obj == g_mirror.pawn ? "source." : "clone.") + function] <= 3)
                Log("anim mirror death: %s.%s%s; source health %.1f, clone health %.1f",
                    obj == g_mirror.pawn ? "source" : "clone", function.c_str(), detail.c_str(), sourceHealth,
                    cloneHealth);
            break;
        }
    }
    if (g_mirror.autoCounter || g_mirror.watchCounter) {
        std::string function = Name(fn);
        bool named = function.find("Counter") != std::string::npos || function.find("Melee") != std::string::npos ||
                     function.find("Attack") != std::string::npos;
        // Any notify on the player's anim shows the player acting (a swing, a dodge), whatever it is called.
        bool notify = !named && g_mirror.watchCounter && function.rfind("AnimNotify_", 0) == 0;
        if (named || notify) {
            UObject *controller = nullptr, *playerMesh = nullptr, *playerAnim = nullptr;
            GetProp(g_mirror.player, "Controller", controller);
            if (GetProp(g_mirror.player, "Mesh", playerMesh) && IsLive(playerMesh)) {
                Call anim(playerMesh, "GetAnimInstance");
                if (anim.invoke()) playerAnim = anim.get<UObject*>("ReturnValue");
            }
            const char* who = obj == g_mirror.player ? "player" : obj == controller ? "controller"
                            : obj == playerAnim ? "player anim" : obj == g_mirror.pawn ? "source"
                            : obj == g_mirror.source ? "source anim" : obj == g_mirror.actor ? "clone"
                            : obj == g_mirror.clone ? "clone anim" : nullptr;
            if (notify && obj != playerAnim) who = nullptr;
            if (who && notify) {
                if (++g_mirror.counterEvents[std::string(who) + "." + function] == 1)
                    Log("anim mirror counter: first %s.%s", who, function.c_str());
            } else if (who) {
                std::string key = std::string(who) + "." + function;
                ++g_mirror.counterEvents[key];
                if (function == "OnCounterAttackSuccessful") ++g_mirror.counterSuccesses;
                float sourceHealth = -1, cloneHealth = -1;
                GetProp(g_mirror.pawn, "Health", sourceHealth);
                GetProp(g_mirror.actor, "Health", cloneHealth);
                Log("anim mirror counter: %s (window %s); source %s health %.1f, clone %s health %.1f", key.c_str(),
                    g_mirror.counterWindow.empty() ? "none" : g_mirror.counterWindow.c_str(),
                    CurrentStateName(g_mirror.sourceStates).c_str(), sourceHealth,
                    CurrentStateName(g_mirror.cloneStates).c_str(), cloneHealth);
            }
        }
    }
    std::string label;
    const char* owner = NotifyOwner(obj, fn, parms, label);
    if (!owner) return false;
    bool clone = owner[0] == 'c';
    auto& counts = clone ? g_mirror.cloneNotifies : g_mirror.sourceNotifies;
    if (++counts[label] == 1) Log("anim mirror: %s notify %s", owner, label.c_str());
    // Gameplay notifies, from their compiled blueprints' locals (reflection, build 19221447): MeleeArmed arms the
    // claws (damage), CounterIcon shows a counter widget, the CanCounter/CanProvokeCounter ones open counter
    // windows on the pawn, AttemptGrapple starts a grapple through the AI controller, DamageModifier changes
    // damage. Cosmetic and self-contained notifies (blood, face, the clone's own movement) stay.
    static const char* gameplay[] = {"AnimNotify_MeleeArmed_C", "AnimNotify_CounterIcon_C",
                                     "AnimNotify_CanCounterMyAttack_C", "AnimNotify_CanCounterMyAttack_State_C",
                                     "AnimNotify_CanProvokeCounterMyAttack_State_C", "AnimNotify_AttemptGrapple_C",
                                     "AnimNotifyState_DamageModifier_C"};
    bool mute = clone && g_mirror.muteNotifies;
    for (const char* name : gameplay) mute = mute || (clone && g_mirror.muteGameplay && label == name);
    // counter-for-target: the counter notifies (gameplay[1..4]) pass only for an attack on the local player,
    // whom the prompt and window are for; damage and grapple notifies stay muted.
    bool counter = false;
    for (int i = 1; i <= 4; ++i) counter = counter || label == gameplay[i];
    if (clone && counter && g_mirror.counterForTarget) {
        mute = !g_mirror.oneShotForPlayer;
        ++(mute ? g_mirror.countersMuted : g_mirror.countersPassed);
    }
    if (!clone && counter && g_mirror.muteSourceCounter) mute = true;
    static int windowBegin = FindNameIndex("Received_NotifyBegin");
    bool opens = label == "AnimNotify_CanCounterMyAttack_State_C" && fn->name.index == windowBegin;
    if (opens && !mute && g_mirror.autoCounter && g_mirror.counterAt == 0) {
        constexpr double kCounterDelay = 0.15;
        g_mirror.counterAt = Seconds() + kCounterDelay;
        g_mirror.counterWindow = owner;
    }
    if (mute) {
        if (clone) ++g_mirror.mutedNotifies;
        return true;
    }
    g_mirror.notifyStack.push_back({obj, fn, std::string(owner) + " " + label});
    return false;
}

void AnimMirrorEventDone(UObject* obj, UObject* fn) {
    if (!g_mirror.active || g_mirror.notifyStack.empty()) return;
    // Search down the stack so a frame whose completion was missed cannot keep recording forever.
    auto& stack = g_mirror.notifyStack;
    for (size_t i = stack.size(); i-- > 0;) {
        if (stack[i].obj != obj || stack[i].fn != fn) continue;
        stack.resize(i);
        return;
    }
}

bool AnimMirroring() { return g_mirror.active; }

// dump-input: the combat action mappings (counter, melee, dodge, attack) of the input settings and of the live
// PlayerInput, so a lab press can use the game's real binding.
void LogCombatInputMappings(UObject* localPawn) {
    UObject* controller = nullptr;
    UObject* playerInput = nullptr;
    if (GetProp(localPawn, "Controller", controller) && IsLive(controller)) GetProp(controller, "PlayerInput", playerInput);
    std::pair<const char*, UObject*> sources[] = {{"InputSettings", FindDefaultObject("InputSettings")},
                                                  {"PlayerInput", playerInput}};
    for (auto [label, owner] : sources) {
        ObjectiveArrayView view;
        std::string error;
        if (!IsLive(owner) || !ReadObjectiveArray(owner, "ActionMappings", view, error)) {
            Log("anim mirror input: %s ActionMappings unreadable (%s)", label, IsLive(owner) ? error.c_str() : "no object");
            continue;
        }
        std::string text;
        for (int i = 0; i < view.count; ++i) {
            FName action{}, key{};
            std::memcpy(&action, view.data + i * view.stride, sizeof action);
            std::memcpy(&key, view.data + i * view.stride + 8, sizeof key);
            std::string name = NameString(action.index), lower = name;
            for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            text += (text.empty() ? "" : ", ") + name + "=" + NameString(key.index);
            if (text.size() > 300) {
                Log("anim mirror input: %s: %s", label, text.c_str());
                text.clear();
            }
        }
        Log("anim mirror input: %s has %d action mappings; rest: %s", label, view.count,
            text.empty() ? "none" : text.c_str());
    }
    // Keyboard bindings live in Bend's own mapper; ask the live one for the combat actions. The returned arrays
    // are engine-allocated and leaked (diagnostics only).
    auto mapperClass = FindClass("BendInputMapper");
    UObject* mapper = nullptr;
    for (int32_t i = 0; mapperClass && i < ObjectCount() && !mapper; ++i) {
        auto obj = ObjectAt(i);
        if (IsLive(obj) && !IsDefaultOrArchetype(obj) && IsA(obj, mapperClass)) mapper = obj;
    }
    if (!mapper) { Log("anim mirror input: no live BendInputMapper"); return; }
    struct RawArray { uint8_t* data; int32_t count, capacity; };
    constexpr int kActionMappingStride = 40; // InputActionKeyMapping: ActionName@0, Key@8+24, flags@32, Primary@36
    for (const char* action : {"Parry", "Melee", "Dodge", "Counter"}) {
        Call find(mapper, "FindInputActionKeyAndAxisMappings");
        if (!find.set("ActionName", FName{FindNameIndex(action), 0}).invoke()) continue;
        auto mappings = find.get<RawArray>("OutActionMappings");
        std::string keys;
        for (int i = 0; mappings.data && i < mappings.count && i < 16; ++i) {
            FName key{};
            std::memcpy(&key, mappings.data + i * kActionMappingStride + 8, sizeof key);
            keys += (keys.empty() ? "" : ", ") + NameString(key.index);
        }
        Log("anim mirror input: BendInputMapper %s -> %s", action, keys.empty() ? "none" : keys.c_str());
    }
}

bool AnimMirrorStart(UObject* localPawn, AnimMirrorMode mode, const std::string& options, std::string& summary) {
    if (HasOption(options, "dump-input")) LogCombatInputMappings(localPawn);
    bool keepController = HasOption(options, "keep-controller");
    bool moveTick = HasOption(options, "move-tick");
    bool copyVars = HasOption(options, "copy-vars");
    if (g_mirror.active) { summary = "anim mirror already running"; return false; }
    auto aiClass = FindClass("BendAIPawn");
    auto statics = FindDefaultObject("GameplayStatics");
    auto actorClass = FindClass("SkeletalMeshActor");
    Call self(localPawn, "K2_GetActorLocation");
    if (!aiClass || !statics || !actorClass || !self.invoke()) { summary = "missing classes or player location"; return false; }
    auto me = self.get<FVector>("ReturnValue");
    UObject* best = nullptr;
    float bestDistance = 3000.f * 3000.f; // 30 m
    for (int32_t i = 0; i < ObjectCount(); ++i) {
        auto obj = ObjectAt(i);
        if (!IsLive(obj) || IsDefaultOrArchetype(obj) || !IsA(obj, aiClass)) continue;
        Call alive(obj, "IsAlive"), where(obj, "K2_GetActorLocation");
        if (!alive.invoke() || !alive.get<bool>("ReturnValue") || !where.invoke()) continue;
        auto at = where.get<FVector>("ReturnValue");
        float d = (at.x - me.x) * (at.x - me.x) + (at.y - me.y) * (at.y - me.y) + (at.z - me.z) * (at.z - me.z);
        if (d < bestDistance) { bestDistance = d; best = obj; }
    }
    AnimMirror m;
    m.mode = mode;
    m.player = localPawn;
    m.heal = HasOption(options, "heal");
    m.keepClaws = HasOption(options, "keep-claws");
    m.muteNotifies = HasOption(options, "mute-notifies");
    m.muteGameplay = HasOption(options, "mute-gameplay");
    m.counterForTarget = HasOption(options, "counter-for-target");
    m.counterKey = HasOption(options, "counter-key");
    m.counterClick = HasOption(options, "counter-click");
    m.watchDeath = HasOption(options, "watch-death") || HasOption(options, "kill-clone");
    m.loopOneShots = HasOption(options, "loop-oneshot");
    if (HasOption(options, "kill-clone")) m.killCloneAt = Seconds() + 12.0;
    m.autoCounter = HasOption(options, "auto-counter") || m.counterKey || m.counterClick;
    m.muteSourceCounter = HasOption(options, "mute-source-counter");
    m.watchCounter = HasOption(options, "watch-counter");
    m.pretendOtherTarget = HasOption(options, "pretend-other-target");
    if (HasOption(options, "no-offset")) m.offset = 0;
    GetProp(localPawn, "Health", m.playerHealth);
    m.copyVars = copyVars;
    m.moveRequest = HasOption(options, "move-request") || HasOption(options, "intent");
    m.pawn = best;
    if (!m.pawn || !GetProp(m.pawn, "Mesh", m.mesh) || !IsLive(m.mesh)) { summary = "no live AI pawn with a mesh within 30 m"; return false; }
    Call sourceAnim(m.mesh, "GetAnimInstance");
    m.source = sourceAnim.invoke() ? sourceAnim.get<UObject*>("ReturnValue") : nullptr;
    UObject* asset = nullptr;
    if (!IsLive(m.source) || !GetProp(m.mesh, "SkeletalMesh", asset) || !IsLive(asset)) {
        summary = "source has no anim instance or mesh asset";
        return false;
    }
    if (!coop::ReadBoneTable(m.mesh, m.bones)) { summary = "cannot read the source bone table"; return false; }
    if (mode == AnimMirrorMode::pawn) {
        m.keepAi = HasOption(options, "ai");
        m.actor = SpawnPawnClone(localPawn, m.pawn, asset, keepController, m.keepAi, summary);
        if (!m.actor) return false;
        GetProp(m.actor, "Mesh", m.component);
        GetProp(m.pawn, "CharacterMovement", m.sourceMove);
        GetProp(m.actor, "CharacterMovement", m.cloneMove);
        if (!IsLive(m.component) || !IsLive(m.sourceMove) || !IsLive(m.cloneMove)) {
            Call(m.actor, "K2_DestroyActor").invoke();
            summary = "pawn clone has no mesh or movement component";
            return false;
        }
        if (!moveTick) Call(m.cloneMove, "SetComponentTickEnabled").setBool("bEnabled", false).invoke();
        if (HasOption(options, "state")) {
            GetProp(m.pawn, "AnimStateMachine", m.sourceStates);
            GetProp(m.actor, "AnimStateMachine", m.cloneStates);
            m.forwardState = IsLive(m.sourceStates) && IsLive(m.cloneStates);
            m.attackData = HasOption(options, "attack-data");
            m.startAttack = HasOption(options, "start-attack");
            m.playOneShot = HasOption(options, "play-oneshot");
            if (m.startAttack) {
                // The attack names the source AI's behavior tree uses (BendBTTask_Attack nodes under its controller).
                UObject* controller = nullptr;
                GetProp(m.pawn, "Controller", controller);
                auto taskClass = FindClass("BendBTTask_Attack");
                std::string names;
                for (int32_t i = 0; taskClass && IsLive(controller) && i < ObjectCount(); ++i) {
                    auto task = ObjectAt(i);
                    if (!IsLive(task) || !IsA(task, taskClass) || IsDefaultOrArchetype(task)) continue;
                    FName name{};
                    if (!GetProp(task, "AttackName", name) || !name.index) continue;
                    bool known = false;
                    for (auto n : m.attackNames) known |= n.index == name.index && n.number == name.number;
                    if (!known) m.attackNames.push_back(name);
                    names += (names.empty() ? "" : ", ") + NameString(name.index) + " (" + Name(task->outer) + ")";
                }
                Log("anim mirror: attack task names: %s", names.empty() ? "none" : names.c_str());
            }
            Log("anim mirror: state machines source %s (%s), clone %s (%s)",
                IsLive(m.sourceStates) ? Name(m.sourceStates).c_str() : "none", CurrentStateName(m.sourceStates).c_str(),
                IsLive(m.cloneStates) ? Name(m.cloneStates).c_str() : "none", CurrentStateName(m.cloneStates).c_str());
        }
    } else {
        FTransform xf{};
        xf.rotation = {0, 0, 0, 1};
        xf.translation = me;
        xf.scale = {1, 1, 1};
        Call begin(statics, "BeginSpawningActorFromClass");
        begin.set("WorldContextObject", localPawn).set("ActorClass", actorClass).set("SpawnTransform", xf).setBool("bNoCollisionFail", true);
        m.actor = begin.invoke() ? begin.get<UObject*>("ReturnValue") : nullptr;
        if (!m.actor) { summary = "cannot spawn the clone actor"; return false; }
        Call(statics, "FinishSpawningActor").set("Actor", m.actor).set("SpawnTransform", xf).invoke();
        if (!GetProp(m.actor, "SkeletalMeshComponent", m.component) || !IsLive(m.component)) {
            Call(m.actor, "K2_DestroyActor").invoke();
            summary = "clone actor has no mesh component";
            return false;
        }
        Call(m.component, "SetSkeletalMesh").set("NewMesh", asset).invoke();
        Call(m.component, "SetAnimInstanceClass").set("NewClass", m.source->cls).invoke();
    }
    Call cloneAnim(m.component, "GetAnimInstance");
    m.clone = cloneAnim.invoke() ? cloneAnim.get<UObject*>("ReturnValue") : nullptr;
    if (!IsLive(m.clone) || m.clone->cls != m.source->cls) {
        Call(m.actor, "K2_DestroyActor").invoke();
        summary = "clone did not get the source anim class " + Name(m.source->cls);
        return false;
    }
    GetProp(m.mesh, "ForcedLodModel", m.sourceForcedLod);
    GetProp(m.mesh, "MeshComponentUpdateFlag", m.sourceUpdateFlag);
    GetBoolProp(m.mesh, "bEnableUpdateRateOptimizations", m.sourceUro);
    PinFullPose(m.mesh, 1, 0, false);
    PinFullPose(m.component, 1, 0, false);
    CollectMirrorFields(m.source->cls, m.fields, m.bytes);
    CollectNodeFields(m);
    if (m.playOneShot) {
        std::string names;
        for (auto s : m.slots) {
            auto text = NameString(s.index);
            names += (names.empty() ? "" : ", ") + text;
            bool full = text.find("Full") != std::string::npos || text.find("full") != std::string::npos;
            if (!m.slot.index || full || (text == "DefaultSlot" && !m.slot.index)) {
                if (full || !m.slot.index) m.slot = s;
            }
        }
        Log("anim mirror: graph slots: %s; playing one-shots into %s", names.empty() ? "none" : names.c_str(),
            m.slot.index ? NameString(m.slot.index).c_str() : "none");
    }
    if (HasOption(options, "intent")) {
        // The locomotion intent a pawn without AI cannot derive from its velocity (lab, 2026-11-06): the
        // requested speed tier, desired direction and turn, and the idle/move transition requests.
        static const std::unordered_set<std::string> names = {
            "Speed", "DesiredSpeedNotSlowedDownForTurn", "SmoothSpeed", "SmoothSpeedNotSlowedDownForTurn",
            "DesiredVelocity_LocalYaw", "DesiredVelocityInterpolated_LocalYaw", "DesiredVelocityInterpolated_Length",
            "DeltaToDesiredRotationYaw", "DeltaToDesiredRotationForPivotTriggerYaw",
            "DeltaToDesiredMovementDirectionYaw", "DeltaToDesiredMovementDirectionForPivotTriggerYaw",
            "DesiredTurnAngleAsFloat", "DesiredSustainedStrafeAngleYaw", "WantsToTransitionIdleToMove",
            "WantsToTransitionMoveToIdleNEW", "WantsToTransitionIdleToTurn", "WantsToTransitionMoveToTurn",
            "RedirectRootMotionYaw", "TorsoPitchAngle"};
        int intentBytes = 0;
        for (const auto& f : m.fields)
            if (names.count(f.name)) {
                m.intent.push_back(f);
                intentBytes += f.size;
            }
        Log("anim mirror: intent set %d fields, %d bytes", static_cast<int>(m.intent.size()), intentBytes);
    }
    uint64_t weakPawn = 0;
    GetProp(m.clone, "BendPawn", weakPawn);
    m.started = m.nextSample = Seconds();
    m.active = true;
    g_mirror = std::move(m);
    PlaceMirror();
    auto clonePawn = ResolveWeak(weakPawn);
    char text[400];
    std::snprintf(text, sizeof text,
                  "%s mirror of %s (%s, %s, %d bones, %.0f m away): %zu variable fields (%d bytes); %d state machines and "
                  "%d asset players, %zu node fields (%d bytes); clone BendPawn=%s; options '%s'",
                  ModeName(mode), Name(g_mirror.pawn).c_str(), Name(g_mirror.source->cls).c_str(), Name(asset).c_str(),
                  static_cast<int>(g_mirror.bones.names.size()), std::sqrt(bestDistance) / 100, g_mirror.fields.size(),
                  g_mirror.bytes, g_mirror.machines, g_mirror.players, g_mirror.nodes.size(), g_mirror.nodeBytes,
                  clonePawn ? Name(clonePawn).c_str() : "null", options.c_str());
    summary = text;
    Log("anim mirror: %s", text);
    return true;
}

bool AnimMirrorStop(std::string& summary) {
    if (!g_mirror.active) { summary = "anim mirror not running"; return false; }
    if (IsLive(g_mirror.actor)) Call(g_mirror.actor, "K2_DestroyActor").invoke();
    if (IsLive(g_mirror.mesh))
        PinFullPose(g_mirror.mesh, g_mirror.sourceForcedLod, g_mirror.sourceUpdateFlag, g_mirror.sourceUro);
    std::vector<float> active(g_mirror.activePlayers.begin(), g_mirror.activePlayers.end());
    char text[320];
    std::snprintf(text, sizeof text,
                  "%s mirror stopped after %.1f s: %zu samples, mean bone error median %.1f deg p90 %.1f deg; "
                  "dominant action match %d/%d; active asset players median %.0f max %.0f of %d; %llu copies",
                  ModeName(g_mirror.mode), Seconds() - g_mirror.started, g_mirror.meanErrors.size(),
                  Percentile(g_mirror.meanErrors, 0.5f), Percentile(g_mirror.meanErrors, 0.9f),
                  g_mirror.actionMatches, g_mirror.actionSamples,
                  Percentile(active, 0.5f), Percentile(active, 1.f), g_mirror.players,
                  static_cast<unsigned long long>(g_mirror.copies));
    summary = text;
    Log("anim mirror: %s", text);
    if (g_mirror.forwardState)
        Log("anim mirror: %d source state changes, %d clone requests accepted", g_mirror.stateChanges,
            g_mirror.stateRequestsOk);
    if (g_mirror.playOneShot)
        Log("anim mirror: %d one-shots seen, %d played on the clone; %d of %d one-shot samples matched",
            g_mirror.oneShots, g_mirror.oneShotsPlayed, g_mirror.oneShotMatches, g_mirror.oneShotSamples);
    {
        std::string hits;
        for (const auto& [party, tally] : g_mirror.damage)
            hits += (hits.empty() ? "" : ", ") + party + " " + std::to_string(tally.hits) + " hits/" +
                    std::to_string(static_cast<int>(tally.total));
        auto list = [](const std::map<std::string, int>& counts) {
            std::string text;
            for (const auto& [name, count] : counts)
                text += (text.empty() ? "" : ", ") + name + "=" + std::to_string(count);
            return text.empty() ? std::string("none") : text;
        };
        Log("anim mirror: player damage: %s; %d heals; %d clone notify events muted; counter events %d passed, %d muted",
            hits.empty() ? "none" : hits.c_str(), g_mirror.heals, g_mirror.mutedNotifies, g_mirror.countersPassed,
            g_mirror.countersMuted);
        Log("anim mirror: source notifies: %s", list(g_mirror.sourceNotifies).c_str());
        Log("anim mirror: clone notifies: %s", list(g_mirror.cloneNotifies).c_str());
        if (g_mirror.watchDeath) Log("anim mirror: death events: %s", list(g_mirror.deathEvents).c_str());
        if (g_mirror.autoCounter || g_mirror.watchCounter)
            Log("anim mirror: %d counter presses, %d successes; counter events: %s", g_mirror.counterTries,
                g_mirror.counterSuccesses, list(g_mirror.counterEvents).c_str());
        for (const auto& [notify, calls] : g_mirror.notifyCalls)
            Log("anim mirror: %s calls: %s", notify.c_str(), list(calls).c_str());
    }
    if (!g_mirror.divergent.empty()) {
        std::vector<std::pair<int, std::string>> ranked;
        for (const auto& [name, count] : g_mirror.divergent) ranked.push_back({count, name});
        std::sort(ranked.rbegin(), ranked.rend());
        std::string list;
        for (size_t i = 0; i < ranked.size() && i < 40; ++i)
            list += (i ? ", " : "") + ranked[i].second + "=" + std::to_string(ranked[i].first);
        Log("anim mirror: %d variables diverged (of %d samples): %s", static_cast<int>(ranked.size()),
            static_cast<int>(g_mirror.meanErrors.size()), list.c_str());
    }
    g_mirror = {};
    return true;
}

// The scripted counter press: the F key (counter-key) or the left mouse button (counter-click).
bool SendCounterInput(bool down) {
    INPUT input{};
    if (g_mirror.counterClick) {
        input.type = INPUT_MOUSE;
        input.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
    } else {
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = 'F';
        input.ki.wScan = static_cast<WORD>(MapVirtualKeyW('F', MAPVK_VK_TO_VSC));
        if (!down) input.ki.dwFlags = KEYEVENTF_KEYUP;
    }
    return SendInput(1, &input, sizeof input) == 1;
}

void AnimMirrorTick() {
    if (!g_mirror.active) return;
    if (!IsLive(g_mirror.pawn) || !IsLive(g_mirror.mesh) || !IsLive(g_mirror.source) || !IsLive(g_mirror.actor) ||
        !IsLive(g_mirror.component) || !IsLive(g_mirror.clone)) {
        std::string summary;
        Log("anim mirror: source or clone went away");
        AnimMirrorStop(summary);
        return;
    }
    // Breadcrumbs for the first tick only: a crash here leaves the last completed step in the log.
    bool first = g_mirror.copies == 0 && g_mirror.meanErrors.empty();
    if (first) Log("anim mirror: first tick, placing clone");
    if (g_mirror.mode == AnimMirrorMode::pawn && g_mirror.keepAi) {
        ++g_mirror.copies;
    } else if (g_mirror.mode == AnimMirrorMode::pawn) {
        if (!g_mirror.keepClaws && g_mirror.clawsDisabled <= 0 && Seconds() - g_mirror.started < 1.0) {
            int disabled = DisableCloneWeapons(g_mirror.actor);
            if (disabled != g_mirror.clawsDisabled) Log("anim mirror: clone weapons with collision off: %d", disabled);
            g_mirror.clawsDisabled = disabled;
        }
        FeedPawnClone();
        if (g_mirror.copyVars) CopyFields(g_mirror.fields);
        if (!g_mirror.intent.empty()) CopyFields(g_mirror.intent);
        if (g_mirror.forwardState && IsLive(g_mirror.sourceStates) && IsLive(g_mirror.cloneStates)) ForwardState();
        if (g_mirror.playOneShot) FollowOneShots();
        if (g_mirror.killCloneAt > 0 && Seconds() >= g_mirror.killCloneAt) {
            g_mirror.killCloneAt = 0;
            float health = 0, sourceHealth = 0;
            GetProp(g_mirror.actor, "Health", health);
            GetProp(g_mirror.pawn, "Health", sourceHealth);
            Call apply(FindDefaultObject("GameplayStatics"), "ApplyDamage");
            apply.set("DamagedActor", g_mirror.actor).set("BaseDamage", health + 10.f)
                .set("EventInstigator", static_cast<UObject*>(nullptr)).set("DamageCauser", static_cast<UObject*>(nullptr));
            bool ok = apply.invoke(), damageable = false;
            GetBoolProp(g_mirror.actor, "bCanBeDamaged", damageable);
            float after = health;
            GetProp(g_mirror.actor, "Health", after);
            Log("anim mirror death: kill-clone ApplyDamage(%.1f) %s -> %.1f, clone health %.1f, bCanBeDamaged=%d; "
                "source health %.1f", health + 10.f, ok ? "called" : "failed", ok ? apply.get<float>("ReturnValue") : 0.f,
                after, damageable, sourceHealth);
            // ApplyDamage is refused on the clone; BendPawn.ForceKill is the native kill.
            if (after > 0) {
                Call kill(g_mirror.actor, "ForceKill");
                ok = kill.set("Killer", static_cast<UObject*>(nullptr)).setBool("forceRagdoll", false).invoke();
                GetProp(g_mirror.actor, "Health", after);
                Log("anim mirror death: kill-clone ForceKill %s, clone health %.1f", ok ? "called" : "failed", after);
            }
        }
        if (g_mirror.counterAt > 0 && Seconds() >= g_mirror.counterAt) {
            g_mirror.counterAt = 0;
            UObject* controller = nullptr;
            bool attempting = false, called = false;
            if (g_mirror.counterKey || g_mirror.counterClick) {
                called = SendCounterInput(true);
                g_mirror.keyUpAt = Seconds() + 0.1;
            } else {
                called = GetProp(g_mirror.player, "Controller", controller) && IsLive(controller) &&
                         Call(controller, "TryToCounterAttack").invoke();
            }
            if (called) GetBoolProp(controller, "IsAttemptingCounter", attempting);
            ++g_mirror.counterTries;
            Log("anim mirror counter: %s in the %s window: %s, IsAttemptingCounter=%d",
                g_mirror.counterClick ? "left button down" : g_mirror.counterKey ? "F key down" : "TryToCounterAttack",
                g_mirror.counterWindow.c_str(),
                called ? "called" : "failed", attempting);
        }
        if (g_mirror.keyUpAt > 0 && Seconds() >= g_mirror.keyUpAt) {
            g_mirror.keyUpAt = 0;
            SendCounterInput(false);
            UObject* controller = nullptr;
            bool attempting = false;
            if (GetProp(g_mirror.player, "Controller", controller) && IsLive(controller))
                GetBoolProp(controller, "IsAttemptingCounter", attempting);
            Log("anim mirror counter: %s up; IsAttemptingCounter=%d", g_mirror.counterClick ? "left button" : "F key",
                attempting);
        }
        ++g_mirror.copies;
    } else {
        PlaceMirror();
    }
    if (first) Log("anim mirror: first tick, clone placed");
    // One swarmer grapple hit (88) outright killed a 78-health player in the lab, so restoring at half health is
    // too late: the player is held at kHealBuffer instead, and every hit still reports its full damage.
    constexpr float kHealBuffer = 1000.f;
    float health = 0;
    if (g_mirror.heal && IsLive(g_mirror.player) && GetProp(g_mirror.player, "Health", health) &&
        health < kHealBuffer) {
        if (g_mirror.heals++ == 0) Log("anim mirror: holding player health at %.0f (was %.1f)", kHealBuffer, health);
        SetPlayerHealth(kHealBuffer);
    }
    if (g_mirror.mode == AnimMirrorMode::variables || g_mirror.mode == AnimMirrorMode::nodes) {
        CopyFields(g_mirror.fields);
        if (g_mirror.mode == AnimMirrorMode::nodes) {
            if (first) Log("anim mirror: first tick, copying node state");
            CopyFields(g_mirror.nodes);
        }
        ++g_mirror.copies;
    }
    double now = Seconds();
    if (now < g_mirror.nextSample) return;
    g_mirror.nextSample = now + 0.5;
    if (first) Log("anim mirror: first sample, reading bones");
    float sum = 0, worst = 0;
    for (auto bone : g_mirror.bones.names) {
        float e = AngleDegrees(BoneRotation(g_mirror.mesh, bone), BoneRotation(g_mirror.component, bone));
        sum += e;
        worst = std::max(worst, e);
    }
    float mean = g_mirror.bones.names.empty() ? 0 : sum / static_cast<float>(g_mirror.bones.names.size());
    g_mirror.meanErrors.push_back(mean);
    // GetCurrentStateName takes an anim-node property index, not a state machine number; index 0 of
    // SwarmerBVim_C crashed the game (lab, 2026-11-06), so states are not compared.
    float speed = 0, cloneSpeed = 0;
    uint8_t aware = 0, cloneAware = 0;
    int32_t sourceLod = -1, cloneLod = -1;
    GetProp(g_mirror.source, "Speed", speed);
    GetProp(g_mirror.clone, "Speed", cloneSpeed);
    GetProp(g_mirror.source, "AwareState", aware);
    GetProp(g_mirror.clone, "AwareState", cloneAware);
    GetProp(g_mirror.mesh, "PredictedLODLevel", sourceLod);
    GetProp(g_mirror.component, "PredictedLODLevel", cloneLod);
    int active = ActivePlayers();
    g_mirror.activePlayers.push_back(active);
    auto sourceAction = DominantAsset(g_mirror.source), cloneAction = DominantAsset(g_mirror.clone);
    // A one-shot replayed as a montage plays through a slot, not an asset player, so DominantAsset cannot see it:
    // the clone's action is the montage's asset while that montage plays.
    bool montage = false;
    if (g_mirror.playOneShot && IsLive(g_mirror.lastMontage)) {
        Call playing(g_mirror.clone, "Montage_IsPlaying");
        montage = playing.set("Montage", g_mirror.lastMontage).invoke() && playing.get<bool>("ReturnValue");
        if (montage) cloneAction = g_mirror.lastOneShot;
    }
    if (g_mirror.playOneShot && sourceAction && sourceAction == g_mirror.lastOneShot) {
        ++g_mirror.oneShotSamples;
        if (cloneAction == sourceAction) ++g_mirror.oneShotMatches;
    }
    ++g_mirror.actionSamples;
    if (sourceAction == cloneAction) ++g_mirror.actionMatches;
    Log("anim mirror action: %s / %s%s%s; bone error %.1f", sourceAction ? Name(sourceAction).c_str() : "-",
        cloneAction ? Name(cloneAction).c_str() : "-", montage ? " [montage]" : "",
        sourceAction == cloneAction ? " (match)" : "", mean);
    if (g_mirror.mode == AnimMirrorMode::pawn && !g_mirror.copyVars && g_mirror.intent.empty()) {
        // Variables the clone's own anim update does not reproduce from position, velocity and movement mode:
        // what a native-style replicator would still have to send. Floats within 5% count as equal.
        auto from = reinterpret_cast<const uint8_t*>(g_mirror.source);
        auto to = reinterpret_cast<const uint8_t*>(g_mirror.clone);
        for (const auto& f : g_mirror.fields) {
            if (f.object) continue;
            bool differs;
            if (f.real) {
                float a = 0, b = 0;
                std::memcpy(&a, from + f.offset, 4);
                std::memcpy(&b, to + f.offset, 4);
                differs = std::fabs(a - b) > 0.05f * std::max(1.f, std::fabs(a));
            } else {
                differs = std::memcmp(from + f.offset, to + f.offset, static_cast<size_t>(f.size)) != 0;
            }
            if (differs) ++g_mirror.divergent[f.name];
        }
    }
    if (g_mirror.mode == AnimMirrorMode::pawn) {
        auto length =[](const FVector& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); };
        FVector animVelocity{}, moveVelocity{}, sourceMoveVelocity{};
        uint64_t weakMove = 0, weakController = 0;
        UObject* controller = nullptr;
        bool moveTicks = false;
        GetProp(g_mirror.clone, "Velocity", animVelocity);
        GetProp(g_mirror.cloneMove, "Velocity", moveVelocity);
        GetProp(g_mirror.sourceMove, "Velocity", sourceMoveVelocity);
        GetProp(g_mirror.clone, "BendCharacterMovement", weakMove);
        GetProp(g_mirror.clone, "BendController", weakController);
        GetProp(g_mirror.actor, "Controller", controller);
        Call ticking(g_mirror.cloneMove, "IsComponentTickEnabled");
        if (ticking.invoke()) moveTicks = ticking.get<bool>("ReturnValue");
        Log("anim mirror pawn: move velocity %.0f (source %.0f), anim velocity %.0f; anim movement %s, anim "
            "controller %s, pawn controller %s, move tick %d",
            length(moveVelocity), length(sourceMoveVelocity), length(animVelocity),
            ResolveWeak(weakMove) ? "set" : "null", ResolveWeak(weakController) ? "set" : "null",
            IsLive(controller) ? Name(controller).c_str() : "none", moveTicks);
    }
    Log("anim mirror +%.1fs: bone error mean %.1f max %.1f deg; speed %.0f (clone %.0f) aware %u (clone %u); "
        "lod %d/%d; active players %d",
        now - g_mirror.started, mean, worst, speed, cloneSpeed, aware, cloneAware, sourceLod, cloneLod, active);
}


namespace {
// UArrayProperty::Inner: the first member after UProperty, the same slot as UStructProperty::Struct (0x78).
UObject* ArrayInner(UObject* property) {
    constexpr int kInnerOffset = 0x78;
    UObject* inner = nullptr;
    if (!ReadableMemory(reinterpret_cast<uint8_t*>(property) + kInnerOffset, sizeof inner)) return nullptr;
    std::memcpy(&inner, reinterpret_cast<uint8_t*>(property) + kInnerOffset, sizeof inner);
    if (!IsLive(inner) || !IsLive(inner->cls)) return nullptr;
    auto kind = Name(inner->cls);
    return kind.size() > 8 && kind.ends_with("Property") ? inner : nullptr;
}

bool HoldsPointer(const uint8_t* data, size_t bytes, const UObject* target) {
    if (!bytes || !ReadableMemory(data, bytes)) return false;
    for (size_t at = 0; at + 8 <= bytes; at += 8) {
        UObject* value = nullptr;
        std::memcpy(&value, data + at, sizeof value);
        if (value == target) return true;
    }
    return false;
}
} // namespace

// asset-refs NAME: every live object called NAME with its full path and flags, and every object whose
// reflected fields (including the elements of its arrays) hold a pointer to one of them.
bool AssetReferences(const std::string& name, std::string& summary) {
    std::vector<UObject*> targets;
    int32_t number = 0;
    int32_t index = SplitNameNumber(name, number);
    for (int32_t i = 0; index >= 0 && i < ObjectCount(); ++i) {
        auto o = ObjectAt(i);
        if (o && o->name.index == index && o->name.number == number && IsLive(o)) targets.push_back(o);
    }
    if (targets.empty()) {
        summary = "no live object named " + name;
        return false;
    }
    for (auto t : targets) {
        std::string chain;
        for (auto o = t->outer; o && chain.size() < 300; o = o->outer) chain += (chain.empty() ? "" : " <- ") + Name(o->cls) + " " + Name(o);
        Log("asset-refs: %s (flags 0x%x); outers: %s", FullName(t).c_str(), t->flags, chain.empty() ? "none" : chain.c_str());
    }
    // Per class: its reflected fields, object-sized ones and arrays.
    struct Field { int offset, size; UObject* property; bool array; int stride; };
    std::unordered_map<UObject*, std::vector<Field>> layouts;
    int hits = 0, scanned = 0;
    for (int32_t i = 0; i < ObjectCount() && hits < 60; ++i) {
        auto o = ObjectAt(i);
        if (!o || !IsLive(o) || !IsLive(o->cls) || std::find(targets.begin(), targets.end(), o) != targets.end()) continue;
        auto [it, fresh] = layouts.try_emplace(o->cls);
        if (fresh) {
            for (auto c = o->cls; IsLive(c); c = Super(c)) {
                int budget = 0;
                for (auto f = Children(c); f && budget < 4096; f = Next(f), ++budget) {
                    if (!IsLive(f) || IsFunction(f)) continue;
                    int offset = PropertyOffset(f), size = ElementSize(f);
                    if (offset < 0 || size <= 0) continue;
                    auto kind = Name(f->cls);
                    if (kind == "ArrayProperty") {
                        auto inner = ArrayInner(f);
                        if (inner) it->second.push_back({offset, size, f, true, ElementSize(inner)});
                    } else if (kind == "ObjectProperty" || kind == "StructProperty") {
                        it->second.push_back({offset, size, f, false, 0});
                    }
                }
            }
        }
        ++scanned;
        auto base = reinterpret_cast<const uint8_t*>(o);
        for (const auto& f : it->second) {
            bool found = false;
            if (f.array) {
                struct Raw { const uint8_t* data; int32_t count, capacity; } raw{};
                std::memcpy(&raw, base + f.offset, sizeof raw);
                size_t bytes = raw.data && raw.count > 0 && raw.count < 100000 && f.stride > 0 && f.stride <= 4096
                                   ? static_cast<size_t>(raw.count) * static_cast<size_t>(f.stride) : 0;
                for (auto t : targets) found = found || HoldsPointer(raw.data, bytes, t);
            } else {
                for (auto t : targets) found = found || HoldsPointer(base + f.offset, static_cast<size_t>(f.size), t);
            }
            if (!found) continue;
            Log("asset-refs: held by %s field %s%s", FullName(o).c_str(), Name(f.property).c_str(), f.array ? " (array)" : "");
            ++hits;
            break;
        }
    }
    summary = std::to_string(targets.size()) + " objects named " + name + ", " + std::to_string(hits) +
              " referencing objects among " + std::to_string(scanned) + " scanned (first 60 shown)";
    return true;
}


namespace {
constexpr size_t kSpawnTypeSize = 1056; // BendAmbientSpawnType (reflection, build 19221447)
constexpr int kSpawnTypePawnClass = 272; // m_PawnClass, an AssetClassProperty (TAssetPtr: weak 8, tag 4+4, FString 16)
constexpr int kSpawnTypeFaction = 0, kSpawnTypeCanJoinCrowd = 1016, kSpawnTypeMaxSpawns = 984;

std::string AssetPtrPath(const uint8_t* at) {
    struct { const wchar_t* data; int32_t num, max; } text{};
    std::memcpy(&text, at + 16, sizeof text);
    if (!text.data || text.num <= 1 || text.num > 512 || !ReadableMemory(text.data, static_cast<size_t>(text.num) * 2)) return {};
    std::string out;
    for (int i = 0; i + 1 < text.num; ++i) out.push_back(text.data[i] < 128 ? static_cast<char>(text.data[i]) : '?');
    return out;
}

struct SpawnTypeRef { UObject* owner; std::string field; const uint8_t* data; };

// Live BendAmbientSpawnType values: struct fields and array elements of every live object.
std::vector<SpawnTypeRef> FindSpawnTypes(size_t limit) {
    std::vector<SpawnTypeRef> found;
    auto scriptStruct = FindClass("ScriptStruct");
    auto spawnType = FindObject("BendAmbientSpawnType", scriptStruct);
    if (!spawnType) return found;
    struct Field { int offset; bool array; std::string name; };
    std::unordered_map<UObject*, std::vector<Field>> layouts;
    for (int32_t i = 0; i < ObjectCount() && found.size() < limit; ++i) {
        auto o = ObjectAt(i);
        if (!o || !IsLive(o) || !IsLive(o->cls) || o->cls == scriptStruct) continue;
        auto [it, fresh] = layouts.try_emplace(o->cls);
        if (fresh) {
            for (auto c = o->cls; IsLive(c); c = Super(c)) {
                int budget = 0;
                for (auto f = Children(c); f && budget < 4096; f = Next(f), ++budget) {
                    if (!IsLive(f) || IsFunction(f) || PropertyOffset(f) < 0) continue;
                    auto kind = Name(f->cls);
                    if (kind == "StructProperty" && PropertySubclassObject(f, "ScriptStruct") == spawnType)
                        it->second.push_back({PropertyOffset(f), false, Name(f)});
                    else if (kind == "ArrayProperty")
                        if (auto inner = ArrayInner(f); inner && Name(inner->cls) == "StructProperty" &&
                            PropertySubclassObject(inner, "ScriptStruct") == spawnType)
                            it->second.push_back({PropertyOffset(f), true, Name(f)});
                }
            }
        }
        auto base = reinterpret_cast<const uint8_t*>(o);
        for (const auto& f : it->second) {
            if (!f.array) {
                found.push_back({o, f.name, base + f.offset});
                continue;
            }
            struct { const uint8_t* data; int32_t count, capacity; } raw{};
            std::memcpy(&raw, base + f.offset, sizeof raw);
            if (!raw.data || raw.count <= 0 || raw.count > 4096 ||
                !ReadableMemory(raw.data, static_cast<size_t>(raw.count) * kSpawnTypeSize)) continue;
            for (int32_t k = 0; k < raw.count && found.size() < limit; ++k)
                found.push_back({o, f.name + "[" + std::to_string(k) + "]", raw.data + static_cast<size_t>(k) * kSpawnTypeSize});
        }
    }
    return found;
}
} // namespace

// spawn-types: the live spawn types the ambient spawner can use, by pawn class.
bool SpawnTypes(std::string& summary) {
    auto types = FindSpawnTypes(4000);
    std::map<std::string, int> byClass;
    int logged = 0;
    for (const auto& t : types) {
        auto path = AssetPtrPath(t.data + kSpawnTypePawnClass);
        auto shortName = path.substr(path.rfind('.') == std::string::npos ? 0 : path.rfind('.') + 1);
        if (++byClass[shortName] <= 2 && logged++ < 60) {
            int32_t max = 0;
            std::memcpy(&max, t.data + kSpawnTypeMaxSpawns, 4);
            Log("spawn-types: %s.%s: pawn %s, faction %u, can join crowd %u, max %d", FullName(t.owner).c_str(),
                t.field.c_str(), path.empty() ? "none" : path.c_str(), t.data[kSpawnTypeFaction],
                t.data[kSpawnTypeCanJoinCrowd], max);
        }
    }
    std::string list;
    for (const auto& [cls, n] : byClass) list += (list.empty() ? "" : ", ") + (cls.empty() ? std::string("none") : cls) + "=" + std::to_string(n);
    Log("spawn-types: %d spawn types by pawn class: %s", static_cast<int>(types.size()), list.c_str());
    summary = std::to_string(types.size()) + " spawn types";
    return !types.empty();
}

// spawn-horde [MODE [PAWN [COUNT]]]: spawn enemies next to the player through the world's BendAmbientGenerator,
// with a copy of a live spawn type whose pawn class is PAWN_C (default HordeSwarmerPawn), COUNT (default 20).
// MODE: arcade (SpawnForArcadeMode, 20 m ahead), scream (ScreamerSpawn, what a screamer's call uses) or
// escalate (SpawnForCombatEscalation within 40 m; no spawn type).
bool SpawnHorde(UObject* localPawn, const std::string& arg, std::string& summary) {
    std::string mode = "arcade", want = "HordeSwarmerPawn";
    int count = 20;
    {
        std::istringstream words(arg);
        std::string m, w;
        int c = 0;
        if (words >> m) mode = m;
        if (words >> w) want = w;
        if (words >> c) count = std::max(1, std::min(100, c));
    }
    if (mode != "arcade" && mode != "horde" && mode != "scream" && mode != "escalate") {
        summary = "mode must be arcade, horde, scream or escalate";
        return false;
    }
    // horde: arcade spawning into a live crowd group, as the horde members that a group drives.
    UObject* group = nullptr;
    if (mode == "horde") {
        for (const char* name : {"BendCrowdGroupHorde", "BendCrowdGroup"}) {
            auto groupClass = FindClass(name);
            for (int32_t i = 0; groupClass && i < ObjectCount() && !group; ++i) {
                auto o = ObjectAt(i);
                if (o && IsLive(o) && !IsDefaultOrArchetype(o) && IsA(o, groupClass) && Name(o->cls).find("Global") == std::string::npos)
                    group = o;
            }
            if (group) break;
        }
        if (!group) { summary = "no live crowd group"; return false; }
    }
    auto types = FindSpawnTypes(4000);
    // The exact pawn class ("SwarmerPawn" must not pick HordeSwarmerPawn); a world-placed spawn type (not a
    // class default or arcade data) is preferred, since its assets are what this world loaded.
    const SpawnTypeRef* pick = nullptr;
    for (const auto& t : types) {
        auto path = AssetPtrPath(t.data + kSpawnTypePawnClass);
        if (!path.ends_with("." + want + "_C")) continue;
        bool world = !IsDefaultOrArchetype(t.owner) && FullName(t.owner).find("PersistentLevel") != std::string::npos;
        if (!pick || world) pick = &t;
        if (world) break;
    }
    if (!pick && mode != "escalate") { summary = "no live spawn type with a pawn class containing " + want; return false; }
    auto generatorClass = FindClass("BendAmbientGenerator");
    UObject* generator = nullptr;
    for (int32_t i = 0; generatorClass && i < ObjectCount() && !generator; ++i) {
        auto o = ObjectAt(i);
        if (o && IsLive(o) && !IsDefaultOrArchetype(o) && IsA(o, generatorClass)) generator = o;
    }
    if (!generator) { summary = "no live BendAmbientGenerator"; return false; }
    Call where(localPawn, "K2_GetActorLocation"), facing(localPawn, "K2_GetActorRotation");
    if (!where.invoke() || !facing.invoke()) { summary = "no player transform"; return false; }
    auto at = where.get<FVector>("ReturnValue");
    float yaw = facing.get<FRotator>("ReturnValue").yaw * 3.14159265f / 180.f;
    at.x += std::cos(yaw) * 2000.f;
    at.y += std::sin(yaw) * 2000.f;
    if (mode == "escalate") {
        Call escalate(generator, "SpawnForCombatEscalation");
        bool ok = escalate.set("CenterLocation", where.get<FVector>("ReturnValue")).set("Radius", 4000.f).invoke();
        summary = std::string("SpawnForCombatEscalation ") + (ok ? "called" : "failed") + ", returned " +
                  (ok && escalate.get<bool>("ReturnValue") ? "true" : "false");
        return ok;
    }
    struct Raw { uint8_t bytes[kSpawnTypeSize]; } type{};
    std::memcpy(type.bytes, pick->data, kSpawnTypeSize); // the native thunk copy-constructs it; this buffer is never destroyed
    if (mode == "scream") {
        Call scream(generator, "ScreamerSpawn");
        bool ok = scream.set("SpawnType", type).set("spawnCount", static_cast<int32_t>(count)).invoke();
        summary = std::string("ScreamerSpawn ") + (ok ? "called" : "failed") + " with " + std::to_string(count) + " of " +
                  AssetPtrPath(pick->data + kSpawnTypePawnClass) + " from " + FullName(pick->owner) + "." + pick->field;
        return ok;
    }
    Call spawn(generator, "SpawnForArcadeMode");
    spawn.set("SpawnType", type).set("Location", at).set("Rotation", FRotator{0, 0, 0}).set("Count", static_cast<int32_t>(count))
        .set("DistanceToDespawn", 30000.f).set("spawnerID", static_cast<uint8_t>(0)).setBool("bForceChasePlayer", true)
        .setBool("bCanDespawnByInactivity", false).set("Group", group);
    bool ok = spawn.invoke();
    if (group) Log("spawn-horde: into crowd group %s", FullName(group).c_str());
    summary = std::string(ok ? "requested " : "failed to request ") + std::to_string(count) + " from " + FullName(pick->owner) + "." +
              pick->field + " (" + AssetPtrPath(pick->data + kSpawnTypePawnClass) + "), returned " +
              (ok && spawn.get<bool>("ReturnValue") ? "true" : "false");
    return ok;
}


namespace {
FVector ActorLocation(UObject* actor) {
    Call where(actor, "K2_GetActorLocation");
    return where.invoke() ? where.get<FVector>("ReturnValue") : FVector{};
}

float Distance(const FVector& a, const FVector& b) {
    float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

int IntCall(UObject* o, const char* function) {
    Call c(o, function);
    return c.invoke() ? c.get<int32_t>("ReturnValue") : -1;
}

std::vector<UObject*> ObjectList(UObject* o, const char* name) {
    std::vector<UObject*> result;
    auto property = IsLive(o) ? FindChild(o->cls, name) : nullptr;
    auto inner = property && Name(property->cls) == "ArrayProperty" ? ArrayInner(property) : nullptr;
    if (!inner || Name(inner->cls) != "ObjectProperty") return result;
    struct { UObject** data; int32_t count, capacity; } raw{};
    std::memcpy(&raw, reinterpret_cast<uint8_t*>(o) + PropertyOffset(property), sizeof raw);
    if (!raw.data || raw.count <= 0 || raw.count > 1024 ||
        !ReadableMemory(raw.data, static_cast<size_t>(raw.count) * sizeof(UObject*))) return result;
    result.assign(raw.data, raw.data + raw.count);
    return result;
}
} // namespace

// wake-horde [info | generate N | manager N | activate | frenzy]: the nearest non-global crowd group (horde
// groups first) with its generators and controller; generate asks its first generator for N members 20 m ahead of
// the player that chase the player; manager asks the crowd manager for N members of the group there; activate
// turns every generator on with a 300 m activation radius; frenzy sets the group's controller frenzied.
bool WakeHorde(UObject* localPawn, const std::string& arg, std::string& summary) {
    std::string mode = "info";
    int count = 10;
    {
        std::istringstream words(arg);
        std::string m;
        int c = 0;
        if (words >> m) mode = m;
        if (words >> c) count = std::max(1, std::min(60, c));
    }
    if (mode != "info" && mode != "generate" && mode != "manager" && mode != "activate" && mode != "frenzy") {
        summary = "mode must be info, generate, manager, activate or frenzy";
        return false;
    }
    auto me = ActorLocation(localPawn);
    UObject* group = nullptr;
    float best = 1e30f;
    for (const char* name : {"BendCrowdGroupHorde", "BendCrowdGroup"}) {
        auto groupClass = FindClass(name);
        for (int32_t i = 0; groupClass && i < ObjectCount(); ++i) {
            auto o = ObjectAt(i);
            if (!o || !IsLive(o) || IsDefaultOrArchetype(o) || !IsA(o, groupClass) ||
                Name(o->cls).find("Global") != std::string::npos) continue;
            float d = Distance(ActorLocation(o), me);
            if (d < best) best = d, group = o;
        }
        if (group) break;
    }
    if (!group) { summary = "no live crowd group"; return false; }
    Call controllerOf(group, "GetController");
    UObject* controller = controllerOf.invoke() ? controllerOf.get<UObject*>("ReturnValue") : nullptr;
    auto generators = ObjectList(group, "Generators");
    Log("wake-horde: group %s at %.0f m: members %d, alive %d, ever spawned %d, died %d; controller %s; %d generators",
        FullName(group).c_str(), best / 100, IntCall(group, "GetNumMembers"), IntCall(group, "GetNumMembersAlive"),
        IntCall(group, "GetNumMembersEverSpawned"), IntCall(group, "GetNumMembersDied"),
        IsLive(controller) ? FullName(controller).c_str() : "none", static_cast<int>(generators.size()));
    for (size_t k = 0; k < generators.size() && k < 12; ++k) {
        auto g = generators[k];
        if (!IsLive(g)) continue;
        Call active(g, "IsActive"), paused(g, "IsPaused"), radius(g, "GetActivationRadius");
        Log("wake-horde: generator %s (%s) at %.0f m: active %d, paused %d, activation radius %.0f m", Name(g).c_str(),
            Name(g->cls).c_str(), Distance(ActorLocation(g), me) / 100, active.invoke() && active.get<bool>("ReturnValue"),
            paused.invoke() && paused.get<bool>("ReturnValue"), radius.invoke() ? radius.get<float>("ReturnValue") / 100 : -1.f);
    }
    UObject* manager = nullptr;
    if (auto managerClass = FindClass("CrowdManagerActor"))
        for (int32_t i = 0; i < ObjectCount() && !manager; ++i) {
            auto o = ObjectAt(i);
            if (o && IsLive(o) && !IsDefaultOrArchetype(o) && IsA(o, managerClass)) manager = o;
        }
    if (manager) {
        Call frenzied(manager, "AreAnyCrowdMembersFrenzied");
        Log("wake-horde: crowd manager %s: max active members %d, any frenzied %d", FullName(manager).c_str(),
            IntCall(manager, "GetMaxCrowdMembersActive"), frenzied.invoke() && frenzied.get<bool>("ReturnValue"));
    }
    if (mode == "manager") {
        if (!manager) { summary = "no crowd manager"; return false; }
        Call facing(localPawn, "K2_GetActorRotation");
        float yaw = facing.invoke() ? facing.get<FRotator>("ReturnValue").yaw : 0.f;
        float rad = yaw * 3.14159265f / 180.f;
        // FTransform: rotation quaternion, translation, scale, each padded to 16 bytes.
        struct alignas(16) Transform { float q[4]; float t[4]; float s[4]; };
        int called = 0;
        for (int k = 0; k < count; ++k) {
            Transform tr{{0, 0, std::sin((rad + 3.14159265f) / 2), std::cos((rad + 3.14159265f) / 2)},
                         {me.x + std::cos(rad) * 2000.f + (k % 5) * 150.f, me.y + std::sin(rad) * 2000.f + (k / 5) * 150.f, me.z, 0},
                         {1, 1, 1, 0}};
            called += Call(manager, "GenerateCrowdMember").set("Transform", tr).set("Group", group).invoke() ? 1 : 0;
        }
        Log("wake-horde: after manager generate: members %d, alive %d, ever spawned %d", IntCall(group, "GetNumMembers"),
            IntCall(group, "GetNumMembersAlive"), IntCall(group, "GetNumMembersEverSpawned"));
        summary = "CrowdManagerActor.GenerateCrowdMember called " + std::to_string(called) + " of " + std::to_string(count) + " times";
        return called > 0;
    }
    if (mode == "info") {
        summary = FullName(group) + " at " + std::to_string(static_cast<int>(best / 100)) + " m";
        return true;
    }
    if (mode == "frenzy") {
        if (!IsLive(controller)) { summary = "the group has no controller"; return false; }
        bool ok = Call(controller, "ForceAllFrenzied").setBool("bEnable", true).invoke();
        Call(controller, "SuppressAmbientWanderBehavior").setBool("bSuppress", false).invoke();
        summary = std::string("ForceAllFrenzied ") + (ok ? "called" : "failed") + " on " + FullName(controller);
        return ok;
    }
    if (mode == "activate") {
        int done = 0;
        for (auto g : generators) {
            if (!IsLive(g)) continue;
            Call(g, "SetActivationRadius").set("Radius", 30000.f).invoke();
            done += Call(g, "SetActive").setBool("bActive", true).invoke() ? 1 : 0;
            if (FindChild(g->cls, "StartUp")) Call(g, "StartUp").invoke();
            Call(g, "SetPaused").setBool("bPaused", false).invoke();
        }
        summary = "activated " + std::to_string(done) + " of " + std::to_string(generators.size()) + " generators";
        return done > 0;
    }
    UObject* generator = nullptr;
    for (auto g : generators) if (IsLive(g)) { generator = g; break; }
    if (!generator) { summary = "the group has no live generator"; return false; }
    Call facing(localPawn, "K2_GetActorRotation");
    float yaw = facing.invoke() ? facing.get<FRotator>("ReturnValue").yaw : 0.f;
    float rad = yaw * 3.14159265f / 180.f;
    int called = 0;
    for (int k = 0; k < count; ++k) {
        FVector at{me.x + std::cos(rad) * 2000.f + (k % 5) * 150.f, me.y + std::sin(rad) * 2000.f + (k / 5) * 150.f, me.z};
        called += Call(generator, "GenerateCrowdMember").set("Location", at).set("Rotation", FRotator{0, yaw + 180.f, 0})
                      .setBool("bSpawnImmediately", true).set("SystemID", static_cast<int32_t>(0))
                      .setBool("bForceChasePlayer", true).invoke() ? 1 : 0;
    }
    Log("wake-horde: after generate: members %d, alive %d, ever spawned %d", IntCall(group, "GetNumMembers"),
        IntCall(group, "GetNumMembersAlive"), IntCall(group, "GetNumMembersEverSpawned"));
    summary = "GenerateCrowdMember called " + std::to_string(called) + " of " + std::to_string(count) + " times on " +
              FullName(generator);
    return called > 0;
}

// The tutorial panel (UI_TutorialPanel_C) closes through its Continue handler; the HUD's CloseTutorial interface
// function is the other way in. A popup that is not open ignores both.
bool CloseTutorial(std::string& summary) {
    auto panelClass = FindClass("TutorialWidget");
    int panels = 0, closed = 0;
    for (int32_t i = 0; panelClass && i < ObjectCount(); ++i) {
        auto o = ObjectAt(i);
        if (!o || !IsLive(o) || IsDefaultOrArchetype(o) || !IsA(o, panelClass)) continue;
        Call visible(o, "IsVisible");
        bool shown = visible.invoke() && visible.get<bool>("ReturnValue");
        ++panels;
        if (!shown) continue;
        if (FindChild(o->cls, "OnTutorialClosedPressed") && Call(o, "OnTutorialClosedPressed").invoke()) ++closed;
        Log("close-tutorial: %s was open", FullName(o).c_str());
    }
    summary = std::to_string(closed) + " of " + std::to_string(panels) + " tutorial panels closed";
    return panelClass != nullptr;
}

} // namespace coop::research
