#include "theme.h"
#include "imgui_internal.h"
#ifdef IMGUI_ENABLE_FREETYPE
#include "imgui_freetype.h"
#endif
#include <cmath>
#include <cstdio>
#include <initializer_list>

namespace theme
{
Palette P;
Fonts   F;
float   dpi        = 1.0f;
float   rounding   = 10.0f;
bool    animations = true;
bool    glow       = true;
int     accent     = 0;

ImVec4 Hex(unsigned rgb, float a)
{
    return ImVec4(((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, a);
}

const AccentPreset kAccents[] = {
    { "Nebula",  Hex(0x8B5CF6), Hex(0x6366F1) },
    { "Ocean",   Hex(0x22B8F0), Hex(0x3B6CF6) },
    { "Mint",    Hex(0x2DD4A0), Hex(0x0EA5A4) },
    { "Sunset",  Hex(0xFB7A3C), Hex(0xEF4466) },
    { "Rose",    Hex(0xF0509C), Hex(0xA855F7) },
    { "Gold",    Hex(0xF5B90F), Hex(0xF97316) },
    { "Red",     Hex(0xF2484B), Hex(0xC81E3C) },
};
const int kAccentCount = IM_ARRAYSIZE(kAccents);

static ImVec4 s_targetA, s_targetB;

ImU32 Col(const ImVec4& c, float alpha)
{
    return ImGui::GetColorU32(ImVec4(c.x, c.y, c.z, c.w * alpha));
}

ImVec4 Mix(const ImVec4& a, const ImVec4& b, float t)
{
    return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
}

ImVec4 WithAlpha(const ImVec4& c, float a) { return ImVec4(c.x, c.y, c.z, a); }

static bool Exists(const char* path)
{
    FILE* f = fopen(path, "rb");
    if (f)
        fclose(f);
    return f != nullptr;
}

static ImFont* TryLoad(std::initializer_list<const char*> paths, float size, int face = 0)
{
    ImGuiIO& io = ImGui::GetIO();
    for (const char* path : paths)
    {
        if (!Exists(path))
            continue;
        ImFontConfig cfg;
        cfg.FontNo = face;
        if (ImFont* font = io.Fonts->AddFontFromFileTTF(path, size, &cfg))
            return font;
    }
    return nullptr;
}

void LoadFonts()
{
#ifdef IMGUI_ENABLE_FREETYPE
    // FreeType is the loader when compiled in. Light hinting snaps glyphs vertically only (like ClearType),
    // which keeps small UI text crisp without distorting letter shapes or spacing.
    ImGui::GetIO().Fonts->FontLoaderFlags = ImGuiFreeTypeLoaderFlags_LightHinting;

    // Windows 11: Segoe UI Variable, the system UI face. Its "Text" optical size is drawn for 12-16 px UI text.
    // Named instances are picked through the face index: (instance << 16). See ftinfo / fvar for the list.
    const char* var = "C:\\Windows\\Fonts\\SegUIVar.ttf";
    if (Exists(var))
    {
        F.regular  = TryLoad({ var }, kTextM, 8 << 16);   // Regular (Text)
        F.semibold = TryLoad({ var }, kTextM, 9 << 16);   // Semibold Text
        F.bold     = TryLoad({ var }, kTextM, 10 << 16);  // Bold Text
    }
#endif
    // Windows 10 (or stb_truetype builds): classic Segoe UI. First font added is the default one.
    if (!F.regular)  F.regular  = TryLoad({ "C:\\Windows\\Fonts\\segoeui.ttf" }, kTextM);
    if (!F.regular)  F.regular  = ImGui::GetIO().Fonts->AddFontDefault();
    if (!F.semibold) F.semibold = TryLoad({ "C:\\Windows\\Fonts\\seguisb.ttf" }, kTextM);
    if (!F.semibold) F.semibold = F.regular;
    if (!F.bold)     F.bold     = TryLoad({ "C:\\Windows\\Fonts\\segoeuib.ttf" }, kTextM);
    if (!F.bold)     F.bold     = F.semibold;
    F.icons = TryLoad({ "C:\\Windows\\Fonts\\SegoeIcons.ttf", "C:\\Windows\\Fonts\\segmdl2.ttf" }, kTextM);
    F.mono  = TryLoad({ "C:\\Windows\\Fonts\\CascadiaMono.ttf", "C:\\Windows\\Fonts\\consola.ttf" }, kTextS);
    if (!F.mono) F.mono = F.regular;
}

void Setup()
{
    // Figma: Nebula / Dark
    P.bg           = Hex(0x0B0C10);
    P.sidebar      = Hex(0x0E0F14);
    P.group        = Hex(0x111219);
    P.field        = Hex(0x17181F);
    P.hover        = Hex(0x1D1F29);
    P.border       = Hex(0x1B1D27);
    P.borderStrong = Hex(0x2B2E3B);
    P.text         = Hex(0xE9EAF0);
    P.textDim      = Hex(0xA0A4B4);
    P.textMute     = Hex(0x5E6273);
    P.success      = Hex(0x22C55E);
    P.warning      = Hex(0xF5A524);
    P.danger       = Hex(0xEF4444);

    SetDpi(dpi);
    SetAccent(0, true);
}

void SetDpi(float scale)
{
    dpi = scale;
    ImGuiStyle& s = ImGui::GetStyle();
    s.FontSizeBase      = kTextM;
    s.FontScaleDpi      = dpi;
    s.WindowPadding     = ImVec2(dp(8), dp(8));
    s.FramePadding      = ImVec2(dp(10), dp(5));
    s.ItemSpacing       = ImVec2(dp(8), dp(4));
    s.ItemInnerSpacing  = ImVec2(dp(6), dp(4));
    s.CellPadding       = ImVec2(dp(6), 0);
    s.ScrollbarSize     = dp(6);
    s.GrabMinSize       = dp(8);
    s.WindowBorderSize  = 0.0f;
    s.ChildBorderSize   = 1.0f;
    s.PopupBorderSize   = 1.0f;
    s.FrameBorderSize   = 0.0f;
    s.WindowRounding    = dp(10);
    s.ScrollbarRounding = dp(6);
    s.TabRounding       = dp(6);
    s.DisabledAlpha     = 0.38f;
    s.SelectableTextAlign = ImVec2(0.0f, 0.5f);
    s.HoverDelayShort   = 0.25f;
}

void SetAccent(int idx, bool instant)
{
    accent = ImClamp(idx, 0, kAccentCount - 1);
    s_targetA = kAccents[accent].a;
    s_targetB = kAccents[accent].b;
    if (instant)
    {
        P.accent = s_targetA;
        P.accent2 = s_targetB;
    }
}

static void ApplyColors()
{
    ImGuiStyle& s = ImGui::GetStyle();
    s.ChildRounding      = RGroup();
    s.FrameRounding      = RControl();
    s.PopupRounding      = dp(rounding * 0.8f);
    s.GrabRounding       = RControl();
    s.SelectableRounding = dp(rounding * 0.5f);

    ImVec4* c = s.Colors;
    const ImVec4 none(0, 0, 0, 0);
    c[ImGuiCol_Text]                  = P.text;
    c[ImGuiCol_TextDisabled]          = P.textMute;
    c[ImGuiCol_WindowBg]              = P.bg;
    c[ImGuiCol_ChildBg]               = none;
    c[ImGuiCol_PopupBg]               = Hex(0x14151C);
    c[ImGuiCol_Border]                = P.borderStrong;
    c[ImGuiCol_BorderShadow]          = none;
    c[ImGuiCol_FrameBg]               = P.field;
    c[ImGuiCol_FrameBgHovered]        = P.hover;
    c[ImGuiCol_FrameBgActive]         = Mix(P.field, P.accent, 0.10f);
    c[ImGuiCol_TitleBg]               = P.bg;
    c[ImGuiCol_TitleBgActive]         = P.bg;
    c[ImGuiCol_TitleBgCollapsed]      = P.bg;
    c[ImGuiCol_MenuBarBg]             = P.sidebar;
    c[ImGuiCol_ScrollbarBg]           = none;
    c[ImGuiCol_ScrollbarGrab]         = WithAlpha(P.text, 0.08f);
    c[ImGuiCol_ScrollbarGrabHovered]  = WithAlpha(P.text, 0.16f);
    c[ImGuiCol_ScrollbarGrabActive]   = WithAlpha(P.accent, 0.70f);
    c[ImGuiCol_CheckMark]             = P.accent;
    c[ImGuiCol_SliderGrab]            = P.accent;
    c[ImGuiCol_SliderGrabActive]      = P.accent2;
    c[ImGuiCol_Button]                = P.field;
    c[ImGuiCol_ButtonHovered]         = P.hover;
    c[ImGuiCol_ButtonActive]          = Mix(P.field, P.accent, 0.25f);
    c[ImGuiCol_Header]                = WithAlpha(P.accent, 0.16f);
    c[ImGuiCol_HeaderHovered]         = P.hover;
    c[ImGuiCol_HeaderActive]          = WithAlpha(P.accent, 0.24f);
    c[ImGuiCol_Separator]             = P.border;
    c[ImGuiCol_SeparatorHovered]      = P.borderStrong;
    c[ImGuiCol_SeparatorActive]       = P.accent;
    c[ImGuiCol_ResizeGrip]            = none;
    c[ImGuiCol_ResizeGripHovered]     = none;
    c[ImGuiCol_ResizeGripActive]      = none;
    c[ImGuiCol_InputTextCursor]       = P.accent;
    c[ImGuiCol_Tab]                   = P.field;
    c[ImGuiCol_TabHovered]            = P.hover;
    c[ImGuiCol_TabSelected]           = Mix(P.field, P.accent, 0.30f);
    c[ImGuiCol_TabSelectedOverline]   = P.accent;
    c[ImGuiCol_PlotLines]             = P.accent;
    c[ImGuiCol_PlotLinesHovered]      = P.accent2;
    c[ImGuiCol_PlotHistogram]         = P.accent;
    c[ImGuiCol_PlotHistogramHovered]  = P.accent2;
    c[ImGuiCol_TableHeaderBg]         = P.field;
    c[ImGuiCol_TableBorderStrong]     = P.border;
    c[ImGuiCol_TableBorderLight]      = P.border;
    c[ImGuiCol_TableRowBg]            = none;
    c[ImGuiCol_TableRowBgAlt]         = WithAlpha(P.text, 0.02f);
    c[ImGuiCol_TextLink]              = P.accent;
    c[ImGuiCol_TextSelectedBg]        = WithAlpha(P.accent, 0.35f);
    c[ImGuiCol_NavCursor]             = P.accent;
    c[ImGuiCol_ModalWindowDimBg]      = ImVec4(0.02f, 0.02f, 0.04f, 0.62f);
}

void Tick(float dt)
{
    // Clamp: with on-demand rendering the first frame after an idle period has a huge dt.
    float k = animations ? 1.0f - expf(-9.0f * ImMin(dt, 1.0f / 30.0f)) : 1.0f;
    P.accent  = ImLerp(P.accent, s_targetA, k);
    P.accent2 = ImLerp(P.accent2, s_targetB, k);
    if (!IsAnimating())
    {
        P.accent = s_targetA;
        P.accent2 = s_targetB;
    }
    ApplyColors();
}

bool IsAnimating()
{
    auto far = [](const ImVec4& a, const ImVec4& b) { return fabsf(a.x - b.x) + fabsf(a.y - b.y) + fabsf(a.z - b.z) > 0.004f; };
    return far(P.accent, s_targetA) || far(P.accent2, s_targetB);
}
}
