#pragma once
// Shared between the menu shell (shell.cpp), live stats (stats.cpp) and the page files.
#include "imgui.h"
#include "theme.h"
#include "widgets.h"
#include <string>
#include <vector>

namespace menu
{
enum Page { Page_Home, Page_Visuals, Page_Audio, Page_Controls, Page_Configs, Page_Settings, Page_COUNT };

struct NavItem { unsigned icon; const char* label; const char* subs[3]; int subCount; };
extern const NavItem kNav[Page_COUNT];

constexpr int kBindCount = 12;
constexpr int kMovementBinds = 7;
extern const char* const kBindNames[kBindCount];
extern const ImGuiKey kBindDefaults[kBindCount];

struct Config { std::string name, desc; double created, used; };

struct State
{
    int    page = Page_Home;
    int    sub[Page_COUNT] = {};
    double changedAt = -10.0;
    bool   resetScroll = false;
    bool   compact = false;
    bool   showDemo = false;

    // Home
    bool     perfMode = true, overlay = false, autosave = true, cloudSync = true, dnd = false;
    ImGuiKey perfKey = ImGuiKey_F5, overlayKey = ImGuiKey_F1;
    float    diag = -1.0f;
    char     diagResult[96] = "";

    // Visuals
    int   displayMode = 1, resolution = 1, aa = 2, upscaler = 1, quality = 2, grade = 0;
    bool  vsync = true, hdr = false;
    float frameCap = 165, fov = 95, brightness = 55, contrast = 50, gamma = 2.2f, renderScale = 100;
    bool  post = true, bloom = true, motionBlur = false, vignette = true, grain = false, chroma = false, photoMode = true;
    ImGuiKey photoKey = ImGuiKey_F9;
    float bloomCol[4] = { 0.55f, 0.36f, 0.96f, 1.0f }, vignetteCol[4] = { 0.94f, 0.31f, 0.61f, 1.0f };
    float bloomAmt = 0.6f, bloomThreshold = 0.8f, bloomRadius = 6, vignetteAmt = 35, grainAmt = 12;
    float temperature = 0, tint = 0, saturation = 100, exposure = 0;
    float ambient[4] = { 0.36f, 0.42f, 0.96f, 1.0f }, shadows[4] = { 0.10f, 0.12f, 0.22f, 1.0f };
    float highlights[4] = { 1.0f, 0.93f, 0.82f, 1.0f }, uiTint[4] = { 0.55f, 0.36f, 0.96f, 1.0f };

    // Audio
    float master = 80, music = 45, sfx = 70, voice = 65, micSens = 60, bassBoost = 0;
    int   outDev = 0, inDev = 0, eqPreset = 0;
    bool  spatial = true, ptt = true, muteUnfocused = false, noiseSupp = true, loudness = false;
    ImGuiKey talkKey = ImGuiKey_V;
    float eq[10] = {};

    // Controls
    float sens = 1.25f, ads = 0.80f, deadzone = 8, trigger = 15;
    bool  rawInput = true, invertY = false, accel = false, vibration = true;
    int   padLayout = 0;
    ImGuiKey binds[kBindCount] = { ImGuiKey_W, ImGuiKey_S, ImGuiKey_A, ImGuiKey_D, ImGuiKey_Space, ImGuiKey_LeftCtrl, ImGuiKey_LeftShift,
                                   ImGuiKey_E, ImGuiKey_Tab, ImGuiKey_R, ImGuiKey_M, ImGuiKey_F12 };

    // Configs
    std::vector<Config> configs;
    int  activeConfig = 0, selConfig = 0, pendingDelete = -1;
    char newConfig[40] = "";

    // Settings
    int  language = 0;
    bool startup = false, tray = true, crash = true, updates = true;
};
extern State S;

// Navigation / feedback
void SetPage(int page, int sub = -1);
void Notice(ui::ToastKind kind, unsigned icon, const char* text, const char* highlight = nullptr);

// Two-column page layout (a table with a 12px gutter)
bool BeginColumns(const char* id, float left_weight = 0.5f);
void NextColumn();
void EndColumns();

// Live stats, all measured from this process (stats.cpp)
namespace stats
{
constexpr int kHist = 120;   // per-frame render times
constexpr int kSpark = 40;   // 4 Hz samples for the sparklines
struct Live
{
    float  renderMs[kHist] = {};
    int    renderOff = 0;
    float  renderSpark[kSpark] = {}, memSpark[kSpark] = {}, cpuSpark[kSpark] = {};
    int    sparkOff = 0;
    float  renderAvg = 0.0f, mem = 0.0f, cpu = 0.0f;
    double startTime = 0.0;
};
extern Live L;
void Init();
void Update(float frame_work_ms);
void Range(const float* v, int n, float* lo, float* hi);
}

// Activity log (stats.cpp)
struct Activity { double t; unsigned icon; std::string text; };
const std::vector<Activity>& Activities();
void Log(unsigned icon, const char* fmt, ...);
void Ago(double t, char* out, size_t n);
void FormatUptime(char* out, size_t n);

// Pages (page_*.cpp)
void HomeOverview();
void HomeActivity();
void VisualsDisplay();
void VisualsEffects();
void VisualsColors();
void AudioMixer();
void AudioEqualizer();
void ControlsMouse();
void ControlsKeybinds();
void PageConfigs();
void SettingsAppearance();
void SettingsAbout();
}
