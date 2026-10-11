#pragma once
#include "imgui.h"

// Compact, animated widgets matching the Figma component sheet (Checkbox, Toggle, Slider,
// Dropdown, Keybind, Swatch, Tab). Everything goes through ImGui's item system
// (ItemAdd/ButtonBehavior), so IDs, keyboard nav and BeginDisabled() keep working.
namespace ui
{
// Segoe Fluent Icons codepoints (Windows 11; Segoe MDL2 Assets on Windows 10).
namespace icon
{
constexpr unsigned Home = 0xE80F, Eye = 0xE890, Volume = 0xE767, Game = 0xE7FC, Folder = 0xE8B7, Sliders = 0xE9E9,
                   Search = 0xE721, Minimize = 0xE921, Close = 0xE8BB, Monitor = 0xE7F4, Sun = 0xE706, Zap = 0xE945,
                   Palette = 0xE790, Sparkle = 0xF4A5, Check = 0xE73E, ChevronDown = 0xE70D, ChevronRight = 0xE76C,
                   Keyboard = 0xE765, Mouse = 0xE962, Headphone = 0xE7F6, Mic = 0xE720, Music = 0xE8D6, Clock = 0xE823,
                   History = 0xE81C, Gauge = 0xEC4A, Info = 0xE946, Warning = 0xE7BA, Pulse = 0xE9D9, Shield = 0xEA18,
                   Globe = 0xE774, Copy = 0xE8C8, Trash = 0xE74D, Plus = 0xE710, Refresh = 0xE72C, Save = 0xE74E,
                   Code = 0xE943, Sync = 0xE895, Layers = 0xE81E, Bell = 0xEA8F, Flag = 0xE7C1, Database = 0xEE94,
                   Chart = 0xE9D2, Cancel = 0xE711, Star = 0xE735, Puzzle = 0xEA86, Gear = 0xE713, Contact = 0xE77B;
}

struct Ico
{
    char s[5];
    explicit Ico(unsigned cp);
    operator const char*() const { return s; }
};

// Per-frame bookkeeping: call once at the start of every frame.
void NewFrame();
// True when something on screen is still moving (the host keeps rendering at full rate).
bool IsAnimating();

// Animation helpers: values keyed by ImGuiID, exponentially smoothed toward target.
// keep_awake=false for decorative motion that may step at the idle refresh rate.
float   Anim(ImGuiID id, float target, float speed = 14.0f, bool keep_awake = true);
float   Ease(float t);
ImGuiID SubId(ImGuiID id, int k);

// Drawing helpers
void   DrawIcon(ImDrawList* dl, unsigned cp, ImVec2 center, float size, ImU32 col);
void   TextAt(ImDrawList* dl, ImFont* f, float size, ImVec2 pos, ImU32 col, const char* text, const char* end = nullptr);
float  TextFit(ImDrawList* dl, ImFont* f, float size, ImVec2 pos, ImU32 col, const char* text, float max_w, const char* end = nullptr); // ellipsis
ImVec2 Measure(ImFont* f, float size, const char* text, const char* end = nullptr);
float  LineH(ImFont* f, float size);
void   GradientRect(ImDrawList* dl, ImVec2 a, ImVec2 b, ImVec4 c0, ImVec4 c1, float rounding, ImDrawFlags flags = 0, bool vertical = false, float alpha = 1.0f);
void   RadialGlow(ImDrawList* dl, ImVec2 center, float radius, ImVec4 col, float alpha);
void   SoftShadow(ImDrawList* dl, ImVec2 a, ImVec2 b, float rounding, ImVec4 col, float alpha, float spread);
void   BrandMark(ImDrawList* dl, ImVec2 pos, float size, ImVec4 c0, ImVec4 c1, float alpha = 1.0f); // logo tile

// Themed tooltips (compact, bordered) instead of ImGui's default ones.
void Tooltip(const char* text);
void ItemTooltip(const char* text);     // shows for the last item after the standard hover delay

// Thin overlay scrollbar for the current window; create the window with ImGuiWindowFlags_NoScrollbar.
void OverlayScrollbar(const char* id);

// Labels submitted by widgets are forwarded here while set (used to build the search index).
using LabelSink = void (*)(const char* label, const char* label_end);
void SetLabelSink(LabelSink sink);

// Groups: titled cards with a 34px header. Pass `enabled` to get a header switch that
// disables the group body when off.
bool BeginGroup(const char* id, unsigned icon, const char* title, bool* enabled = nullptr);
void EndGroup();
void Caption(const char* text);
void Divider();

// Rows
bool Checkbox(const char* label, bool* v);
bool CheckboxColor(const char* label, bool* v, float col[4]);
bool CheckboxKey(const char* label, bool* v, ImGuiKey* key);
bool Slider(const char* label, float* v, float vmin, float vmax, const char* fmt = "%.0f");
bool SliderInt(const char* label, int* v, int vmin, int vmax, const char* fmt = "%d");
bool Combo(const char* label, int* current, const char* const* items, int count);
bool Keybind(const char* label, ImGuiKey* key);
bool ColorRow(const char* label, float col[4]);
bool Pills(const char* id, int* current, const char* const* items, int count, float width = 0.0f); // width: 0 fill, <0 hug
bool ToggleMini(const char* id, bool* v);
bool AccentPicker();
void KeyValue(const char* key, const char* value, bool mono = false);
const char* KeyLabel(ImGuiKey key);
bool IsCapturingKey();   // a keybind chip is waiting for input (suppress global hotkeys)

// Buttons
enum ButtonKind { Btn_Primary, Btn_Ghost, Btn_Danger };
bool Button(const char* label, unsigned icon = 0, ButtonKind kind = Btn_Primary, ImVec2 size = ImVec2(0, 0));
bool IconButton(const char* id, unsigned icon, const char* tooltip = nullptr, float size = 0.0f, bool danger = false);
void Badge(const char* text, ImVec4 col);

// Data viz
void Sparkline(ImDrawList* dl, ImVec2 a, ImVec2 b, const float* v, int n, int offset, float vmin, float vmax, float alpha = 1.0f);
void LineChart(const char* id, const float* v, int n, int offset, float vmin, float vmax, float height, const char* fmt);
void Ring(const char* id, float frac, const char* value, const char* label, float radius);
void StatCell(const char* id, const char* label, const char* value, const float* hist, int n, int offset, float vmin, float vmax, float width, float height);
bool Equalizer(const char* id, float* bands, int n, const char* const* labels, float height);
void LevelMeter(const char* id, float level, float height);
void Progress(float frac, float height);

// Notifications: compact single-line log entries, "<text> <highlight>", bottom-right.
enum ToastKind { Toast_Info, Toast_Success, Toast_Warning, Toast_Error };
void Notify(ToastKind kind, const char* text, const char* highlight = nullptr);
void RenderToasts();
bool ToastsActive();
}
