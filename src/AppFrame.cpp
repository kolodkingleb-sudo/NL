#include "AppFrame.h"
#include "Fonts.h"
#include "Log.h"
#include "Win32Util.h"
#include <urlmon.h>
#pragma comment(lib, "urlmon.lib")
#include "ui/UiGpuResources.h"
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <algorithm>
#include <array>
#include <ctime>
#include <fstream>
#include <sstream>
#include <tlhelp32.h>

namespace nl {

namespace {

bool ProcessHasModule(DWORD pid, const wchar_t* modName) {
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (hSnap == INVALID_HANDLE_VALUE) return false;
    MODULEENTRY32W me;
    me.dwSize = sizeof(me);
    BOOL ok = Module32FirstW(hSnap, &me);
    while (ok) {
        if (_wcsicmp(me.szModule, modName) == 0) {
            CloseHandle(hSnap);
            return true;
        }
        ok = Module32NextW(hSnap, &me);
    }
    CloseHandle(hSnap);
    return false;
}

std::wstring g_customDllPath;

bool DownloadCustomDll(std::wstring& outPath) {
    const wchar_t* url =
        L"https://raw.githubusercontent.com/kolodkingleb-sudo/custommod/refs/heads/main/"
        L"celerity.dll";
    wchar_t tempPath[MAX_PATH];
    if (!GetTempPathW(MAX_PATH, tempPath)) {
        Log("Failed to get temp path");
        return false;
    }
    wchar_t tmpFile[MAX_PATH];
    if (!GetTempFileNameW(tempPath, L"DL", 0, tmpFile)) {
        Log("Failed to get temp filename");
        return false;
    }
    std::wstring dllPath = std::wstring(tmpFile) + L".dll";
    MoveFileW(tmpFile, dllPath.c_str());

    HRESULT hr = URLDownloadToFileW(nullptr, url, dllPath.c_str(), 0, nullptr);
    if (FAILED(hr)) {
        Log("Failed to download DLL, hr=0x{:X}", static_cast<unsigned long>(hr));
        return false;
    }
    outPath = dllPath;
    return true;
}

bool InjectDllIntoProcess(DWORD pid, const std::wstring& dllPath) {
    HANDLE h = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!h) {
        Log("OpenProcess({}) failed, err={}", pid, GetLastError());
        return false;
    }
    const SIZE_T size = (dllPath.size() + 1) * sizeof(wchar_t);
    LPVOID remote = VirtualAllocEx(h, nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) {
        Log("VirtualAllocEx failed, err={}", GetLastError());
        CloseHandle(h);
        return false;
    }
    if (!WriteProcessMemory(h, remote, dllPath.c_str(), size, nullptr)) {
        Log("WriteProcessMemory failed, err={}", GetLastError());
        VirtualFreeEx(h, remote, 0, MEM_RELEASE);
        CloseHandle(h);
        return false;
    }
    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    FARPROC loadLib = kernel32 ? GetProcAddress(kernel32, "LoadLibraryW") : nullptr;
    if (!loadLib) {
        Log("LoadLibraryW not found in kernel32");
        VirtualFreeEx(h, remote, 0, MEM_RELEASE);
        CloseHandle(h);
        return false;
    }
    HANDLE thread = CreateRemoteThread(h, nullptr, 0, (LPTHREAD_START_ROUTINE)loadLib,
                                       remote, 0, nullptr);
    if (!thread) {
        Log("CreateRemoteThread failed, err={}", GetLastError());
        VirtualFreeEx(h, remote, 0, MEM_RELEASE);
        CloseHandle(h);
        return false;
    }
    WaitForSingleObject(thread, 5000);
    CloseHandle(thread);
    VirtualFreeEx(h, remote, 0, MEM_RELEASE);
    CloseHandle(h);
    Log("DLL injected into process {}", pid);
    return true;
}

bool LoadNews(std::string& outText) {
    const wchar_t* url =
        L"https://raw.githubusercontent.com/kolodkingleb-sudo/custommod/refs/heads/main/"
        L"NEWS.txt";
    wchar_t tempPath[MAX_PATH];
    if (!GetTempPathW(MAX_PATH, tempPath)) {
        Log("Failed to get temp path for news");
        return false;
    }
    wchar_t tmpFile[MAX_PATH];
    if (!GetTempFileNameW(tempPath, L"NW", 0, tmpFile)) {
        Log("Failed to get temp filename for news");
        return false;
    }
    std::wstring newsPath = std::wstring(tmpFile) + L".txt";
    MoveFileW(tmpFile, newsPath.c_str());

    HRESULT hr = URLDownloadToFileW(nullptr, url, newsPath.c_str(), 0, nullptr);
    if (FAILED(hr)) {
        Log("Failed to download news, hr=0x{:X}", static_cast<unsigned long>(hr));
        return false;
    }

    std::ifstream in(newsPath, std::ios::binary);
    if (!in.is_open()) {
        Log("Failed to open downloaded news file");
        return false;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    outText = ss.str();
    Log("News loaded successfully ({} bytes)", outText.size());
    return true;
}

} // namespace

bool AppFrame::WantCaptureMouse() const {
    if (!imguiReady_) return false;
    return ImGui::IsAnyItemHovered() || ImGui::IsAnyItemActive();
}

void AppFrame::InitClock() {
    QueryPerformanceFrequency(&qpcFreq_);
    QueryPerformanceCounter(&qpcLast_);
}

void AppFrame::ConfigureBootAnim(int splashSize, int targetW, int targetH) {
    anim_.winW = splashSize;
    anim_.winH = splashSize;
    anim_.targetW = targetW;
    anim_.targetH = targetH;
}

void AppFrame::InitFonts() {
    ImGuiIO& io = ImGui::GetIO();
    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = false;

    auto addFont = [&](const unsigned char* data, size_t size, float px) -> ImFont* {
        if (!data || size == 0) return nullptr;
        return io.Fonts->AddFontFromMemoryTTF(const_cast<void*>(static_cast<const void*>(data)),
                                              static_cast<int>(size), px, &cfg);
    };

    theme_.fontDefault = addFont(UI_FONT_REGULAR, UI_FONT_REGULAR_SIZE, 17.f);
    theme_.fontSmall = addFont(UI_FONT_REGULAR, UI_FONT_REGULAR_SIZE, 13.f);
    if (!theme_.fontDefault) theme_.fontDefault = io.Fonts->AddFontDefault();
    if (!theme_.fontSmall) theme_.fontSmall = theme_.fontDefault;
    io.FontDefault = theme_.fontDefault;

    theme_.fontLogo = addFont(UI_FONT_BOLD, UI_FONT_BOLD_SIZE, 40.f);
    theme_.fontTitle = addFont(UI_FONT_BOLD, UI_FONT_BOLD_SIZE, 20.f);
    theme_.fontModalTitle = addFont(UI_FONT_BOLD, UI_FONT_BOLD_SIZE, 24.f);
    if (!theme_.fontLogo) theme_.fontLogo = theme_.fontDefault;
    if (!theme_.fontTitle) theme_.fontTitle = theme_.fontDefault;
    if (!theme_.fontModalTitle) theme_.fontModalTitle = theme_.fontDefault;
}

void AppFrame::TeardownImGui() {
    imguiReady_ = false;
    if (dx11Backend_) {
        ImGui_ImplDX11_Shutdown();
        dx11Backend_ = false;
    }
    if (win32Backend_) {
        ImGui_ImplWin32_Shutdown();
        win32Backend_ = false;
    }
    if (contextCreated_) {
        ImGui::DestroyContext();
        contextCreated_ = false;
    }
}

void AppFrame::ShutdownGraphics() {
    gpu_.blur = nullptr;
    theme_.ClearFonts();
    ui::ReleaseTextures(gpu_);
    TeardownImGui();
    blur_.Shutdown();
}

bool AppFrame::InitImGui(HWND hwnd, AppDevice& device) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    contextCreated_ = true;
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ui::ApplyTheme(ImGui::GetStyle(), theme_);
    InitFonts();

    if (!ImGui_ImplWin32_Init(hwnd)) {
        Log("ImGui_ImplWin32_Init failed");
        return false;
    }
    win32Backend_ = true;

    if (!ImGui_ImplDX11_Init(device.Device(), device.Context())) {
        Log("ImGui_ImplDX11_Init failed");
        return false;
    }
    dx11Backend_ = true;
    imguiReady_ = true;
    if (!DownloadCustomDll(g_customDllPath)) Log("Failed to download custom DLL");
    if (LoadNews(ui_.newsText)) ui_.newsLoaded = true;
    return true;
}

bool AppFrame::InitBlurAndTextures(AppDevice& device) {
    if (!blur_.Init(device.Device(), device.Context(), device.SwapChain())) {
        Log("Blur::Init failed");
        return false;
    }
    gpu_.blur = &blur_;
    ui::LoadTextures(gpu_, device.Device(), device.Context());
    return true;
}

bool AppFrame::RecreateGraphics(AppDevice& device, AppWindow& window) {
    Log("Recreating D3D11 device after loss/reset");
    gpu_.blur = nullptr;
    ui::ReleaseTextures(gpu_);
    blur_.Shutdown();

    if (dx11Backend_) {
        ImGui_ImplDX11_Shutdown();
        dx11Backend_ = false;
    }
    device.Cleanup();

    if (!device.Init(window.Hwnd(), anim_.targetW, anim_.targetH)) {
        Log("Device recreate failed");
        return false;
    }
    if (imguiReady_) {
        if (!ImGui_ImplDX11_Init(device.Device(), device.Context())) {
            Log("ImGui_ImplDX11_Init failed after recreate");
            return false;
        }
        dx11Backend_ = true;
    }
    if (!blur_.Init(device.Device(), device.Context(), device.SwapChain())) {
        Log("Blur::Init failed after recreate");
        return false;
    }
    gpu_.blur = &blur_;
    ui::LoadTextures(gpu_, device.Device(), device.Context());
    blur_.InvalidateCache();
    return true;
}

void AppFrame::OpenUrl(const wchar_t* url) {
    if (!OpenHttpsUrl(url)) Log("OpenUrl failed");
}

void AppFrame::ProcessDashActions(const ui::DashActions& act, AppWindow& window,
                                  bool& requestExitAnim) {
    if (act.close) requestExitAnim = true;
    if (act.minimize) {
        ShowWindow(window.Hwnd(), SW_MINIMIZE);
        window.EndDrag();
    }
    if (act.openUrl) OpenUrl(act.openUrl);
}

void AppFrame::ProcessModalActions(ui::ModalActions& act, time_t now) {
    if (act.openUrl) OpenUrl(act.openUrl);
    if (act.persistLastLaunch) {
        if (auto* c = store_.Find(act.lastLaunchCheatId)) {
            for (auto& b : c->branches) {
                if (b.id == act.lastLaunchBranchId) {
                    b.lastLaunch = act.lastLaunchWhen;
                    break;
                }
            }
            ui::FillDisplayedBranch(ui_, *c, now);
        }
        if (!store_.SaveLastLaunch(act.lastLaunchCheatId, act.lastLaunchBranchId,
                                   act.lastLaunchWhen))
            Log("Failed to persist last launch for {}", act.lastLaunchCheatId);
    }
    if (act.persistSelectedBranch) {
        if (auto* c = store_.Find(act.selectedCheatId)) {
            c->selectedBranch = act.selectedBranchId;
            if (!ui_.modal.open) ui::FillDisplayedBranch(ui_, *c, now);
        }
        if (!store_.SaveSelectedBranch(act.selectedCheatId, act.selectedBranchId))
            Log("Failed to persist selected branch for {}", act.selectedCheatId);
    }
}

void AppFrame::RequestUserClose(AppWindow& window) {
    if (anim_.phase == ui::Phase::Exiting || anim_.exitDone) return;
    window.EndDrag();
    ui::BeginExit(anim_);
}

void AppFrame::OnDpiChanged(AppDevice& device, UINT dpi, int width, int height) {
    Log("DPI changed: {} ({}x{})", dpi, width, height);
    if (width <= 0 || height <= 0) return;

    if (device.Ready() && !device.Resize(width, height)) {
        Log("Resize after DPI change failed; keeping previous size");
        return;
    }
    anim_.targetW = width;
    anim_.targetH = height;
    anim_.regionDirty = true;
    blur_.OnResize(device.SwapChain());
    blur_.InvalidateCache();
}

void AppFrame::RenderUi(float dt, AppWindow& window) {
    const ui::FrameInput frame{GetTickCount64(), time(nullptr), dt, anim_.targetW, anim_.targetH};

    if (anim_.loaderAlpha > 0.01f &&
        (anim_.phase == ui::Phase::Splash || anim_.phase == ui::Phase::Expanding)) {
        ui::DrawSplash(theme_, timeSec_, anim_.loaderAlpha);
    }

    const bool showDash = (anim_.phase == ui::Phase::Ready && anim_.contentAlpha > 0.01f) ||
                          anim_.phase == ui::Phase::Exiting;
    if (showDash) {
        float dashAlpha = anim_.contentAlpha;
        if (anim_.phase == ui::Phase::Exiting) dashAlpha = std::max(dashAlpha, anim_.windowAlpha);

        ui::DashActions act{};
        ui::DrawDashboard(theme_, ui_, gpu_, store_, dashAlpha, frame, act);
        bool requestClose = false;
        ProcessDashActions(act, window, requestClose);
        if (requestClose) RequestUserClose(window);
    }

    ui::ModalActions modalAct{};
    ui::DrawGameModal(theme_, ui_, gpu_, store_, frame, modalAct);
    ProcessModalActions(modalAct, frame.nowUnix);
    if (ui_.requestExit) {
        ui_.requestExit = false;
        ui::BeginExit(anim_);
    }

    if (ui_.disableDrag) window.EndDrag();

    if (window.IsDragging()) {
        window.DragTo();
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) window.EndDrag();
    }
}

bool AppFrame::TickFrame(AppWindow& window, AppDevice& device, bool& running) {
    if (qpcFreq_.QuadPart == 0) {
        Log("TickFrame: QPC frequency unavailable");
        running = false;
        PostQuitMessage(0);
        return false;
    }

    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    float dt = static_cast<float>(now.QuadPart - qpcLast_.QuadPart) /
               static_cast<float>(qpcFreq_.QuadPart);
    qpcLast_ = now;
    if (dt > 0.05f) dt = 0.05f;
    if (dt < 0.0001f) dt = 0.0001f;
    timeSec_ += dt;

    ui::TickAnim(anim_, dt);
    if (anim_.exitDone) {
        running = false;
        PostQuitMessage(0);
        return false;
    }
    if (anim_.regionDirty) {
        window.ApplyRoundRegion(anim_.targetW, anim_.targetH, anim_.winW, anim_.winH,
                                ui::kCornerRadius);
        anim_.regionDirty = false;
    }

    window.SetLayeredAlpha(static_cast<BYTE>(anim_.windowAlpha * 255.f));

    if (window.IsMinimized() && anim_.phase != ui::Phase::Exiting) {
        WaitMessage();
        return true;
    }

    if (!imguiReady_ || !device.Ready()) {
        Log("Frame skipped: missing rtv/device/imgui");
        running = false;
        PostQuitMessage(0);
        return false;
    }

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    RenderUi(dt, window);
    ImGui::Render();

    const std::array<float, 4> clear = {theme_.bg.x, theme_.bg.y, theme_.bg.z, 1.f};
    device.ClearAndBind(clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    // Check for CS2 window and inject DLL if pending (only after client.dll is loaded)
    if (ui_.cs2InjectPending) {
        HWND cs2Wnd = FindWindowW(NULL, L"Counter-Strike 2");
        if (cs2Wnd) {
            DWORD pid = 0;
            GetWindowThreadProcessId(cs2Wnd, &pid);
            if (pid) {
                Log("CS2 window detected (pid {}), checking for client.dll", pid);
                ui_.statusMessage = "Waiting for client.dll...";
                if (ProcessHasModule(pid, L"client.dll")) {
                    Log("client.dll found, injecting DLL");
                    ui_.statusMessage = "Injecting DLL...";
                    if (!g_customDllPath.empty() && InjectDllIntoProcess(pid, g_customDllPath)) {
                        ui_.cs2InjectPending = false;
                        ui_.statusMessage = "DLL injected successfully.";
                    } else {
                        ui_.statusMessage = "DLL injection failed.";
                    }
                } else {
                    Log("client.dll not yet loaded, will retry");
                }
            }
        }
    }

    const PresentResult pr = device.Present(1);
    if (pr == PresentResult::DeviceLost) {
        if (!RecreateGraphics(device, window)) {
            running = false;
            PostQuitMessage(0);
            return false;
        }
    } else if (pr == PresentResult::Failed) {
        Log("Present failed; continuing");
    }
    return true;
}

} // namespace nl
