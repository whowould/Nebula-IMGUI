#pragma once
#include "imgui.h"

// Design tokens. Values mirror the "Nebula" variable collection in the Figma file
// (Nebula — Compact ImGui Menu): bg/*, border/*, text/*, accent/*, radius/*.
namespace theme
{
struct Palette
{
    ImVec4 bg, sidebar, group, field, hover, border, borderStrong;
    ImVec4 text, textDim, textMute;
    ImVec4 accent, accent2;
    ImVec4 success, warning, danger;
};

struct Fonts
{
    ImFont* regular  = nullptr;
    ImFont* semibold = nullptr;
    ImFont* bold     = nullptr;
    ImFont* icons    = nullptr;
    ImFont* mono     = nullptr;
};

struct AccentPreset { const char* name; ImVec4 a, b; };

extern Palette P;
extern Fonts   F;
extern float   dpi;
extern float   rounding;    // radius/group (unscaled px); controls and checks derive from it
extern bool    animations;
extern bool    glow;
extern int     accent;
extern const AccentPreset kAccents[];
extern const int kAccentCount;

// Type scale. Every text size in the UI comes from here.
// Values are ImGui font sizes, which are the full line height (ascender + descender), not the em size
// designers use. For Segoe UI that line is 1.33 em, so each value is the intended em size x 1.33.
constexpr float kEm     = 1.33f;
constexpr float kTextXS = 11.0f * kEm;   // section caps, axis ticks, badges, timestamps
constexpr float kTextS  = 12.0f * kEm;   // secondary lines, values, key chips, notifications
constexpr float kTextM  = 13.0f * kEm;   // row labels, buttons, inputs (ImGui base size)
constexpr float kTextL  = 14.0f * kEm;   // group and dialog titles
constexpr float kTextXL = 17.0f * kEm;   // page title, big numbers

inline float dp(float v) { return v * dpi; }
inline float RGroup()   { return dp(rounding); }
inline float RControl() { return dp(rounding * 0.6f); }
inline float RCheck()   { return dp(rounding * 0.4f); }

void LoadFonts();
void Setup();                      // palette, sizes and default accent
void SetDpi(float scale);          // re-applies every size for a new monitor scale (WM_DPICHANGED)
void SetAccent(int idx, bool instant = false);
void Tick(float dt);               // animates accent changes and pushes colors into ImGuiStyle
bool IsAnimating();                // accent transition still running

ImU32  Col(const ImVec4& c, float alpha = 1.0f);   // respects style.Alpha (page fades, disabled blocks)
ImVec4 Mix(const ImVec4& a, const ImVec4& b, float t);
ImVec4 WithAlpha(const ImVec4& c, float a);
ImVec4 Hex(unsigned rgb, float a = 1.0f);
}
