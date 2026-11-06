#include "UiState.h"
#include "UiGpuResources.h"
#include "GameCatalog.h"
#include "Widgets.h"
#include "Anim.h"
#include "../Format.h"
#include "BlurImGui.h"
#include "../gfx/Blur.h"
#include "imgui.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <span>
#include <vector>

namespace nl::ui {

static_assert(kGameCatalogCount <= kMaxGameSlots,
              "gameDim[] would silently stop animating slots past kMaxGameSlots");

namespace {

constexpr float kCardRounding = 12.f;
constexpr float kCardBorder = 1.f;
constexpr float kCardPadX = 20.f;
constexpr float kCardPadY = 16.f;
constexpr float kCardHeight = 90.f;
constexpr float kIconSize = 50.f;
constexpr float kIconPad = 16.f;
constexpr float kIconRound = 8.f;
constexpr float kInactiveDim = 0.45f;
constexpr float kHoverGlow = 0.15f;
constexpr ImVec4 kOpaqueWhite(1.f, 1.f, 1.f, 1.f);
constexpr ImVec4 kStatusActiveBg(0.2f, 0.6f, 0.3f, 1.f);
constexpr ImVec4 kStatusExpiredBg(0.6f, 0.2f, 0.2f, 1.f);
constexpr ImVec4 kStatusActiveGlow(0.3f, 0.8f, 0.4f, 0.4f);

bool ActiveFirstThenIndex(const ContainerInfo& a, const ContainerInfo& b) {
    if (a.isActive != b.isActive) return a.isActive > b.isActive;
    return a.gameIdx < b.gameIdx;
}

void OpenGameModal(UiState& ui, data::CheatStore& store, const char* openId, std::uint64_t nowMs,
                   time_t nowUnix) {
    ui.modal.open = true;
    ui.modal.closing = false;
    ui.modal.openTime = nowMs;
    ui.modal.alpha = 0.f;
    ui.modal.gameId = openId;
    ui.inject = {};
    ui.inject.modalW = kModalW;
    ui.inject.modalH = kModalH;
    ui.inject.contentAlpha = 1.f;
    ui.modal.branchMenu = false;
    ui.modal.branchMenuT = 0.f;
    ui.modal.contentFade = 1.f;
    ui.modal.contentFadingOut = false;
    ui.modal.contentFadingIn = false;
    ui.modal.contentSwapPending = false;
    if (auto* info = store.Find(openId)) FillDisplayedBranch(ui, *info, nowUnix);
    ui.textScroll.Reset();
}

float DrawHeader(Theme& theme, float contentAlpha, int winW, float dt) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float logoX = 24.f, logoY = 18.f, logoSize = 32.f;
    
    for (int i = 3; i > 0; --i) {
        float glowAlpha = contentAlpha * 0.15f / static_cast<float>(i);
        float glowSize = logoSize + static_cast<float>(i) * 4.f;
        dl->AddCircleFilled(ImVec2(logoX + logoSize/2.f, logoY + logoSize/2.f), glowSize/2.f,
            ImGui::GetColorU32(ImVec4(theme.accent.x, theme.accent.y, theme.accent.z, glowAlpha)), 32);
    }
    
    dl->AddRectFilled(ImVec2(logoX, logoY), ImVec2(logoX + logoSize, logoY + logoSize),
        ImGui::GetColorU32(ImVec4(theme.accent.x, theme.accent.y, theme.accent.z, contentAlpha)), 6.f);
    
    ImGui::PushFont(theme.fontLogo);
    ImVec2 spSz = ImGui::CalcTextSize("SP");
    ImGui::PopFont();
    ImGui::SetCursorPos(ImVec2(logoX + (logoSize - spSz.x)/2.f, logoY + (logoSize - spSz.y)/2.f - 2.f));
    { const ScopedTextColor face(theme.text, contentAlpha); ImGui::TextUnformatted("SP"); }
    
    const char* title = "Shadow Pulse";
    const char* subtitle = "Premium Cheat Platform";
    ImGui::PushFont(theme.fontTitle);
    ImVec2 titleSz = ImGui::CalcTextSize(title);
    ImGui::PopFont();
    float titleX = (static_cast<float>(winW) - titleSz.x) / 2.f;
    
    ImGui::SetCursorPos(ImVec2(titleX, 22.f));
    { const ScopedTextColor color(theme.text, contentAlpha); ImGui::TextUnformatted(title); }
    ImGui::SetCursorPos(ImVec2(titleX, 44.f));
    { const ScopedTextColor color(theme.textMuted, contentAlpha * 0.7f); ImGui::TextUnformatted(subtitle); }
    
    float sepY = 70.f;
    for (float x = logoX + logoSize + 20.f; x < static_cast<float>(winW) - 20.f; x += 2.f) {
        float alpha = contentAlpha * (0.3f + 0.2f * std::sin(x * 0.02f));
        dl->AddLine(ImVec2(x, sepY), ImVec2(x + 1.f, sepY),
            ImGui::GetColorU32(ImVec4(theme.accent.x, theme.accent.y, theme.accent.z, alpha)), 1.f);
    }
    return 85.f;
}

void DrawSideNav(Theme& theme, UiState& ui, float contentAlpha, float dt, bool modalOpen, DashActions& out) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    struct NavBtn { const char* icon; const char* label; float* hover; const wchar_t* url; };
    NavBtn buttons[] = {
        {"H", "Home", &ui.siteHover, L"https://t.me/LeverWareCs2"},
        {"D", "Discord", &ui.discordHover, L"https://t.me/LeverWareCs2"},
        {"T", "Telegram", &ui.telegramHover, L"https://t.me/ShadowPulseLoader"},
    };
    
    float btnX = 20.f, btnY = 100.f, btnW = 60.f, btnH = 70.f, gap = 10.f;
    for (auto& btn : buttons) {
        float hoverTarget = ImGui::GetIO().MousePos.x >= btnX && ImGui::GetIO().MousePos.x <= btnX + btnW &&
                           ImGui::GetIO().MousePos.y >= btnY && ImGui::GetIO().MousePos.y <= btnY + btnH ? 1.f : 0.f;
        HoverLerp(*btn.hover, hoverTarget > 0.5f, dt);
        float bgAlpha = contentAlpha * (0.08f + 0.05f * btn.hover[0]);
        float borderAlpha = contentAlpha * (0.2f + 0.3f * btn.hover[0]);
        
        dl->AddRectFilled(ImVec2(btnX, btnY), ImVec2(btnX + btnW, btnY + btnH),
            ImGui::GetColorU32(ImVec4(theme.cardBg.x, theme.cardBg.y, theme.cardBg.z, bgAlpha)), 8.f);
        dl->AddRect(ImVec2(btnX, btnY), ImVec2(btnX + btnW, btnY + btnH),
            ImGui::GetColorU32(ImVec4(theme.accent.x, theme.accent.y, theme.accent.z, borderAlpha)), 8.f, 0, 1.2f);
        
        ImVec2 iconSz = ImGui::CalcTextSize(btn.icon);
        ImGui::SetCursorPos(ImVec2(btnX + (btnW - iconSz.x)/2.f, btnY + 18.f));
        { const ScopedTextColor color(theme.text, contentAlpha * (0.7f + 0.3f * btn.hover[0])); ImGui::TextUnformatted(btn.icon); }
        
        ImVec2 labelSz = ImGui::CalcTextSize(btn.label);
        ImGui::SetCursorPos(ImVec2(btnX + (btnW - labelSz.x)/2.f, btnY + btnH - 22.f));
        { const ScopedTextColor color(theme.textMuted, contentAlpha * (0.6f + 0.4f * btn.hover[0])); ImGui::TextUnformatted(btn.label); }
        
        ImGui::SetCursorPos(ImVec2(btnX, btnY));
        ImGui::InvisibleButton(btn.label, ImVec2(btnW, btnH));
        if (ImGui::IsItemClicked() && !modalOpen) out.openUrl = btn.url;
        btnY += btnH + gap;
    }
}

struct ComingSoonCard { float y = 0.f; const char* name = ""; ID3D11ShaderResourceView* tex = nullptr; };

void TickContainerAnim(UiState& ui, float dt) {
    for (auto& c : ui.containers) {
        if (c.animationProgress < 1.f) {
            c.animationProgress = std::min(1.f, c.animationProgress + dt / kContainerAnimSec);
            const float t = EaseInOutCubic(c.animationProgress);
            c.currentY = c.startY + (c.targetY - c.startY) * t;
        } else {
            c.currentY = c.targetY;
        }
    }
}

void DrawGameCard(ImDrawList* dl, Theme& theme, const ImVec2& pos, float containerWidth,
                  float containerHeight, float vis, const char* gameName, bool isActive,
                  const char* status, ID3D11ShaderResourceView* texture, float hoverAlpha) {
    const ImVec2 end(pos.x + containerWidth, pos.y + containerHeight);
    float bgAlpha = vis * (0.12f + 0.08f * hoverAlpha);
    dl->AddRectFilled(pos, end, ColorA(theme.cardBg, bgAlpha), kCardRounding);
    
    float borderAlpha = vis * (0.3f + 0.5f * hoverAlpha);
    float glowAlpha = vis * hoverAlpha * kHoverGlow;
    if (hoverAlpha > 0.01f)
        dl->AddRectFilled(pos, end, ImGui::GetColorU32(ImVec4(theme.accent.x, theme.accent.y, theme.accent.z, glowAlpha)), kCardRounding);
    dl->AddRect(pos, end, ColorA(theme.accent, borderAlpha), kCardRounding, 0, kCardBorder);
    
    float accentBarW = 4.f;
    ImVec4 accentColor = isActive ? theme.accent : ImVec4(theme.textMuted.x, theme.textMuted.y, theme.textMuted.z, 1.f);
    dl->AddRectFilled(ImVec2(pos.x + 2.f, pos.y + 2.f), ImVec2(pos.x + accentBarW, end.y - 2.f), ColorA(accentColor, vis), 2.f);
    
    if (texture) {
        float iconX = end.x - kIconSize - kIconPad;
        float iconY = pos.y + (containerHeight - kIconSize) / 2.f;
        if (isActive && hoverAlpha > 0.01f)
            dl->AddRectFilled(ImVec2(iconX - 3.f, iconY - 3.f), ImVec2(iconX + kIconSize + 3.f, iconY + kIconSize + 3.f),
                ImGui::GetColorU32(ImVec4(theme.accent.x, theme.accent.y, theme.accent.z, 0.2f * hoverAlpha)), kIconRound + 3.f);
        dl->AddImageRounded(reinterpret_cast<ImTextureID>(texture), ImVec2(iconX, iconY),
            ImVec2(iconX + kIconSize, iconY + kIconSize), ImVec2(0, 0), ImVec2(1, 1),
            ColorA(kOpaqueWhite, vis * (0.9f + 0.1f * hoverAlpha)), kIconRound);
    }
    
    float nameX = pos.x + kCardPadX + accentBarW + 12.f;
    float nameY = pos.y + kCardPadY + 4.f;
    ImGui::PushFont(theme.fontDefault);
    ImVec2 nameSz = ImGui::CalcTextSize(gameName);
    ImGui::PopFont();
    ImGui::SetCursorPos(ImVec2(nameX, nameY));
    { const ScopedTextColor color(theme.text, vis * (0.9f + 0.1f * hoverAlpha)); ImGui::TextUnformatted(gameName); }
    
    const char* statusText = isActive ? "Active" : "Expired";
    ImVec4 statusBg = isActive ? kStatusActiveBg : kStatusExpiredBg;
    ImVec2 statusBadgeSz = ImGui::CalcTextSize(statusText);
    float badgeW = statusBadgeSz.x + 16.f, badgeH = 20.f;
    float badgeX = nameX, badgeY = nameY + nameSz.y + 10.f;
    
    dl->AddRectFilled(ImVec2(badgeX, badgeY), ImVec2(badgeX + badgeW, badgeY + badgeH), ImGui::GetColorU32(statusBg), badgeH / 2.f);
    if (isActive)
        dl->AddRectFilled(ImVec2(badgeX, badgeY), ImVec2(badgeX + badgeW, badgeY + badgeH),
            ImGui::GetColorU32(ImVec4(kStatusActiveGlow.x, kStatusActiveGlow.y, kStatusActiveGlow.z, 0.3f)), badgeH / 2.f);
    
    ImGui::SetCursorPos(ImVec2(badgeX + (badgeW - statusBadgeSz.x)/2.f, badgeY + (badgeH - statusBadgeSz.y)/2.f - 1.f));
    { const ScopedTextColor color(ImVec4(1.f, 1.f, 1.f, 1.f), vis); ImGui::TextUnformatted(statusText); }
    
    ImGui::SetCursorPos(ImVec2(nameX, badgeY + badgeH + 8.f));
    { const ScopedTextColor color(theme.textMuted, vis * 0.6f); ImGui::TextUnformatted(status); }
    
    if (hoverAlpha > 0.01f) {
        float arrowX = end.x - kIconSize - kIconPad - 30.f;
        float arrowY = pos.y + containerHeight / 2.f;
        dl->AddTriangleFilled(ImVec2(arrowX, arrowY - 6.f), ImVec2(arrowX, arrowY + 6.f), ImVec2(arrowX + 8.f, arrowY),
            ImGui::GetColorU32(ImVec4(theme.accent.x, theme.accent.y, theme.accent.z, vis * hoverAlpha)));
    }
}

void DrawComingSoonOverlay(ImDrawList* dl, Theme& theme, const UiGpuResources& gpu,
                           std::span<const ComingSoonCard> cards, float startX,
                           float containerWidth, float containerHeight, float containersStartY,
                           float contentAlpha, int winW, int winH, bool containersAnimating,
                           ScrollState& listScroll) {
    const float displayW = static_cast<float>(winW), displayH = static_cast<float>(winH);
    const float x1 = startX + containerWidth;
    const float contentA = std::clamp(contentAlpha, 0.f, 1.f);
    ImFont* font = ImGui::GetFont();
    dl->PushClipRect(ImVec2(startX, containersStartY), ImVec2(displayW, displayH), true);
    
    constexpr float kSoonOpacity = 180.f / 255.f;
    const float soonA = contentA * kSoonOpacity;
    const ImVec4 soonText(theme.text.x * 0.86f, theme.text.y * 0.86f, theme.text.z * 0.86f, 1.f);
    
    for (const ComingSoonCard& card : cards) {
        const ImVec2 pos(startX, card.y);
        const ImVec2 end(x1, card.y + containerHeight);
        dl->AddRectFilled(pos, end, ColorA(theme.cardBg, soonA), kCardRounding);
        dl->AddRect(pos, end, ColorA(theme.cardOutline, soonA * 0.7f), kCardRounding, 0, kCardBorder);
        dl->AddText(font, font->FontSize, ImVec2(startX + kCardPadX, card.y + kCardPadY + 4.f),
            ColorA(soonText, soonA), card.name);
        dl->AddText(font, font->FontSize, ImVec2(startX + kCardPadX, card.y + kCardPadY + 24.f),
            ColorA(theme.textMuted, soonA), "Coming soon");
        if (card.tex) {
            const ImVec2 ip(x1 - kIconSize - kIconPad, pos.y + (containerHeight - kIconSize)/2.f);
            dl->AddImageRounded(reinterpret_cast<ImTextureID>(card.tex), ip,
                ImVec2(ip.x + kIconSize, ip.y + kIconSize), ImVec2(0, 0), ImVec2(1, 1),
                ColorA(kOpaqueWhite, soonA * 0.28f), kIconRound);
        }
    }
    dl->PopClipRect();
}

void DrawAvatarFooter(ImDrawList* dl, Theme& theme, const UiGpuResources& gpu, float contentAlpha, int winH) {
    (void)dl; (void)gpu;
    constexpr float kImageSize = 40.f;
    const ImVec2 ip(20.f, static_cast<float>(winH) - kImageSize - 25.f);
    ImGui::SetCursorPos(ImVec2(ip.x, ip.y + (kImageSize - ImGui::GetTextLineHeight()) / 2.f));
    const ScopedTextColor color(theme.text, 0.9f * contentAlpha);
    ImGui::TextUnformatted("account");
}

void DrawWindowChrome(ImDrawList* dl, Theme& theme, UiState& ui, float contentAlpha, float dt,
                      int winW, bool modalOpen, DashActions& out) {
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::SetCursorPos(ImVec2(static_cast<float>(winW) - 35.f, 20.f));
    ImGui::InvisibleButton("CloseButton", ImVec2(20.f, 20.f));
    HoverLerp(ui.closeHover, ImGui::IsItemHovered(), dt);
    if (ImGui::IsItemClicked() && !modalOpen) out.close = true;
    const ImVec4 closeCol =
        Lerp4(ImVec4(theme.textDim.x, theme.textDim.y, theme.textDim.z, contentAlpha),
              ImVec4(theme.text.x, theme.text.y, theme.text.z, contentAlpha), ui.closeHover);
    dl->AddLine(ImVec2(static_cast<float>(winW) - 30.f, 25.f),
                ImVec2(static_cast<float>(winW) - 20.f, 35.f), ImGui::GetColorU32(closeCol), 2.f);
    dl->AddLine(ImVec2(static_cast<float>(winW) - 30.f, 35.f),
                ImVec2(static_cast<float>(winW) - 20.f, 25.f), ImGui::GetColorU32(closeCol), 2.f);

    ImGui::SetCursorPos(ImVec2(static_cast<float>(winW) - 60.f, 25.f));
    ImGui::InvisibleButton("MinimizeButton", ImVec2(20.f, 20.f));
    HoverLerp(ui.minimizeHover, ImGui::IsItemHovered(), dt);
    if (ImGui::IsItemClicked() && !modalOpen) out.minimize = true;
    const ImVec4 minCol =
        Lerp4(ImVec4(theme.textDim.x, theme.textDim.y, theme.textDim.z, contentAlpha),
              ImVec4(theme.text.x, theme.text.y, theme.text.z, contentAlpha), ui.minimizeHover);
    dl->AddLine(ImVec2(static_cast<float>(winW) - 55.f, 30.f),
                ImVec2(static_cast<float>(winW) - 43.f, 30.f), ImGui::GetColorU32(minCol), 2.f);
    ImGui::PopStyleVar();
}

void DrawModalDim(ImDrawList* dl, const ModalState& modal, int winW, int winH, std::uint64_t now) {
    if (!modal.open && !modal.closing) return;
    const float modalT =
        modal.closing
            ? 1.f - std::min(static_cast<float>(now - modal.closeTime) / kModalFadeMs, 1.f)
            : std::min(static_cast<float>(now - modal.openTime) / kModalFadeMs, 1.f);
    const float dimA = EaseInOutCubic(modalT) * 0.55f;
    dl->AddRectFilled(ImVec2(0, 0), ImVec2(static_cast<float>(winW), static_cast<float>(winH)),
                      ImGui::GetColorU32(ImVec4(0, 0, 0, dimA)));
}

} // namespace

void DrawDashboard(Theme& theme, UiState& ui, const UiGpuResources& gpu, data::CheatStore& store,
                   float contentAlpha, const FrameInput& frame, DashActions& out) {
    const int winW = frame.winW;
    const int winH = frame.winH;
    const float dt = frame.dt;
    const std::uint64_t now = frame.nowMs;
    const time_t nowUnix = frame.nowUnix;

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(static_cast<float>(winW), static_cast<float>(winH)));
    ImGui::Begin("MainWindow", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus |
                     ImGuiWindowFlags_NoNavFocus);
    ImGui::SetScrollY(0);

    ImDrawList* dl = ImGui::GetWindowDrawList();

    bool slotActive[kGameCatalogCount]{};
    unsigned activeMask = 0;
    for (int i = 0; i < kGameCatalogCount; ++i) {
        if (!kGameCatalog[i].id) continue;
        if (const auto* info = store.Find(kGameCatalog[i].id)) {
            slotActive[i] = info->IsActive(nowUnix);
            if (slotActive[i]) activeMask |= (1u << i);
        }
    }
    if (!ui.prevActiveValid) {
        ui.prevActiveMask = activeMask;
        ui.prevActiveValid = true;
    }

    constexpr float kDimSpeed = 2.f;
    for (int i = 0; i < kGameCatalogCount && i < kMaxGameSlots; ++i) {
        const float target = (!kGameCatalog[i].id || slotActive[i]) ? 1.f : kInactiveDim;
        if (!ui.dimReady || contentAlpha < 0.01f)
            ui.gameDim[i] = target;
        else {
            ui.gameDim[i] += (target - ui.gameDim[i]) * kDimSpeed * dt;
            ui.gameDim[i] = std::clamp(ui.gameDim[i], kInactiveDim, 1.f);
        }
    }
    ui.dimReady = true;

    const float headerH = DrawHeader(theme, contentAlpha, winW, dt);
    ImGui::PushFont(theme.fontDefault);
    DrawSideNav(theme, ui, contentAlpha, dt, ui.modal.open, out);
    
    if (!ui.statusMessage.empty()) {
        ImGui::SetCursorPos(ImVec2(90.f, headerH + 5.f));
        ImGui::PushFont(theme.fontSmall ? theme.fontSmall : theme.fontDefault);
        const ScopedTextColor color(theme.textMuted, contentAlpha);
        ImGui::TextUnformatted(ui.statusMessage.c_str());
        ImGui::PopFont();
    }

    float newsBottomY = headerH + 15.f;
    if (ui.newsLoaded && !ui.newsText.empty()) {
        ImGui::SetCursorPos(ImVec2(90.f, newsBottomY));
        ImGui::PushFont(theme.fontDefault);
        ImGui::PushTextWrapPos(static_cast<float>(winW) - 20.f);
        const ScopedTextColor color(theme.textSoft, 0.85f * contentAlpha);
        ImGui::TextUnformatted(ui.newsText.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopFont();
        int lines = 1;
        for (char c : ui.newsText) if (c == '\n') ++lines;
        newsBottomY += static_cast<float>(lines) * 16.f + 30.f;
    }

    constexpr float kScrollSpeed = 60.f;
    constexpr float kContainerGap = 12.f;
    const float kContainersStartY = newsBottomY;
    const float startX = 90.f;
    const float containerWidth = static_cast<float>(winW) - startX - 20.f;
    const float containersAreaHeight = static_cast<float>(winH) - kContainersStartY;

    if (ui.containers.empty()) {
        ui.containers.reserve(static_cast<size_t>(kGameCatalogCount));
        for (int i = 0; i < kGameCatalogCount; ++i)
            ui.containers.push_back({i, slotActive[i], 0, 0, 0, 1.f});
        std::stable_sort(ui.containers.begin(), ui.containers.end(), ActiveFirstThenIndex);
        for (size_t i = 0; i < ui.containers.size(); ++i) {
            const float y = kContainersStartY + static_cast<float>(i) * (kCardHeight + kContainerGap);
            ui.containers[i].targetY = ui.containers[i].currentY = ui.containers[i].startY = y;
        }
    } else {
        for (auto& c : ui.containers) {
            if (c.gameIdx >= 0 && c.gameIdx < kGameCatalogCount) c.isActive = slotActive[c.gameIdx];
        }
    }

    if (ui.prevActiveMask != activeMask) {
        std::stable_sort(ui.containers.begin(), ui.containers.end(), ActiveFirstThenIndex);
        ui.prevActiveMask = activeMask;
        for (auto& c : ui.containers) {
            c.startY = c.currentY;
            c.animationProgress = 0.f;
        }
        if (gpu.blur) gpu.blur->InvalidateCache();
    }

    for (size_t i = 0; i < ui.containers.size(); ++i)
        ui.containers[i].targetY = kContainersStartY + static_cast<float>(i) * (kCardHeight + kContainerGap);
    TickContainerAnim(ui, dt);

    const float cardCount = static_cast<float>(ui.containers.size());
    const float contentH = cardCount * (kCardHeight + kContainerGap);
    const float maxScroll = std::max(0.f, contentH - containersAreaHeight);

    {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const bool over = !ui.modal.open && mouse.x >= startX &&
                          mouse.x <= startX + containerWidth && mouse.y >= kContainersStartY &&
                          mouse.y <= kContainersStartY + containersAreaHeight;
        TickScroll(ui.listScroll, maxScroll, ImGui::GetIO().MouseWheel, over, dt, now, kScrollSpeed);
    }

    ImGui::PushClipRect(ImVec2(startX, kContainersStartY),
                        ImVec2(static_cast<float>(winW), static_cast<float>(winH)), true);
    const float overscrollT = ui.listScroll.overscroll * 0.5f;

    std::vector<ComingSoonCard> comingSoon;
    comingSoon.reserve(static_cast<size_t>(kGameCatalogCount));

    for (auto& container : ui.containers) {
        const float baseY = container.currentY - ui.listScroll.current + overscrollT;
        const ImVec2 pos(startX, baseY);
        const int idx = container.gameIdx;
        if (idx < 0 || idx >= kGameCatalogCount) continue;

        const GameCatalogEntry& entry = kGameCatalog[idx];
        const bool isComingSoon = (entry.id == nullptr);
        const data::CheatInfo* info = isComingSoon ? nullptr : store.Find(entry.id);
        const char* gameName = entry.fallbackName;
        if (info && !info->name.empty()) gameName = info->name.c_str();
        ID3D11ShaderResourceView* texture = gpu.ArtFor(entry.art);
        const bool isActive = slotActive[idx];
        const float dim = (idx < kMaxGameSlots) ? ui.gameDim[idx] : 1.f;

        if (isComingSoon) {
            comingSoon.push_back({baseY, gameName, texture});
            continue;
        }

        // Hover detection
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const bool hovered = !ui.modal.open && mouse.x >= pos.x && mouse.x <= pos.x + containerWidth &&
                             mouse.y >= pos.y && mouse.y <= pos.y + kCardHeight;
        float& hoverState = ui.cardHover[idx];
        hoverState += ((hovered ? 1.f : 0.f) - hoverState) * std::min(1.f, 8.f * dt);
        
        ImGui::SetCursorPos(pos);
        char gameBtnId[32];
        std::snprintf(gameBtnId, sizeof(gameBtnId), "##game%d", idx);
        ImGui::InvisibleButton(gameBtnId, ImVec2(containerWidth, kCardHeight));
        if (ImGui::IsItemClicked() && !ui.modal.open && !ui.modal.closing &&
            now >= ui.modal.reopenBlockUntil && isActive) {
            OpenGameModal(ui, store, entry.id, now, nowUnix);
        }

        const std::string status = nl::FormatSubscriptionStatus(
            isActive, info ? info->lifetime : false, info ? info->license : 0, nowUnix);
        DrawGameCard(dl, theme, pos, containerWidth, kCardHeight, contentAlpha * dim, gameName,
                     isActive, status.c_str(), texture, hoverState);
    }
    ImGui::PopClipRect();

    if (!comingSoon.empty() && contentAlpha > 0.01f) {
        DrawComingSoonOverlay(dl, theme, gpu, comingSoon, startX, containerWidth, kCardHeight,
                              kContainersStartY, contentAlpha, winW, winH, false, ui.listScroll);
    }

    DrawScrollBar(dl, ui.listScroll, maxScroll, contentH, 0.f, kContainersStartY,
                  containersAreaHeight, static_cast<float>(winW) - 3.f, theme.accent);

    DrawAvatarFooter(dl, theme, gpu, contentAlpha, winH);
    DrawWindowChrome(dl, theme, ui, contentAlpha, dt, winW, ui.modal.open, out);
    DrawModalDim(dl, ui.modal, winW, winH, now);

    ImGui::PopFont();
    ImGui::End();
}

} // namespace nl::ui