#include "research.hpp"

#include "log.hpp"

#include <windows.h>
#ifdef GetProp
#undef GetProp
#endif

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <unordered_map>
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

constexpr uint64_t kCpfNet = 0x20;        // CPF_Net: the property replicates
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
    bool operator==(const TraceKey&) const = default;
};

struct TraceHash {
    size_t operator()(const TraceKey& k) const {
        return std::hash<void*>()(k.cls) ^ (std::hash<void*>()(k.fn) << 1);
    }
};

bool g_tracing = false;
double g_traceStart = 0;
std::unordered_map<TraceKey, TraceEntry, TraceHash> g_trace;

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

    int worlds = 0, engines = 0, replicatedActors = 0, totalActors = 0, netDrivers = 0;
    std::map<std::string, int> replicatedByClass, netPropsByPackage, netClassesByPackage, proxiesByClass;
    UObject *gameMode = nullptr, *gameState = nullptr, *playerState = nullptr, *controller = nullptr;
    for (int32_t i = 0, n = ObjectCount(); i < n; ++i) {
        auto obj = ObjectAt(i);
        if (!IsLive(obj) || !IsLive(obj->cls)) continue;
        if (IsClassObject(obj)) {
            // Count replicated properties declared per class, grouped by package.
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
                    std::fprintf(file, "NETCLASS %s replicated-properties=%d\n", full.c_str(), netProps);
            }
            continue;
        }
        if (IsDefaultOrArchetype(obj)) continue;
        if (worldClass && IsA(obj, worldClass)) {
            ++worlds;
            std::fprintf(file, "WORLD %s NetDriver=%s DemoNetDriver=%s NetworkManager=%s\n", FullName(obj).c_str(),
                         ObjectProp(obj, "NetDriver").c_str(), ObjectProp(obj, "DemoNetDriver").c_str(),
                         ObjectProp(obj, "NetworkManager").c_str());
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
            if (gameModeClass && !gameMode && IsA(obj, gameModeClass)) gameMode = obj;
            else if (gameStateClass && !gameState && IsA(obj, gameStateClass)) gameState = obj;
            else if (playerStateClass && !playerState && IsA(obj, playerStateClass)) playerState = obj;
            else if (controllerClass && !controller && IsA(obj, controllerClass)) controller = obj;
        }
    }
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
            std::fprintf(file, "PC %s Role=%s RemoteRole=%s bReplicates=%s NetConnection=%s Pawn=%s PlayerState=%s StateName=%s\n",
                         FullName(obj).c_str(), ByteProp(obj, "Role").c_str(), ByteProp(obj, "RemoteRole").c_str(),
                         BoolProp(obj, "bReplicates").c_str(), ObjectProp(obj, "NetConnection").c_str(),
                         ObjectProp(obj, "Pawn").c_str(), ObjectProp(obj, "PlayerState").c_str(),
                         [&] { FName v{}; return ue::GetProp(obj, "StateName", v) ? NameString(v.index) : std::string("?"); }().c_str());
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

UObject* FindInstance(const char* className) {
    auto cls = FindClass(className);
    for (int32_t i = 0, n = ObjectCount(); cls && i < n; ++i) {
        auto obj = ObjectAt(i);
        if (IsLive(obj) && IsLive(obj->cls) && !IsDefaultOrArchetype(obj) && IsA(obj, cls)) return obj;
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

void SetStreamingRpcDrops(bool hostStatus, bool guestVisibility) {
    g_dropHostStatus = hostStatus;
    g_dropGuestVisibility = guestVisibility;
}

bool DropProcessEvent(UObject* obj, UObject* fn) {
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
    using MovePendingLevelFn = void (*)(void*, uint8_t*);
    auto vtable = *reinterpret_cast<void***>(engine);
    reinterpret_cast<MovePendingLevelFn>(vtable[kEngineMovePendingLevelSlot])(engine, context);
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

void HookedHandleClientPlayer(UObject* connection, UObject* pc, UObject* netConnection) {
    auto local = FindInstance("LocalPlayer");
    UObject* keep = nullptr;
    if (IsLive(local)) ue::GetProp(local, "PlayerController", keep);
    if (!IsLive(keep) || keep == pc) keep = nullptr;
    if (keep) SetObjectProp(local, "PlayerController", nullptr);
    g_handleClientPlayerOriginal(connection, pc, netConnection);
    if (keep) {
        SetObjectProp(local, "PlayerController", keep);
        SetObjectProp(keep, "Player", local);
    }
    Log("research: HandleClientPlayer: replicated %s attached to the connection; local player kept %s",
        IsLive(pc) ? FullName(pc).c_str() : "?", keep ? FullName(keep).c_str() : "nothing");
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
    summary = std::string("Possess ") + (possessed ? "ran" : "failed") + "; " + FullName(pc) + ".Pawn=" +
              (IsLive(after) ? FullName(after) : std::string("null"));
    return possessed && after == pawn;
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
    summary = "local player now drives " + FullName(pawn) + " through " + FullName(pc) + " (ClientRestart " +
              (restarted ? "ran" : "failed") + ", view target " + (viewed ? "set" : "not set") + "); own controller kept as " +
              (g_takeoverOwnController ? FullName(g_takeoverOwnController) : std::string("none"));
    return true;
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

void TraceEvent(UObject* obj, UObject* fn, void* parms) {
    if (!g_tracing || !obj || !fn) return;
    if (g_trace.size() >= 200000) return;
    auto& entry = g_trace[{obj->cls, fn}];
    if (entry.count++ == 0) entry.object = IsLive(obj) ? FullName(obj) : "<stale>";
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
    std::fprintf(file, "# %.1f s, %d unique class.function pairs\n", Seconds() - g_traceStart,
                 static_cast<int>(rows.size()));
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

} // namespace coop::research
