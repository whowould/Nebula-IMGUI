// Menu shell: sidebar, header (sub-tabs, search, window buttons), hotkeys and the per-frame root.
#include "menu.h"
#include "internal.h"
#include "app.h"
#include "imgui_internal.h"

#include <windows.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace theme;
using namespace ui;

namespace menu
{
const NavItem kNav[Page_COUNT] = {
    { icon::Home,    "Home",     { "Overview", "Activity" },          2 },
    { icon::Eye,     "Visuals",  { "Display", "Effects", "Colors" },  3 },
    { icon::Volume,  "Audio",    { "Mixer", "Equalizer" },            2 },
    { icon::Game,    "Controls", { "Mouse", "Keybinds" },             2 },
    { icon::Folder,  "Configs",  {},                                  0 },
    { icon::Sliders, "Settings", { "Appearance", "About" },           2 },
};

const char* const kBindNames[kBindCount] = { "Forward", "Back", "Strafe left", "Strafe right", "Jump", "Crouch", "Sprint",
                                             "Interact", "Inventory", "Reload", "Map", "Screenshot" };
const ImGuiKey kBindDefaults[kBindCount] = { ImGuiKey_W, ImGuiKey_S, ImGuiKey_A, ImGuiKey_D, ImGuiKey_Space, ImGuiKey_LeftCtrl, ImGuiKey_LeftShift,
                                             ImGuiKey_E, ImGuiKey_Tab, ImGuiKey_R, ImGuiKey_M, ImGuiKey_F12 };

State S;
static bool s_transitioning = false;

//-----------------------------------------------------------------------------
// Shared helpers
//-----------------------------------------------------------------------------

void SetPage(int page, int sub)
{
    page = ImClamp(page, 0, (int)Page_COUNT - 1);
    bool changed = page != S.page || (sub >= 0 && sub != S.sub[page]);
    S.page = page;
    if (sub >= 0 && sub < kNav[page].subCount)
        S.sub[page] = sub;
    if (changed)
    {
        S.changedAt = ImGui::GetTime();
        S.resetScroll = true;
    }
}

void Notice(ToastKind kind, unsigned ic, const char* text, const char* highlight)
{
    Log(ic, "%s%s%s", text, highlight ? " " : "", highlight ? highlight : "");
    if (!S.dnd || kind == Toast_Error)
        Notify(kind, text, highlight);
}

bool BeginColumns(const char* id, float left_weight)
{
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(dp(6), 0));
    if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::PopStyleVar();
        return false;
    }
    ImGui::TableSetupColumn("##l", ImGuiTableColumnFlags_WidthStretch, left_weight);
    ImGui::TableSetupColumn("##r", ImGuiTableColumnFlags_WidthStretch, 1.0f - left_weight);
    ImGui::TableNextColumn();
    return true;
}

void NextColumn() { ImGui::TableNextColumn(); }

void EndColumns()
{
    ImGui::EndTable();
    ImGui::PopStyleVar();
}

static void DrawView(int page, int sub)
{
    switch (page)
    {
    case Page_Home:     sub == 0 ? HomeOverview() : HomeActivity(); break;
    case Page_Visuals:  sub == 0 ? VisualsDisplay() : sub == 1 ? VisualsEffects() : VisualsColors(); break;
    case Page_Audio:    sub == 0 ? AudioMixer() : AudioEqualizer(); break;
    case Page_Controls: sub == 0 ? ControlsMouse() : ControlsKeybinds(); break;
    case Page_Configs:  PageConfigs(); break;
    case Page_Settings: sub == 0 ? SettingsAppearance() : SettingsAbout(); break;
    }
}

int ViewCount()
{
    int n = 0;
    for (const NavItem& it : kNav) n += ImMax(1, it.subCount);
    return n;
}

void SetView(int index)
{
    for (int p = 0; p < Page_COUNT; p++)
    {
        int n = ImMax(1, kNav[p].subCount);
        if (index < n) { SetPage(p, index); return; }
        index -= n;
    }
}

//-----------------------------------------------------------------------------
// Search index: built from the labels the widgets actually submit, so it never drifts
//-----------------------------------------------------------------------------

struct SearchEntry { std::string label; int page, sub; };
static std::vector<SearchEntry> s_index;
static bool s_indexed = false;
static int s_indexPage = 0, s_indexSub = 0;

static void AddEntry(const char* label, const char* end, int page, int sub)
{
    if (!end) end = label + strlen(label);
    if (end <= label)
        return;
    std::string l(label, end);
    for (const SearchEntry& e : s_index)
        if (e.page == page && e.sub == sub && e.label == l)
            return;
    s_index.push_back({ l, page, sub });
}

static void OnLabel(const char* label, const char* end) { AddEntry(label, end, s_indexPage, s_indexSub); }

// Runs every view once in an off-screen, input-less window and records the labels it submits.
static void BuildSearchIndex()
{
    s_index.clear();
    for (int p = 0; p < Page_COUNT; p++)
    {
        AddEntry(kNav[p].label, nullptr, p, 0);
        for (int s = 0; s < kNav[p].subCount; s++)
            AddEntry(kNav[p].subs[s], nullptr, p, s);
    }
    ImGui::SetNextWindowPos(ImVec2(-30000, -30000));
    ImGui::SetNextWindowSize(ImVec2(dp(600), dp(452)));
    ImGui::Begin("##search_index", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoBackground);
    SetLabelSink(&OnLabel);
    for (int p = 0; p < Page_COUNT; p++)
        for (int s = 0; s < ImMax(1, kNav[p].subCount); s++)
        {
            s_indexPage = p;
            s_indexSub = s;
            ImGui::PushID(p * 8 + s);
            DrawView(p, s);
            ImGui::PopID();
        }
    SetLabelSink(nullptr);
    ImGui::End();
    s_indexed = true;
}

//-----------------------------------------------------------------------------
// Sidebar
//-----------------------------------------------------------------------------

static ImTextureID s_avatar = (ImTextureID)0;
static std::string s_userName = "user";

static void Logo(ImDrawList* dl, ImVec2 p, float s)
{
    if (glow)
        RadialGlow(dl, p + ImVec2(s, s) * 0.5f, s * 1.4f, P.accent, 0.30f);
    BrandMark(dl, p, s, P.accent, P.accent2);
}

static void Avatar(ImDrawList* dl, ImVec2 c, float r)
{
    if (s_avatar)
        dl->AddImageRounded(s_avatar, c - ImVec2(r, r), c + ImVec2(r, r), ImVec2(0, 0), ImVec2(1, 1), Col(ImVec4(1, 1, 1, 1)), r);
    else
    {
        dl->AddCircleFilled(c, r, Col(P.hover), 32);
        DrawIcon(dl, icon::Contact, c, r, Col(P.textDim));
    }
    dl->AddCircle(c, r + dp(0.5f), Col(P.borderStrong), 32, 1.0f);
}

static void Sidebar(float width, float ct)
{
    ImGui::PushStyleColor(ImGuiCol_ChildBg, WithAlpha(P.sidebar, 0.9f));
    ImGui::BeginChild("##sidebar", ImVec2(width, 0), ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleColor();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
    dl->AddLine(ImVec2(wp.x + ws.x - 1, wp.y), ImVec2(wp.x + ws.x - 1, wp.y + ws.y), Col(P.border));
    const float la = ImSaturate(1.0f - ct * 1.8f);
    const float pad = dp(10), item_h = dp(34);

    // Brand
    float ls = dp(28);
    ImVec2 lp(wp.x + ImLerp(dp(14), (width - ls) * 0.5f, ct), wp.y + dp(12));
    Logo(dl, lp, ls);
    if (la > 0.01f)
    {
        TextAt(dl, F.semibold, dp(kTextL), lp + ImVec2(ls + dp(10), dp(-2)), Col(P.text, la), "nebula");
        TextAt(dl, F.regular, dp(kTextXS), lp + ImVec2(ls + dp(10), dp(17)), Col(P.textMute, la), "v2.4 \xC2\xB7 stable");
    }

    // Navigation
    float ys[Page_COUNT];
    float y = dp(60);
    auto section = [&](const char* label) {
        if (la > 0.01f)
            TextAt(dl, F.semibold, dp(kTextXS), ImVec2(wp.x + dp(20), wp.y + y + dp(4)), Col(P.textMute, la), label);
        y += dp(24);
    };
    section("MAIN");
    for (int i = 0; i < Page_COUNT; i++)
    {
        if (i == Page_Settings)
        {
            y += dp(6);
            section("SYSTEM");
        }
        ys[i] = y;
        y += item_h + dp(2);
    }

    float sel_y = Anim(ImGui::GetID("##navsel"), ys[S.page], 18.0f);
    ImVec2 sa(wp.x + pad, wp.y + sel_y), sb(wp.x + width - pad, wp.y + sel_y + item_h);
    dl->AddRectFilled(sa, sb, Col(P.accent, 0.14f), dp(7));
    GradientRect(dl, ImVec2(sa.x, sa.y + dp(8)), ImVec2(sa.x + dp(2), sb.y - dp(8)), P.accent, P.accent2, dp(1), 0, true);
    if (glow)
        RadialGlow(dl, ImVec2(sa.x + dp(1), (sa.y + sb.y) * 0.5f), dp(28), P.accent, 0.35f);

    for (int i = 0; i < Page_COUNT; i++)
    {
        ImVec2 a(wp.x + pad, wp.y + ys[i]);
        ImGui::SetCursorScreenPos(a);
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("##nav", ImVec2(width - pad * 2, item_h), ImGuiButtonFlags_EnableNav))
            SetPage(i);
        bool hovered = ImGui::IsItemHovered();
        if (ct > 0.5f)
            ItemTooltip(kNav[i].label);
        float hv = Anim(ImGui::GetItemID(), hovered ? 1.0f : 0.0f, 18.0f);
        ImGui::PopID();
        bool sel = S.page == i;
        if (!sel && hv > 0.01f)
            dl->AddRectFilled(a, a + ImVec2(width - pad * 2, item_h), Col(P.hover, 0.7f * hv), dp(7));
        float ix = ImLerp(a.x + dp(18), wp.x + width * 0.5f, ct);
        DrawIcon(dl, kNav[i].icon, ImVec2(ix, a.y + item_h * 0.5f), dp(14), Col(sel ? P.accent : Mix(P.textMute, P.textDim, hv)));
        if (la > 0.01f)
        {
            ImFont* f = sel ? F.semibold : F.regular;
            TextFit(dl, f, dp(kTextM), ImVec2(a.x + dp(38), a.y + (item_h - LineH(f, dp(kTextM))) * 0.5f), Col(sel ? P.text : Mix(P.textDim, P.text, hv), la),
                    kNav[i].label, width - pad * 2 - dp(40));
        }
    }

    // Account: hairline divider, avatar, name, "Till:" line and a settings shortcut. No card.
    const float row_h = dp(54), av = dp(16);
    const float top = wp.y + ws.y - row_h;
    if (la > 0.01f)
        dl->AddLine(ImVec2(wp.x + pad, top), ImVec2(wp.x + width - pad, top), Col(P.border, la));
    ImVec2 ac(ImLerp(wp.x + pad + dp(2) + av, wp.x + width * 0.5f, ct), top + row_h * 0.5f);
    Avatar(dl, ac, av);
    if (la > 0.01f)
    {
        float tx = ac.x + av + dp(9), gear = dp(24);
        float max_w = wp.x + width - pad - gear - dp(6) - tx;
        TextFit(dl, F.semibold, dp(kTextM), ImVec2(tx, ac.y - dp(18)), Col(P.text, la), s_userName.c_str(), max_w);
        float tw = Measure(F.regular, dp(kTextXS), "Till:").x;
        TextAt(dl, F.regular, dp(kTextXS), ImVec2(tx, ac.y + dp(1)), Col(P.textMute, la), "Till:");
        TextFit(dl, F.regular, dp(kTextXS), ImVec2(tx + tw + dp(4), ac.y + dp(1)), Col(Mix(P.textDim, P.accent, 0.55f), la), "lifetime", max_w - tw - dp(4));
        ImGui::SetCursorScreenPos(ImVec2(wp.x + width - pad - gear, ac.y - gear * 0.5f));
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, la);
        if (IconButton("##account", icon::Gear, "Settings", gear))
            SetPage(Page_Settings, 0);
        ImGui::PopStyleVar();
    }
    ImGui::EndChild();
}

//-----------------------------------------------------------------------------
// Header: title, sub-tabs, search palette, window buttons
//-----------------------------------------------------------------------------

static char s_query[64] = "";
static bool s_openSearch = false;

static void SearchPopup(ImVec2 anchor)
{
    bool just_opened = false;
    if (s_openSearch || (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_K) && !ImGui::IsPopupOpen("##search")))
    {
        ImGui::OpenPopup("##search");
        s_query[0] = 0;
        s_openSearch = false;
        just_opened = true;
    }
    ImGui::SetNextWindowPos(anchor, ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(dp(330), 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(dp(6), dp(6)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, dp(2)));
    bool open = ImGui::BeginPopup("##search");
    ImGui::PopStyleVar(2);
    if (!open)
        return;
    app::AddNoDragRect(ImGui::GetWindowPos(), ImGui::GetWindowPos() + ImGui::GetWindowSize());
    ImDrawList* dl = ImGui::GetWindowDrawList();

    if (just_opened || ImGui::IsWindowAppearing())
        ImGui::SetKeyboardFocusHere();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(dp(28), dp(6)));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, P.field);
    ImGui::SetNextItemWidth(-FLT_MIN);
    bool enter = ImGui::InputTextWithHint("##q", "Search settings...", s_query, sizeof(s_query), ImGuiInputTextFlags_EnterReturnsTrue);
    ImVec2 qa = ImGui::GetItemRectMin(), qb = ImGui::GetItemRectMax();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    DrawIcon(dl, icon::Search, ImVec2(qa.x + dp(14), (qa.y + qb.y) * 0.5f), dp(12), Col(P.textMute));
    ImGui::Dummy(ImVec2(0, dp(2)));

    // Prefix matches first, then substring matches; empty query lists the pages.
    std::vector<int> hits;
    size_t qlen = strlen(s_query);
    for (int pass = 0; pass < 2 && hits.size() < 7; pass++)
        for (int i = 0; i < (int)s_index.size() && hits.size() < 7; i++)
        {
            const char* l = s_index[i].label.c_str();
            bool match = qlen == 0 ? (pass == 0 && s_index[i].label == kNav[s_index[i].page].label)
                       : pass == 0 ? ImStrnicmp(l, s_query, qlen) == 0
                                   : (ImStrnicmp(l, s_query, qlen) != 0 && ImStristr(l, nullptr, s_query, nullptr));
            if (match)
                hits.push_back(i);
        }

    for (int n = 0; n < (int)hits.size(); n++)
    {
        const SearchEntry& e = s_index[hits[n]];
        ImGui::PushID(hits[n]);
        bool pick = ImGui::Selectable("##r", n == 0 && qlen > 0, 0, ImVec2(0, dp(32))) || (enter && n == 0);
        ImVec2 ra = ImGui::GetItemRectMin(), rb = ImGui::GetItemRectMax();
        float cy = (ra.y + rb.y) * 0.5f;
        DrawIcon(dl, kNav[e.page].icon, ImVec2(ra.x + dp(14), cy), dp(12), Col(P.accent));
        char where[48];
        if (kNav[e.page].subCount)
            snprintf(where, sizeof(where), "%s / %s", kNav[e.page].label, kNav[e.page].subs[e.sub]);
        else
            snprintf(where, sizeof(where), "%s", kNav[e.page].label);
        ImVec2 ws = Measure(F.regular, dp(kTextXS), where);
        TextAt(dl, F.regular, dp(kTextXS), ImVec2(rb.x - ws.x - dp(8), cy - ws.y * 0.5f), Col(P.textMute), where);
        TextFit(dl, F.regular, dp(kTextM), ImVec2(ra.x + dp(30), cy - LineH(F.regular, dp(kTextM)) * 0.5f), Col(P.text), e.label.c_str(), rb.x - ws.x - dp(16) - (ra.x + dp(30)));
        if (pick)
        {
            SetPage(e.page, e.sub);
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopID();
    }
    if (hits.empty())
    {
        ImGui::Dummy(ImVec2(0, dp(2)));
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + dp(8));
        ImGui::TextColored(P.textMute, "No matches");
    }
    ImGui::EndPopup();
}

static void Header(float h)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x;
    float cy = p.y + h * 0.5f;
    app::SetCaptionHeight(h);

    const NavItem& nav = kNav[S.page];
    float fs = dp(kTextXL);
    TextAt(dl, F.semibold, fs, ImVec2(p.x + dp(18), cy - LineH(F.semibold, fs) * 0.5f), Col(P.text), nav.label);
    if (nav.subCount > 1)
    {
        float tx = p.x + dp(18) + Measure(F.semibold, fs, nav.label).x + dp(14);
        ImGui::SetCursorScreenPos(ImVec2(tx, cy - dp(16)));
        ImGui::PushID(S.page);
        if (Pills("##sub", &S.sub[S.page], nav.subs, nav.subCount, -1.0f))
        {
            S.changedAt = ImGui::GetTime();
            S.resetScroll = true;
        }
        ImGui::PopID();
    }

    float bs = dp(30), x = p.x + w - dp(8) - bs;
    ImGui::SetCursorScreenPos(ImVec2(x, cy - bs * 0.5f));
    if (IconButton("##close", icon::Close, "Close", bs, true))
        app::Close();
    x -= bs + dp(2);
    ImGui::SetCursorScreenPos(ImVec2(x, cy - bs * 0.5f));
    if (IconButton("##min", icon::Minimize, "Minimize", bs))
        app::Minimize();
    x -= dp(9);
    dl->AddLine(ImVec2(x, cy - dp(8)), ImVec2(x, cy + dp(8)), Col(P.borderStrong));
    x -= dp(8) + bs;
    ImGui::SetCursorScreenPos(ImVec2(x, cy - bs * 0.5f));
    if (IconButton("##search", icon::Search, "Search  (Ctrl+K)", bs))
        s_openSearch = true;
    SearchPopup(ImVec2(x + bs, p.y + h - dp(4)));

    dl->AddLine(ImVec2(p.x, p.y + h - 1), ImVec2(p.x + w, p.y + h - 1), Col(P.border));
    ImGui::SetCursorScreenPos(p);
    ImGui::Dummy(ImVec2(w, h));
}

//-----------------------------------------------------------------------------
// Root
//-----------------------------------------------------------------------------

void Init()
{
    stats::Init();
    double now = ImGui::GetTime();
    S.configs = {
        { "Competitive",   "Max frame rate, low latency, minimal post-processing.", now - 86400 * 12, now },
        { "Cinematic",     "Ultra preset with HDR, bloom and film grain for screenshots.", now - 86400 * 30, now - 3600 * 5 },
        { "Streaming",     "Balanced quality that leaves headroom for the encoder.", now - 86400 * 3, now - 3600 * 26 },
        { "Battery saver", "Capped at 60 fps with a reduced render scale.", now - 86400 * 60, now - 86400 * 4 },
    };
    Log(icon::Flag, "Session started");

    // Display name: NEBULA_USER if set, otherwise the Windows account name.
    wchar_t wname[256];
    DWORD wlen = IM_ARRAYSIZE(wname);
    char name[512];
    bool have = GetEnvironmentVariableW(L"NEBULA_USER", wname, wlen) > 0 || GetUserNameW(wname, &wlen);
    if (have && WideCharToMultiByte(CP_UTF8, 0, wname, -1, name, sizeof(name), nullptr, nullptr) > 1)
        s_userName = name;
    s_avatar = app::LoadAvatar((int)roundf(dp(28)));
}

void OnDpiChanged()
{
    s_avatar = app::LoadAvatar((int)roundf(dp(28)));
}

static void Hotkeys()
{
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput || IsCapturingKey())
        return;
    for (int i = 0; i < Page_COUNT; i++)
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | (ImGuiKey)(ImGuiKey_1 + i)))
            SetPage(i);
    struct HK { ImGuiKey key; bool* v; const char* name; };
    HK hks[] = { { S.perfKey, &S.perfMode, "Performance mode" }, { S.overlayKey, &S.overlay, "Overlay" }, { S.photoKey, &S.photoMode, "Photo mode" } };
    for (HK& h : hks)
        if (h.key != ImGuiKey_None && ImGui::IsKeyPressed(h.key, false))
        {
            *h.v = !*h.v;
            Notice(Toast_Info, icon::Keyboard, h.name, *h.v ? "enabled" : "disabled");
        }
}

// Seconds until the next frame is needed when nothing happens (input always wakes the loop).
float IdleDelay()
{
    ImGuiIO& io = ImGui::GetIO();
    if (ui::IsAnimating() || theme::IsAnimating() || ToastsActive() || s_transitioning || ImGui::IsAnyItemActive() ||
        IsCapturingKey() || S.diag >= 0.0f || S.showDemo)
        return 0.0f;
    const ImGuiContext& g = *ImGui::GetCurrentContext();
    const bool nav_focus = g.NavCursorVisible && g.NavId != 0;        // keyboard focus shows tooltips too
    float d = 1.0f;                                                   // clocks and "x min ago" labels
    if (io.WantTextInput || ImGui::IsAnyItemHovered() || nav_focus) d = 0.1f; // caret blink, tooltip delays
    if (S.page == Page_Home && S.sub[Page_Home] == 0)    d = ImMin(d, 0.1f);        // live stats
    if (S.page == Page_Audio && S.sub[Page_Audio] == 0)  d = ImMin(d, 1.0f / 30.0f); // level meter
    return d;
}

void Render()
{
    ImGuiIO& io = ImGui::GetIO();
    ui::NewFrame();
    Tick(io.DeltaTime);
    stats::Update(app::FrameWorkMs());
    if (!s_indexed)
        BuildSearchIndex();
    Hotkeys();

    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("##root", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
                 ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar(3);

    if (glow)
        RadialGlow(ImGui::GetWindowDrawList(), vp->Pos + ImVec2(vp->Size.x * 0.92f, -dp(30)), dp(320), P.accent, 0.10f);

    float ct = Anim(ImGui::GetID("##compact"), S.compact ? 1.0f : 0.0f, 12.0f);
    Sidebar(ImLerp(dp(176), dp(60), ct), ct);
    ImGui::SameLine(0, 0);

    ImGui::BeginChild("##content", ImVec2(0, 0), ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    Header(dp(52));

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(dp(14), dp(14)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(dp(12), dp(12)));
    ImGui::BeginChild("##body", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoScrollbar);
    if (S.resetScroll)
    {
        ImGui::SetScrollY(0.0f);
        S.resetScroll = false;
    }
    float pt = animations ? Ease((float)(ImGui::GetTime() - S.changedAt) / 0.30f) : 1.0f;
    s_transitioning = pt < 1.0f;
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, pt);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (1.0f - pt) * dp(10));
    DrawView(S.page, S.sub[S.page]);
    ImGui::PopStyleVar();
    OverlayScrollbar("##vscroll");
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::EndChild();
    ImGui::End();

    if (S.showDemo)
        ImGui::ShowDemoWindow(&S.showDemo);
    RenderToasts();
}
}
