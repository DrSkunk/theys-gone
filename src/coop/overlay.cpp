#include "overlay.hpp"
#include "log.hpp"

#include <windows.h>
#include <d3d11.h>
#include <imgui.h>
#include <backends/imgui_impl_dx11.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>

namespace coop {
namespace {
using PresentFn = HRESULT (STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using ResizeFn = HRESULT (STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
PresentFn originalPresent = nullptr;
ResizeFn originalResize = nullptr;
// The vendor implementations, read from the module file on disk. Other overlays (notably Steam's) hook the same
// shared vtable and can capture our hook as their "original"; forwarding to them again would recurse forever.
PresentFn nativePresent = nullptr;
ResizeFn nativeResize = nullptr;
thread_local int presentDepth = 0, resizeDepth = 0;
std::atomic<bool> presentLoopLogged = false, resizeLoopLogged = false;
std::mutex frameMutex, renderMutex;
OverlayFrame frame;
std::atomic<bool> ready = false;
HWND dummyWindow = nullptr;
IDXGISwapChain* activeChain = nullptr;
ID3D11Device* device = nullptr;
ID3D11DeviceContext* context = nullptr;
ID3D11RenderTargetView* target = nullptr;
ImGuiContext* imguiContext = nullptr;

struct Depth {
    int& value;
    explicit Depth(int& counter) : value(counter) { ++value; }
    ~Depth() { --value; }
};

HMODULE ModuleAt(const void* address) {
    HMODULE module = nullptr;
    if (!address || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                        static_cast<LPCWSTR>(address), &module))
        return nullptr;
    return module;
}

// "module.dll+0x1234" for crash-dump matching; raw address when no module owns it.
std::string Describe(const void* address) {
    char text[MAX_PATH + 32];
    HMODULE module = ModuleAt(address);
    char path[MAX_PATH]{};
    if (!module || !GetModuleFileNameA(module, path, MAX_PATH)) {
        std::snprintf(text, sizeof text, "%p", address);
        return text;
    }
    const char* name = path;
    for (const char* at = path; *at; ++at) if (*at == '\\' || *at == '/') name = at + 1;
    std::snprintf(text, sizeof text, "%s+0x%llx", name, static_cast<unsigned long long>(
        reinterpret_cast<const uint8_t*>(address) - reinterpret_cast<const uint8_t*>(module)));
    return text;
}

// Reads a vtable entry as the module file on disk has it, rebased to where the module is loaded. Hooks patch the
// table in memory only, so this recovers the vendor function even when another overlay hooked it before us.
void* OnDiskEntry(void* const* entry) {
    HMODULE module = ModuleAt(entry);
    wchar_t path[MAX_PATH]{};
    if (!module || !GetModuleFileNameW(module, path, MAX_PATH)) return nullptr;
    auto base = reinterpret_cast<uint8_t*>(module);
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
    uint64_t rva = reinterpret_cast<const uint8_t*>(entry) - base;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) return nullptr;
    auto readAt = [&](uint64_t offset, void* out, DWORD size) {
        LARGE_INTEGER position{};
        position.QuadPart = static_cast<LONGLONG>(offset);
        DWORD read = 0;
        return SetFilePointerEx(file, position, nullptr, FILE_BEGIN) && ReadFile(file, out, size, &read, nullptr) &&
               read == size;
    };
    // The loader rewrites ImageBase in memory after relocating, so the preferred base must come from the file.
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 diskNt{};
    uint64_t value = 0;
    bool found = readAt(0, &dos, sizeof dos) && readAt(static_cast<uint64_t>(dos.e_lfanew), &diskNt, sizeof diskNt) &&
                 diskNt.OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC;
    if (found) {
        found = false;
        auto section = IMAGE_FIRST_SECTION(nt);
        for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
            if (rva < section->VirtualAddress || rva + sizeof value > section->VirtualAddress + section->SizeOfRawData)
                continue;
            found = readAt(section->PointerToRawData + (rva - section->VirtualAddress), &value, sizeof value);
            break;
        }
    }
    CloseHandle(file);
    if (!found || value < diskNt.OptionalHeader.ImageBase) return nullptr;
    uint64_t offset = value - diskNt.OptionalHeader.ImageBase;
    if (offset >= nt->OptionalHeader.SizeOfImage) return nullptr;
    return base + offset;
}

void ReleaseTarget() {
    if (target) target->Release();
    target = nullptr;
}

HRESULT STDMETHODCALLTYPE Present(IDXGISwapChain* chain, UINT interval, UINT flags) {
    if (presentDepth > 0 && nativePresent) {
        if (!presentLoopLogged.exchange(true))
            Log("overlay: Present re-entered from %s; forwarding to the native implementation",
                Describe(__builtin_return_address(0)).c_str());
        return nativePresent(chain, interval, flags);
    }
    Depth depth(presentDepth);
    if (!(flags & DXGI_PRESENT_TEST)) {
        std::lock_guard lock(renderMutex);
        DXGI_SWAP_CHAIN_DESC desc{};
        if (SUCCEEDED(chain->GetDesc(&desc)) && desc.OutputWindow != dummyWindow && IsWindowVisible(desc.OutputWindow)) {
            if (!activeChain) {
                HRESULT result = chain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&device));
                if (SUCCEEDED(result)) {
                    device->GetImmediateContext(&context);
                    imguiContext = ImGui::CreateContext();
                    auto& io = ImGui::GetIO();
                    io.IniFilename = nullptr;
                    io.LogFilename = nullptr;
                    const char* fonts[] = {"C:\\windows\\Fonts\\segoeui.ttf", "C:\\windows\\Fonts\\tahoma.ttf",
                        "C:\\windows\\Fonts\\arial.ttf", "Z:\\usr\\share\\fonts\\TTF\\DejaVuSans.ttf"};
                    for (auto path : fonts) {
                        if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) continue;
                        if (io.Fonts->AddFontFromFileTTF(path, 24.f)) break;
                        Log("overlay: could not load font %s", path);
                    }
                    if (ImGui_ImplDX11_Init(device, context)) {
                        activeChain = chain;
                        ready = true;
                        auto table = *reinterpret_cast<void***>(chain);
                        Log("overlay: Direct3D 11 renderer ready (called from %s; vtable %s: Present=%s ResizeBuffers=%s)",
                            Describe(__builtin_return_address(0)).c_str(), Describe(table).c_str(),
                            Describe(table[8]).c_str(), Describe(table[13]).c_str());
                    } else {
                        Log("overlay: Direct3D 11 renderer initialization failed");
                        ImGui::DestroyContext(imguiContext);
                        imguiContext = nullptr;
                        context->Release();
                        context = nullptr;
                        device->Release();
                        device = nullptr;
                    }
                } else Log("overlay: GetDevice failed (0x%lx)", static_cast<unsigned long>(result));
            }
            if (chain == activeChain) {
                OverlayFrame current;
                {
                    std::lock_guard frameLock(frameMutex);
                    current = frame;
                }
                if (!current.primitives.empty() && current.width > 0 && current.height > 0) {
                    ID3D11Texture2D* buffer = nullptr;
                    D3D11_TEXTURE2D_DESC texture{};
                    HRESULT result = chain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&buffer));
                    if (SUCCEEDED(result)) {
                        buffer->GetDesc(&texture);
                        if (!target) result = device->CreateRenderTargetView(buffer, nullptr, &target);
                        buffer->Release();
                    }
                    if (SUCCEEDED(result) && target) {
                        ImGui::SetCurrentContext(imguiContext);
                        auto& io = ImGui::GetIO();
                        io.DisplaySize = ImVec2(static_cast<float>(texture.Width), static_cast<float>(texture.Height));
                        io.DeltaTime = 1.f / 60.f;
                        ImGui_ImplDX11_NewFrame();
                        ImGui::NewFrame();
                        auto draw = ImGui::GetForegroundDrawList();
                        float sx = io.DisplaySize.x / current.width, sy = io.DisplaySize.y / current.height;
                        for (const auto& primitive : current.primitives) {
                            auto c = primitive.color;
                            ImU32 color = ImGui::ColorConvertFloat4ToU32(ImVec4(c.r, c.g, c.b, c.a));
                            ImVec2 at(primitive.x * sx, primitive.y * sy);
                            if (primitive.text.empty())
                                draw->AddRectFilled(at, ImVec2(at.x + primitive.width * sx, at.y + primitive.height * sy), color);
                            else draw->AddText(nullptr, primitive.fontSize * sy, at, color, primitive.text.c_str());
                        }
                        ImGui::Render();
                        ID3D11RenderTargetView* previous[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
                        ID3D11DepthStencilView* depth = nullptr;
                        context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, previous, &depth);
                        context->OMSetRenderTargets(1, &target, nullptr);
                        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
                        context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, previous, depth);
                        for (auto view : previous) if (view) view->Release();
                        if (depth) depth->Release();
                    } else Log("overlay: render target unavailable (0x%lx)", static_cast<unsigned long>(result));
                }
            }
        }
    }
    return originalPresent(chain, interval, flags);
}

HRESULT STDMETHODCALLTYPE Resize(IDXGISwapChain* chain, UINT count, UINT width, UINT height,
                                  DXGI_FORMAT format, UINT flags) {
    if (resizeDepth > 0 && nativeResize) {
        if (!resizeLoopLogged.exchange(true))
            Log("overlay: ResizeBuffers re-entered from %s; forwarding to the native implementation",
                Describe(__builtin_return_address(0)).c_str());
        return nativeResize(chain, count, width, height, format, flags);
    }
    Depth depth(resizeDepth);
    {
        std::lock_guard lock(renderMutex);
        if (chain == activeChain) ReleaseTarget();
    }
    return originalResize(chain, count, width, height, format, flags);
}

DWORD WINAPI InstallRenderer(void*) {
    dummyWindow = CreateWindowExW(0, L"STATIC", L"TheysGone renderer setup", WS_POPUP, 0, 0, 1, 1,
                                  nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!dummyWindow) {
        Log("overlay: setup window failed (error %lu)", GetLastError());
        return 1;
    }
    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferCount = 1;
    desc.BufferDesc.Width = desc.BufferDesc.Height = 1;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow = dummyWindow;
    desc.SampleDesc.Count = 1;
    desc.Windowed = TRUE;
    IDXGISwapChain* chain = nullptr;
    ID3D11Device* temporaryDevice = nullptr;
    ID3D11DeviceContext* temporaryContext = nullptr;
    HRESULT result = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &desc, &chain, &temporaryDevice, nullptr, &temporaryContext);
    if (SUCCEEDED(result)) {
        auto table = *reinterpret_cast<void***>(chain);
        DWORD protection = 0;
        if (VirtualProtect(table + 8, 6 * sizeof(void*), PAGE_READWRITE, &protection)) {
            originalPresent = reinterpret_cast<PresentFn>(table[8]);
            originalResize = reinterpret_cast<ResizeFn>(table[13]);
            nativePresent = reinterpret_cast<PresentFn>(OnDiskEntry(table + 8));
            nativeResize = reinterpret_cast<ResizeFn>(OnDiskEntry(table + 13));
            Log("overlay: vtable %s: Present=%s (native %s) ResizeBuffers=%s (native %s)", Describe(table).c_str(),
                Describe(reinterpret_cast<void*>(originalPresent)).c_str(),
                Describe(reinterpret_cast<void*>(nativePresent)).c_str(),
                Describe(reinterpret_cast<void*>(originalResize)).c_str(),
                Describe(reinterpret_cast<void*>(nativeResize)).c_str());
            InterlockedExchangePointer(table + 8, reinterpret_cast<void*>(&Present));
            InterlockedExchangePointer(table + 13, reinterpret_cast<void*>(&Resize));
            DWORD ignored = 0;
            if (!VirtualProtect(table + 8, 6 * sizeof(void*), protection, &ignored))
                Log("overlay: could not restore vtable protection (error %lu)", GetLastError());
            Log("overlay: swap-chain callbacks installed");
        } else Log("overlay: swap-chain vtable protection failed (error %lu)", GetLastError());
        temporaryContext->Release();
        temporaryDevice->Release();
        chain->Release();
    } else Log("overlay: setup device failed (0x%lx)", static_cast<unsigned long>(result));
    DestroyWindow(dummyWindow);
    return SUCCEEDED(result) ? 0 : 1;
}
} // namespace

void StartOverlay() {
    HANDLE thread = CreateThread(nullptr, 0, InstallRenderer, nullptr, 0, nullptr);
    if (thread) CloseHandle(thread);
    else Log("overlay: setup thread failed (error %lu)", GetLastError());
}
bool OverlayReady() { return ready; }
void PublishOverlay(OverlayFrame value) {
    std::lock_guard lock(frameMutex);
    frame = std::move(value);
}
} // namespace coop
