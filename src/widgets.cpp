#include "widgets.h"
#include "theme.h"
#include "imgui_internal.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

using namespace theme;

namespace ui
{
//-----------------------------------------------------------------------------
// Frame bookkeeping and animation
//-----------------------------------------------------------------------------

Ico::Ico(unsigned cp)
{
    if (cp < 0x80) { s[0] = (char)cp; s[1] = 0; }
    else if (cp < 0x800) { s[0] = (char)(0xC0 | (cp >> 6)); s[1] = (char)(0x80 | (cp & 0x3F)); s[2] = 0; }
    else { s[0] = (char)(0xE0 | (cp >> 12)); s[1] = (char)(0x80 | ((cp >> 6) & 0x3F)); s[2] = (char)(0x80 | (cp & 0x3F)); s[3] = 0; }
}

struct AnimSlot { float value; int frame; };
static std::unordered_map<ImGuiID, AnimSlot> s_anim;
static bool s_animating = false;
static LabelSink s_labelSink = nullptr;

void NewFrame()
{
    s_animating = false;
    // Drop values for widgets that haven't been drawn for a while (other pages, closed popups).
    const int frame = ImGui::GetFrameCount();
    if (frame % 600 == 0)
        for (auto it = s_anim.begin(); it != s_anim.end();)
            it = (frame - it->second.frame > 300) ? s_anim.erase(it) : std::next(it);
}

bool IsAnimating() { return s_animating; }

float Anim(ImGuiID id, float target, float speed, bool keep_awake)
{
    const int frame = ImGui::GetFrameCount();
    auto it = s_anim.find(id);
    if (it == s_anim.end())
    {
        s_anim.emplace(id, AnimSlot{ target, frame });
        return target;
    }
    AnimSlot& a = it->second;
    a.frame = frame;
    if (!animations)
        return a.value = target;
    // Clamp dt: after an idle period the first frame would otherwise jump straight to the target.
    float dt = ImMin(ImGui::GetIO().DeltaTime, 1.0f / 30.0f);
    a.value += (target - a.value) * (1.0f - expf(-speed * dt));
    if (fabsf(a.value - target) < 0.0005f * ImMax(1.0f, fabsf(target)))
        a.value = target;
    else if (keep_awake)
        s_animating = true;
    return a.value;
}

float Ease(float t)
{
    t = ImSaturate(t);
    float u = 1.0f - t;
    return 1.0f - u * u * u;
}

ImGuiID SubId(ImGuiID id, int k) { return ImHashData(&k, sizeof(k), id); }

void SetLabelSink(LabelSink sink) { s_labelSink = sink; }

static void ReportLabel(const char* label)
{
    if (s_labelSink)
        s_labelSink(label, ImGui::FindRenderedTextEnd(label));
}

//-----------------------------------------------------------------------------
// Drawing helpers
//-----------------------------------------------------------------------------

float LineH(ImFont* f, float size) { return f->CalcTextSizeA(size, FLT_MAX, 0.0f, "Ag").y; }

ImVec2 Measure(ImFont* f, float size, const char* text, const char* end)
{
    return f->CalcTextSizeA(size, FLT_MAX, 0.0f, text, end);
}

void TextAt(ImDrawList* dl, ImFont* f, float size, ImVec2 pos, ImU32 col, const char* text, const char* end)
{
    dl->AddText(f, size, ImVec2(IM_ROUND(pos.x), IM_ROUND(pos.y)), col, text, end);
}

float TextFit(ImDrawList* dl, ImFont* f, float size, ImVec2 pos, ImU32 col, const char* text, float max_w, const char* end)
{
    static const char* kEllipsis = "\xE2\x80\xA6";
    if (!end)
        end = text + strlen(text);
    float full = f->CalcTextSizeA(size, FLT_MAX, 0.0f, text, end).x;
    if (full <= max_w)
    {
        TextAt(dl, f, size, pos, col, text, end);
        return full;
    }
    float ew = f->CalcTextSizeA(size, FLT_MAX, 0.0f, kEllipsis).x;
    if (max_w <= ew)
        return 0.0f;
    const char* cut = text;
    for (const char* s = text; s < end;)
    {
        unsigned int c;
        int len = ImTextCharFromUtf8(&c, s, end);
        if (len <= 0 || f->CalcTextSizeA(size, FLT_MAX, 0.0f, text, s + len).x + ew > max_w)
            break;
        s += len;
        cut = s;
    }
    while (cut > text && cut[-1] == ' ')
        cut--;
    float w = f->CalcTextSizeA(size, FLT_MAX, 0.0f, text, cut).x;
    TextAt(dl, f, size, pos, col, text, cut);
    TextAt(dl, f, size, ImVec2(pos.x + w, pos.y), col, kEllipsis);
    return w + ew;
}

void DrawIcon(ImDrawList* dl, unsigned cp, ImVec2 c, float size, ImU32 col)
{
    ImFont* f = F.icons ? F.icons : F.regular;
    Ico s(cp);
    ImFontBaked* baked = f->GetFontBaked(size);
    const ImFontGlyph* g = baked ? baked->FindGlyphNoFallback((ImWchar)cp) : nullptr;
    ImVec2 pos;
    if (g)
    {
        float k = size / baked->Size;
        pos = ImVec2(c.x - (g->X0 + g->X1) * 0.5f * k, c.y - (g->Y0 + g->Y1) * 0.5f * k);
    }
    else
    {
        ImVec2 ts = f->CalcTextSizeA(size, FLT_MAX, 0.0f, s);
        pos = ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f);
    }
    dl->AddText(f, size, ImVec2(IM_ROUND(pos.x), IM_ROUND(pos.y)), col, s);
}

// Recolors vertices [v0, v1) with a linear gradient, multiplying by each vertex's existing alpha
// (so anti-aliased fringes stay soft and alpha gradients work).
static void ShadeLinear(ImDrawList* dl, int v0, int v1, ImVec2 p0, ImVec2 p1, ImVec4 c0, ImVec4 c1)
{
    ImVec2 d = p1 - p0;
    float len2 = ImLengthSqr(d);
    for (ImDrawVert* v = dl->VtxBuffer.Data + v0; v < dl->VtxBuffer.Data + v1; v++)
    {
        float t = len2 > 0.0f ? ImSaturate(ImDot(v->pos - p0, d) / len2) : 0.0f;
        ImVec4 c = ImLerp(c0, c1, t);
        c.w *= (float)((v->col >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f;
        v->col = ImGui::ColorConvertFloat4ToU32(c);
    }
}

void GradientRect(ImDrawList* dl, ImVec2 a, ImVec2 b, ImVec4 c0, ImVec4 c1, float r, ImDrawFlags flags, bool vertical, float alpha)
{
    int v0 = dl->VtxBuffer.Size;
    dl->AddRectFilled(a, b, Col(ImVec4(1, 1, 1, 1), alpha), r, flags);
    ShadeLinear(dl, v0, dl->VtxBuffer.Size, a, vertical ? ImVec2(a.x, b.y) : ImVec2(b.x, a.y), c0, c1);
}

void RadialGlow(ImDrawList* dl, ImVec2 c, float r, ImVec4 col, float alpha)
{
    if (alpha <= 0.002f)
        return;
    alpha *= ImGui::GetStyle().Alpha;
    static const float rf[] = { 0.22f, 0.48f, 0.74f, 1.0f };
    static const float af[] = { 0.78f, 0.42f, 0.14f, 0.0f };
    const int seg = 48, rings = 4;
    const ImVec2 uv = dl->_Data->TexUvWhitePixel;
    dl->PrimReserve(seg * 3 + (rings - 1) * seg * 6, 1 + rings * seg);
    const unsigned base = dl->_VtxCurrentIdx;
    dl->PrimWriteVtx(c, uv, ImGui::ColorConvertFloat4ToU32(WithAlpha(col, alpha)));
    for (int ri = 0; ri < rings; ri++)
        for (int i = 0; i < seg; i++)
        {
            float ang = (float)i / seg * IM_PI * 2.0f;
            ImVec2 p(c.x + cosf(ang) * r * rf[ri], c.y + sinf(ang) * r * rf[ri]);
            dl->PrimWriteVtx(p, uv, ImGui::ColorConvertFloat4ToU32(WithAlpha(col, alpha * af[ri])));
        }
    auto V = [&](int ri, int i) { return (ImDrawIdx)(base + 1 + ri * seg + (i % seg)); };
    for (int i = 0; i < seg; i++)
    {
        dl->PrimWriteIdx((ImDrawIdx)base); dl->PrimWriteIdx(V(0, i)); dl->PrimWriteIdx(V(0, i + 1));
    }
    for (int ri = 0; ri < rings - 1; ri++)
        for (int i = 0; i < seg; i++)
        {
            dl->PrimWriteIdx(V(ri, i)); dl->PrimWriteIdx(V(ri + 1, i)); dl->PrimWriteIdx(V(ri + 1, i + 1));
            dl->PrimWriteIdx(V(ri, i)); dl->PrimWriteIdx(V(ri + 1, i + 1)); dl->PrimWriteIdx(V(ri, i + 1));
        }
}

void SoftShadow(ImDrawList* dl, ImVec2 a, ImVec2 b, float r, ImVec4 col, float alpha, float spread)
{
    const int steps = 8;
    for (int i = 1; i <= steps; i++)
    {
        float t = (float)i / steps;
        float e = spread * t;
        dl->AddRect(a - ImVec2(e, e), b + ImVec2(e, e), Col(col, alpha * (1.0f - t) * (1.0f - t) * 0.5f), r + e, 0, spread / steps + 1.0f);
    }
}

void BrandMark(ImDrawList* dl, ImVec2 p, float s, ImVec4 c0, ImVec4 c1, float alpha)
{
    ImVec2 a = p, b = p + ImVec2(s, s), c = (a + b) * 0.5f;
    ImVec4 white(1, 1, 1, 1);
    GradientRect(dl, a, b, c0, c1, s * 0.3f, 0, true, alpha);
    dl->AddCircleFilled(c, s * 0.16f, Col(white, alpha), 24);
    dl->AddEllipse(c, ImVec2(s * 0.35f, s * 0.125f), Col(white, 0.9f * alpha), -0.44f, 32, ImMax(1.0f, s * 0.055f));
}

static void RowLabel(ImDrawList* dl, ImVec2 p, float row_h, const char* label, ImVec4 col, float max_w)
{
    float fs = dp(kTextM);
    TextFit(dl, F.regular, fs, ImVec2(p.x, p.y + (row_h - LineH(F.regular, fs)) * 0.5f), Col(col), label, max_w, ImGui::FindRenderedTextEnd(label));
}

static void NavRing(const ImRect& bb, ImGuiID id, float rounding)
{
    ImGui::RenderNavCursor(bb, id, ImGuiNavRenderCursorFlags_None, rounding);
}

//-----------------------------------------------------------------------------
// Tooltips and scrollbar
//-----------------------------------------------------------------------------

void Tooltip(const char* text)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(dp(8), dp(4)));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, dp(5));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, Hex(0x0D0E13));
    ImGui::PushStyleColor(ImGuiCol_Border, P.borderStrong);
    bool open = ImGui::BeginTooltip();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
    if (!open)
        return;
    ImGui::PushFont(F.regular, kTextS);
    ImGui::PushStyleColor(ImGuiCol_Text, P.textDim);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::EndTooltip();
}

void ItemTooltip(const char* text)
{
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        Tooltip(text);
}

void OverlayScrollbar(const char* str_id)
{
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    const float max = win->ScrollMax.y;
    if (max <= 0.0f)
        return;
    ImGuiID id = win->GetID(str_id);
    ImRect wr = win->Rect();
    const float top = wr.Min.y + dp(4), bottom = wr.Max.y - dp(4), track = bottom - top;
    const float thumb = ImMax(dp(28), track * wr.GetHeight() / (wr.GetHeight() + max));
    ImRect hit(ImVec2(wr.Max.x - dp(12), top), ImVec2(wr.Max.x, bottom));
    if (!ImGui::ItemAdd(hit, id, nullptr, ImGuiItemFlags_NoNav))
        return;
    bool hov, held;
    bool pressed = ImGui::ButtonBehavior(hit, id, &hov, &held, ImGuiButtonFlags_PressedOnClick);

    ImGuiStorage* st = ImGui::GetStateStorage();
    float y = top + (track - thumb) * (win->Scroll.y / max);
    float my = ImGui::GetIO().MousePos.y;
    if (pressed) // grab where clicked on the thumb, or center the thumb under the cursor
        st->SetFloat(SubId(id, 1), (my >= y && my <= y + thumb) ? my - y : thumb * 0.5f);
    if (held)
    {
        float t = ImSaturate((my - st->GetFloat(SubId(id, 1)) - top) / ImMax(1.0f, track - thumb));
        ImGui::SetScrollY(t * max);
        y = top + (track - thumb) * t;
    }

    // Visible while hovering the window, dragging, or for a moment after scrolling.
    double now = ImGui::GetTime();
    float* last = st->GetFloatRef(SubId(id, 2), win->Scroll.y);
    float* last_t = st->GetFloatRef(SubId(id, 3), -10.0f);
    if (*last != win->Scroll.y) { *last = win->Scroll.y; *last_t = (float)now; }
    bool recent = now - *last_t < 0.8;
    bool window_hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    float vis = Anim(SubId(id, 4), (held || hov || recent) ? 1.0f : window_hovered ? 0.55f : 0.0f, 10.0f);
    float wide = Anim(SubId(id, 5), (held || hov) ? 1.0f : 0.0f, 16.0f);
    if (vis <= 0.01f)
        return;
    float w = dp(3) + dp(3) * wide;
    ImRect clip = wr;
    if (win->ParentWindow)
        clip.ClipWith(win->ParentWindow->ClipRect);
    ImDrawList* dl = win->DrawList;
    dl->PushClipRect(clip.Min, clip.Max, false);
    dl->AddRectFilled(ImVec2(wr.Max.x - dp(4) - w, y), ImVec2(wr.Max.x - dp(4), y + thumb), Col(held ? P.accent : P.text, (0.16f + 0.18f * wide) * vis), w * 0.5f);
    dl->PopClipRect();
}

//-----------------------------------------------------------------------------
// Groups
//-----------------------------------------------------------------------------

struct GroupCtx { bool disabled; };
static ImVector<GroupCtx> s_groups;

static bool MiniToggleAt(ImGuiID id, ImVec2 pos, bool* v)
{
    ImVec2 sz(dp(28), dp(16));
    ImRect r(pos, pos + sz);
    if (!ImGui::ItemAdd(r, id))
        return false;
    bool hov, held;
    bool pressed = ImGui::ButtonBehavior(r, id, &hov, &held);
    if (pressed)
    {
        *v = !*v;
        ImGui::MarkItemEdited(id);
    }
    float t = Anim(id, *v ? 1.0f : 0.0f, 16.0f);
    float hv = Anim(SubId(id, 1), hov ? 1.0f : 0.0f, 18.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float rr = sz.y * 0.5f;
    dl->AddRectFilled(r.Min, r.Max, Col(Mix(P.hover, P.borderStrong, hv * 0.6f)), rr);
    dl->AddRect(r.Min, r.Max, Col(P.borderStrong, 1.0f - t), rr);
    if (t > 0.01f)
        GradientRect(dl, r.Min, r.Max, P.accent, P.accent2, rr, 0, false, t);
    ImVec2 kc(ImLerp(r.Min.x + rr, r.Max.x - rr, t), r.Min.y + rr);
    if (glow)
        RadialGlow(dl, kc, dp(16), P.accent, 0.30f * t);
    dl->AddCircleFilled(kc, rr - dp(2), Col(Mix(P.textDim, ImVec4(1, 1, 1, 1), t)));
    NavRing(r, id, rr);
    return pressed;
}

bool ToggleMini(const char* str_id, bool* v)
{
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    if (win->SkipItems)
        return false;
    ImVec2 p = win->DC.CursorPos;
    ImGui::ItemSize(ImVec2(dp(28), dp(16)));
    return MiniToggleAt(win->GetID(str_id), p, v);
}

bool BeginGroup(const char* str_id, unsigned ic, const char* title, bool* enabled)
{
    ReportLabel(title);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(dp(12), 0));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, WithAlpha(P.group, 0.92f));
    ImGui::PushStyleColor(ImGuiCol_Border, P.border);
    bool visible = ImGui::BeginChild(str_id, ImVec2(0, 0),
                                     ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_NavFlattened,
                                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();

    ImGuiWindow* win = ImGui::GetCurrentWindow();
    ImDrawList* dl = win->DrawList;
    ImRect wr = win->Rect();
    const float hh = dp(38);
    const float cy = wr.Min.y + hh * 0.5f;

    // Header spans the full card width, so draw it outside the padded clip rect.
    ImRect clip = wr;
    if (win->ParentWindow)
        clip.ClipWith(win->ParentWindow->ClipRect);
    dl->PushClipRect(clip.Min, clip.Max, false);
    DrawIcon(dl, ic, ImVec2(wr.Min.x + dp(20), cy), dp(14), Col(P.accent));
    float fs = dp(kTextL), tx = wr.Min.x + dp(36);
    float title_max = wr.Max.x - dp(12) - (enabled ? dp(28) + dp(8) : 0.0f) - tx;
    TextFit(dl, F.semibold, fs, ImVec2(tx, cy - LineH(F.semibold, fs) * 0.5f), Col(P.text), title, title_max);
    dl->AddLine(ImVec2(wr.Min.x + 1, wr.Min.y + hh), ImVec2(wr.Max.x - 1, wr.Min.y + hh), Col(P.border));
    dl->PopClipRect();

    if (enabled)
        MiniToggleAt(win->GetID("##enabled"), ImVec2(wr.Max.x - dp(12) - dp(28), cy - dp(8)), enabled);

    ImGui::SetCursorScreenPos(ImVec2(wr.Min.x + dp(12), wr.Min.y + hh + 1.0f + dp(8)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(dp(8), dp(4)));
    bool disabled = enabled && !*enabled;
    if (disabled)
        ImGui::BeginDisabled();
    s_groups.push_back({ disabled });
    return visible;
}

void EndGroup()
{
    GroupCtx ctx = s_groups.back();
    s_groups.pop_back();
    if (ctx.disabled)
        ImGui::EndDisabled();
    ImGui::Dummy(ImVec2(0, dp(8)));
    ImGui::PopStyleVar();
    ImGui::EndChild();

    ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    bool hov = ImGui::IsMouseHoveringRect(a, b) && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    float h = Anim(ImGui::GetItemID() ^ 0x5EED, hov ? 1.0f : 0.0f, 10.0f);
    if (h > 0.01f)
        ImGui::GetWindowDrawList()->AddRect(a, b, Col(P.borderStrong, h), RGroup());
}

void Caption(const char* text)
{
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    if (win->SkipItems)
        return;
    ImVec2 p = win->DC.CursorPos;
    float h = dp(22), w = ImGui::GetContentRegionAvail().x;
    TextFit(win->DrawList, F.semibold, dp(kTextXS), ImVec2(p.x, p.y + (h - LineH(F.semibold, dp(kTextXS))) * 0.5f), Col(P.textMute), text, w);
    ImGui::ItemSize(ImVec2(w, h));
}

void Divider()
{
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    ImVec2 p = win->DC.CursorPos;
    float w = ImGui::GetContentRegionAvail().x;
    ImGui::ItemSize(ImVec2(w, dp(7)));
    win->DrawList->AddLine(ImVec2(p.x, p.y + dp(3)), ImVec2(p.x + w, p.y + dp(3)), Col(P.border));
}

//-----------------------------------------------------------------------------
// Color picker (rounded SV square, hue and alpha bars, hex field)
//-----------------------------------------------------------------------------

static bool BarBehavior(ImGuiID id, const ImRect& r, float* t, bool vertical_inverted)
{
    ImGui::ItemAdd(r, id);
    bool hov, held;
    ImGui::ButtonBehavior(r, id, &hov, &held, ImGuiButtonFlags_PressedOnClick);
    if (!held)
        return false;
    float v = ImSaturate((ImGui::GetIO().MousePos.y - r.Min.y) / r.GetHeight());
    *t = vertical_inverted ? 1.0f - v : ImMin(v, 0.9999f);
    return true;
}

static void BarHandle(ImDrawList* dl, const ImRect& r, float y)
{
    ImVec2 a(r.Min.x - dp(2), y - dp(3)), b(r.Max.x + dp(2), y + dp(3));
    dl->AddRect(a - ImVec2(1, 1), b + ImVec2(1, 1), Col(ImVec4(0, 0, 0, 0.45f)), dp(3));
    dl->AddRectFilled(a, b, Col(ImVec4(1, 1, 1, 1)), dp(3));
}

static bool PickerBody(ImGuiID id, float col[4])
{
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    ImDrawList* dl = win->DrawList;
    ImGuiStorage* st = ImGui::GetStateStorage();
    float h, s, v;
    ImGui::ColorConvertRGBtoHSV(col[0], col[1], col[2], h, s, v);
    float* keep_h = st->GetFloatRef(SubId(id, 11), h);
    float* keep_s = st->GetFloatRef(SubId(id, 12), s);
    if (s <= 0.0001f || v <= 0.0001f) h = *keep_h; // hue is undefined for greys / black
    if (v <= 0.0001f) s = *keep_s;
    float a = col[3];

    const float sv = dp(156), bw = dp(10), gap = dp(10), r = dp(6);
    const ImVec4 white(1, 1, 1, 1), black(0, 0, 0, 1);
    ImVec2 p = win->DC.CursorPos;
    ImGui::ItemSize(ImVec2(sv + (gap + bw) * 2, sv));
    bool changed = false;

    // Saturation / value
    ImRect svr(p, p + ImVec2(sv, sv));
    ImGuiID sv_id = SubId(id, 20);
    ImGui::ItemAdd(svr, sv_id);
    bool hov, held;
    ImGui::ButtonBehavior(svr, sv_id, &hov, &held, ImGuiButtonFlags_PressedOnClick);
    if (held)
    {
        ImVec2 m = ImGui::GetIO().MousePos;
        s = ImSaturate((m.x - svr.Min.x) / sv);
        v = 1.0f - ImSaturate((m.y - svr.Min.y) / sv);
        changed = true;
    }
    ImVec4 hue(0, 0, 0, 1);
    ImGui::ColorConvertHSVtoRGB(h, 1.0f, 1.0f, hue.x, hue.y, hue.z);
    GradientRect(dl, svr.Min, svr.Max, white, hue, r);
    GradientRect(dl, svr.Min, svr.Max, ImVec4(0, 0, 0, 0), black, r, 0, true);
    dl->AddRect(svr.Min, svr.Max, Col(ImVec4(1, 1, 1, 0.08f)), r);
    ImVec2 mc(svr.Min.x + s * sv, svr.Min.y + (1.0f - v) * sv);
    dl->AddCircle(mc, dp(7), Col(ImVec4(0, 0, 0, 0.45f)), 24, dp(1));
    dl->AddCircle(mc, dp(5.5f), Col(white), 24, dp(2));

    // Hue
    ImRect hr(ImVec2(svr.Max.x + gap, p.y), ImVec2(svr.Max.x + gap + bw, p.y + sv));
    changed |= BarBehavior(SubId(id, 21), hr, &h, false);
    static const ImVec4 stops[7] = { { 1, 0, 0, 1 }, { 1, 1, 0, 1 }, { 0, 1, 0, 1 }, { 0, 1, 1, 1 }, { 0, 0, 1, 1 }, { 1, 0, 1, 1 }, { 1, 0, 0, 1 } };
    float seg = sv / 6.0f;
    for (int i = 0; i < 6; i++)
    {
        bool cap = (i == 0 || i == 5);
        GradientRect(dl, ImVec2(hr.Min.x, hr.Min.y + seg * i), ImVec2(hr.Max.x, hr.Min.y + seg * (i + 1)), stops[i], stops[i + 1],
                     cap ? bw * 0.5f : 0.0f, i == 0 ? ImDrawFlags_RoundTop : i == 5 ? ImDrawFlags_RoundBottom : ImDrawFlags_RoundNone, true);
    }
    BarHandle(dl, hr, hr.Min.y + h * sv);

    // Alpha
    ImRect ar(ImVec2(hr.Max.x + gap, p.y), ImVec2(hr.Max.x + gap + bw, p.y + sv));
    changed |= BarBehavior(SubId(id, 22), ar, &a, true);
    ImVec4 rgb(0, 0, 0, 1);
    ImGui::ColorConvertHSVtoRGB(h, s, v, rgb.x, rgb.y, rgb.z);
    dl->AddRectFilled(ar.Min, ar.Max, Col(P.hover), bw * 0.5f);
    GradientRect(dl, ar.Min, ar.Max, rgb, WithAlpha(rgb, 0.0f), bw * 0.5f, 0, true);
    BarHandle(dl, ar, ar.Min.y + (1.0f - a) * sv);

    // Hex + alpha readout
    char hex[8];
    snprintf(hex, sizeof(hex), "%02X%02X%02X", (int)(rgb.x * 255 + 0.5f), (int)(rgb.y * 255 + 0.5f), (int)(rgb.z * 255 + 0.5f));
    ImGui::PushFont(F.mono, kTextS);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(dp(20), dp(5)));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, dp(5));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Border, P.borderStrong);
    ImGui::SetNextItemWidth(sv);
    bool hex_edit = ImGui::InputText("##hex", hex, sizeof(hex), ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CharsUppercase | ImGuiInputTextFlags_AutoSelectAll);
    ImVec2 ha = ImGui::GetItemRectMin(), hb = ImGui::GetItemRectMax();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
    ImGui::PopFont();
    TextAt(dl, F.mono, dp(kTextS), ImVec2(ha.x + dp(9), (ha.y + hb.y) * 0.5f - LineH(F.mono, dp(kTextS)) * 0.5f), Col(P.textMute), "#");
    char pct[8];
    snprintf(pct, sizeof(pct), "%d%%", (int)(a * 100.0f + 0.5f));
    ImVec2 ps = Measure(F.regular, dp(kTextXS), pct);
    TextAt(dl, F.regular, dp(kTextXS), ImVec2(ar.Max.x - ps.x, (ha.y + hb.y) * 0.5f - ps.y * 0.5f), Col(P.textDim), pct);

    unsigned int packed = 0;
    if (hex_edit && strlen(hex) == 6 && sscanf(hex, "%06X", &packed) == 1)
    {
        col[0] = ((packed >> 16) & 0xFF) / 255.0f;
        col[1] = ((packed >> 8) & 0xFF) / 255.0f;
        col[2] = (packed & 0xFF) / 255.0f;
        ImGui::ColorConvertRGBtoHSV(col[0], col[1], col[2], *keep_h, *keep_s, v);
        return true;
    }
    if (changed)
    {
        ImGui::ColorConvertHSVtoRGB(h, s, v, col[0], col[1], col[2]);
        col[3] = a;
        *keep_h = h;
        *keep_s = s;
    }
    return changed;
}

static bool SwatchAt(ImGuiID id, ImRect r, float col[4])
{
    if (!ImGui::ItemAdd(r, id))
        return false;
    bool hov, held;
    bool pressed = ImGui::ButtonBehavior(r, id, &hov, &held);
    float hv = Anim(id, hov ? 1.0f : 0.0f, 18.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec4 c(col[0], col[1], col[2], 1.0f);
    if (glow)
        RadialGlow(dl, r.GetCenter(), dp(16), c, 0.18f + 0.22f * hv);
    float g = dp(1) * hv;
    dl->AddRectFilled(r.Min - ImVec2(g, g), r.Max + ImVec2(g, g), Col(c), RCheck());
    dl->AddRect(r.Min - ImVec2(g, g), r.Max + ImVec2(g, g), Col(ImVec4(1, 1, 1, 0.14f + 0.2f * hv)), RCheck());
    NavRing(r, id, RCheck());
    if (pressed)
        ImGui::OpenPopup(id);
    bool changed = false;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(dp(10), dp(10)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(dp(8), dp(8)));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, dp(8));
    bool open = ImGui::BeginPopupEx(id, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleVar(3);
    if (open)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(dp(8), dp(8)));
        changed = PickerBody(id, col);
        ImGui::PopStyleVar();
        ImGui::EndPopup();
    }
    return changed;
}

//-----------------------------------------------------------------------------
// Checkbox + accessories (swatch, keybind chip)
//-----------------------------------------------------------------------------

static bool CheckboxImpl(const char* label, bool* v, float acc_w, ImRect* row_out, ImGuiID* id_out)
{
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    if (win->SkipItems)
        return false;
    ReportLabel(label);
    ImGuiID id = win->GetID(label);
    float w = ImGui::GetContentRegionAvail().x, h = dp(30);
    ImVec2 p = win->DC.CursorPos;
    ImRect row(p, p + ImVec2(w, h));
    ImRect hit(p, ImVec2(p.x + w - (acc_w > 0.0f ? acc_w + dp(8) : 0.0f), p.y + h));
    ImGui::ItemSize(row.GetSize());
    if (row_out) *row_out = row;
    if (id_out) *id_out = id;
    if (!ImGui::ItemAdd(hit, id))
        return false;

    bool hov, held;
    bool pressed = ImGui::ButtonBehavior(hit, id, &hov, &held);
    if (pressed)
    {
        *v = !*v;
        ImGui::MarkItemEdited(id);
    }
    float t = Anim(id, *v ? 1.0f : 0.0f, 18.0f);
    float hv = Anim(SubId(id, 1), hov ? 1.0f : 0.0f, 18.0f);
    ImDrawList* dl = win->DrawList;

    float s = dp(16), rr = RCheck();
    ImVec2 a(p.x, IM_ROUND(row.GetCenter().y - s * 0.5f)), b = a + ImVec2(s, s);
    dl->AddRectFilled(a, b, Col(Mix(P.field, P.hover, hv)), rr);
    dl->AddRect(a, b, Col(Mix(P.borderStrong, P.accent, hv * 0.6f), 1.0f - t), rr);
    if (t > 0.01f)
    {
        float k = 0.6f + 0.4f * t;
        ImVec2 c = (a + b) * 0.5f, hs = ImVec2(s, s) * 0.5f * k;
        if (glow)
            RadialGlow(dl, c, dp(16), P.accent, 0.28f * t);
        GradientRect(dl, c - hs, c + hs, P.accent, P.accent2, rr * k, 0, true, t);
        DrawIcon(dl, icon::Check, c, dp(10), Col(ImVec4(1, 1, 1, 1), t));
    }
    float fs = dp(kTextM), lx = b.x + dp(10);
    TextFit(dl, F.regular, fs, ImVec2(lx, row.GetCenter().y - LineH(F.regular, fs) * 0.5f),
            Col(Mix(P.textDim, P.text, ImMax(t, hv))), label, hit.Max.x - lx, ImGui::FindRenderedTextEnd(label));
    NavRing(hit, id, RCheck());
    return pressed;
}

bool Checkbox(const char* label, bool* v) { return CheckboxImpl(label, v, 0.0f, nullptr, nullptr); }

bool CheckboxColor(const char* label, bool* v, float col[4])
{
    ImRect row;
    ImGuiID id = 0;
    float sw = dp(14);
    bool pressed = CheckboxImpl(label, v, sw, &row, &id);
    if (id == 0)
        return pressed;
    float cy = IM_ROUND(row.GetCenter().y);
    bool changed = SwatchAt(SubId(id, 7), ImRect(ImVec2(row.Max.x - sw, cy - sw * 0.5f), ImVec2(row.Max.x, cy + sw * 0.5f)), col);
    return pressed || changed;
}

const char* KeyLabel(ImGuiKey k)
{
    switch (k)
    {
    case ImGuiKey_None:        return "-";
    case ImGuiKey_LeftCtrl:    return "LCtrl";
    case ImGuiKey_RightCtrl:   return "RCtrl";
    case ImGuiKey_LeftShift:   return "LShift";
    case ImGuiKey_RightShift:  return "RShift";
    case ImGuiKey_LeftAlt:     return "LAlt";
    case ImGuiKey_RightAlt:    return "RAlt";
    case ImGuiKey_MouseRight:  return "M2";
    case ImGuiKey_MouseMiddle: return "M3";
    case ImGuiKey_MouseX1:     return "M4";
    case ImGuiKey_MouseX2:     return "M5";
    case ImGuiKey_Escape:      return "Esc";
    case ImGuiKey_CapsLock:    return "Caps";
    case ImGuiKey_PageUp:      return "PgUp";
    case ImGuiKey_PageDown:    return "PgDn";
    default:                   return ImGui::GetKeyName(k);
    }
}

static ImGuiID s_listening = 0;
static int s_listenFrame = 0;

bool IsCapturingKey() { return s_listening != 0; }

static float KeyChipWidth(ImGuiID id, ImGuiKey key, float min_w)
{
    const char* txt = (s_listening == id) ? "..." : KeyLabel(key);
    return ImMax(min_w, Measure(F.semibold, dp(kTextS), txt).x + dp(16));
}

static bool KeyChipAt(ImGuiID id, ImRect r, ImGuiKey* key)
{
    if (!ImGui::ItemAdd(r, id))
        return false;
    bool listening = (s_listening == id);
    bool hov, held;
    if (ImGui::ButtonBehavior(r, id, &hov, &held, ImGuiButtonFlags_PressedOnClick | ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight) && !listening)
    {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        {
            *key = ImGuiKey_None; // right click clears
            ImGui::MarkItemEdited(id);
            return true;
        }
        s_listening = id;
        s_listenFrame = ImGui::GetFrameCount();
        listening = true;
    }

    bool changed = false;
    if (listening && ImGui::GetFrameCount() > s_listenFrame)
    {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            s_listening = 0;
        else if (ImGui::IsKeyPressed(ImGuiKey_Backspace, false) || ImGui::IsKeyPressed(ImGuiKey_Delete, false))
        {
            *key = ImGuiKey_None;
            s_listening = 0;
            changed = true;
        }
        else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !hov)
            s_listening = 0;
        else
            for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_ReservedForModCtrl; k++)
            {
                if (k == ImGuiKey_MouseLeft || k == ImGuiKey_MouseWheelX || k == ImGuiKey_MouseWheelY)
                    continue;
                if (ImGui::IsKeyPressed((ImGuiKey)k, false))
                {
                    *key = (ImGuiKey)k;
                    s_listening = 0;
                    changed = true;
                    break;
                }
            }
        if (changed)
            ImGui::MarkItemEdited(id);
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    float hv = Anim(SubId(id, 1), hov ? 1.0f : 0.0f, 18.0f);
    float ls = Anim(SubId(id, 2), listening ? 1.0f : 0.0f, 14.0f);
    float pulse = 0.5f + 0.5f * sinf((float)ImGui::GetTime() * 6.0f);
    float rr = dp(5);
    dl->AddRectFilled(r.Min, r.Max, Col(Mix(Mix(P.field, P.hover, hv), WithAlpha(P.accent, 0.16f), ls)), rr);
    dl->AddRect(r.Min, r.Max, Col(Mix(P.borderStrong, P.accent, ls * (0.5f + 0.5f * pulse))), rr);
    const char* txt = listening ? "..." : KeyLabel(*key);
    ImVec2 ts = Measure(F.semibold, dp(kTextS), txt);
    ImVec4 tc = listening ? P.accent : (*key == ImGuiKey_None ? P.textMute : Mix(P.textDim, P.text, hv));
    TextAt(dl, F.semibold, dp(kTextS), ImVec2(r.GetCenter().x - ts.x * 0.5f, r.GetCenter().y - ts.y * 0.5f), Col(tc), txt);
    NavRing(r, id, rr);
    if (!listening)
        ItemTooltip("Click to rebind, right-click to clear");
    return changed;
}

bool CheckboxKey(const char* label, bool* v, ImGuiKey* key)
{
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    if (win->SkipItems)
        return false;
    ImGuiID chip_id = SubId(win->GetID(label), 8);
    float cw = KeyChipWidth(chip_id, *key, dp(28));
    ImRect row;
    ImGuiID id = 0;
    bool pressed = CheckboxImpl(label, v, cw, &row, &id);
    if (id == 0)
        return pressed;
    float cy = IM_ROUND(row.GetCenter().y), ch = dp(22);
    bool changed = KeyChipAt(chip_id, ImRect(ImVec2(row.Max.x - cw, cy - ch * 0.5f), ImVec2(row.Max.x, cy + ch * 0.5f)), key);
    return pressed || changed;
}

bool Keybind(const char* label, ImGuiKey* key)
{
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    if (win->SkipItems)
        return false;
    ReportLabel(label);
    ImGuiID id = win->GetID(label);
    float w = ImGui::GetContentRegionAvail().x, h = dp(30);
    ImVec2 p = win->DC.CursorPos;
    ImGui::ItemSize(ImVec2(w, h));
    float cw = KeyChipWidth(id, *key, dp(60)), ch = dp(24);
    float cy = IM_ROUND(p.y + h * 0.5f);
    RowLabel(win->DrawList, p, h, label, P.textDim, w - cw - dp(8));
    return KeyChipAt(id, ImRect(ImVec2(p.x + w - cw, cy - ch * 0.5f), ImVec2(p.x + w, cy + ch * 0.5f)), key);
}

//-----------------------------------------------------------------------------
// Slider / Combo / Color / Pills
//-----------------------------------------------------------------------------

static bool SliderImpl(const char* label, float* v, float vmin, float vmax, const char* fmt, bool is_int)
{
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    if (win->SkipItems)
        return false;
    ReportLabel(label);
    ImGuiID id = win->GetID(label);
    float w = ImGui::GetContentRegionAvail().x, h = dp(40);
    ImVec2 p = win->DC.CursorPos;
    float inset = dp(5);
    float ty = p.y + dp(30);
    ImRect track(ImVec2(p.x + inset, ty - dp(2)), ImVec2(p.x + w - inset, ty + dp(2)));
    ImRect hit(ImVec2(p.x, p.y + dp(20)), ImVec2(p.x + w, p.y + h));
    ImGui::ItemSize(ImVec2(w, h));
    if (!ImGui::ItemAdd(hit, id))
        return false;

    bool hov, held;
    ImGui::ButtonBehavior(hit, id, &hov, &held, ImGuiButtonFlags_PressedOnClick);
    bool changed = false;
    if (held)
    {
        float t = ImSaturate((ImGui::GetIO().MousePos.x - track.Min.x) / track.GetWidth());
        float nv = vmin + t * (vmax - vmin);
        if (is_int)
            nv = roundf(nv);
        if (nv != *v)
        {
            *v = nv;
            changed = true;
            ImGui::MarkItemEdited(id);
        }
    }
    // Focused (clicked or reached with the keyboard): Left/Right adjust the value instead of moving focus.
    if (ImGui::IsItemFocused() && !held)
    {
        ImGui::SetKeyOwner(ImGuiKey_LeftArrow, id);
        ImGui::SetKeyOwner(ImGuiKey_RightArrow, id);
        float step = is_int ? 1.0f : (vmax - vmin) / 100.0f;
        if (ImGui::GetIO().KeyShift)
            step *= 10.0f;
        float nv = *v;
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, ImGuiInputFlags_Repeat, id))  nv = ImMax(vmin, nv - step);
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, ImGuiInputFlags_Repeat, id)) nv = ImMin(vmax, nv + step);
        if (nv != *v)
        {
            *v = nv;
            changed = true;
            ImGui::MarkItemEdited(id);
        }
    }

    float af = Anim(id, ImSaturate((*v - vmin) / (vmax - vmin)), 24.0f);
    float hv = Anim(SubId(id, 1), (hov || held) ? 1.0f : 0.0f, 16.0f);
    float act = Anim(SubId(id, 2), held ? 1.0f : 0.0f, 16.0f);
    ImDrawList* dl = win->DrawList;

    float fs = dp(kTextM);
    char buf[48];
    if (is_int) snprintf(buf, sizeof(buf), fmt, (int)*v);
    else        snprintf(buf, sizeof(buf), fmt, *v);
    ImVec2 ts = Measure(F.semibold, dp(kTextS), buf);
    TextFit(dl, F.regular, fs, ImVec2(p.x, p.y + dp(1)), Col(Mix(P.textDim, P.text, hv)), label, w - ts.x - dp(8), ImGui::FindRenderedTextEnd(label));
    TextAt(dl, F.semibold, dp(kTextS), ImVec2(p.x + w - ts.x, p.y + dp(1) + (LineH(F.regular, fs) - ts.y) * 0.5f), Col(Mix(P.accent, ImVec4(1, 1, 1, 1), 0.25f * act)), buf);

    float tr = track.GetHeight() * 0.5f;
    dl->AddRectFilled(track.Min, track.Max, Col(P.hover), tr);
    float fx = ImLerp(track.Min.x, track.Max.x, af);
    if (fx > track.Min.x + 1.0f)
        GradientRect(dl, track.Min, ImVec2(fx, track.Max.y), P.accent2, P.accent, tr);
    ImVec2 kc(fx, ty);
    float kr = dp(5) + dp(1) * hv;
    if (glow)
        RadialGlow(dl, kc, dp(18), P.accent, 0.20f + 0.30f * hv);
    dl->AddCircleFilled(kc, kr, Col(P.accent), 24);
    dl->AddCircleFilled(kc, kr - dp(2), Col(ImVec4(1, 1, 1, 1)), 24);
    NavRing(ImRect(p, p + ImVec2(w, h)), id, RControl());
    return changed;
}

bool Slider(const char* label, float* v, float vmin, float vmax, const char* fmt) { return SliderImpl(label, v, vmin, vmax, fmt, false); }

bool SliderInt(const char* label, int* v, int vmin, int vmax, const char* fmt)
{
    float f = (float)*v;
    bool changed = SliderImpl(label, &f, (float)vmin, (float)vmax, fmt, true);
    *v = (int)f;
    return changed;
}

bool Combo(const char* label, int* current, const char* const* items, int count)
{
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    if (win->SkipItems)
        return false;
    ReportLabel(label);
    ImGui::PushID(label);
    float w = ImGui::GetContentRegionAvail().x, h = dp(30), fh = dp(28);
    float fw = ImMin(dp(148), w * 0.56f);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* row_dl = win->DrawList;
    RowLabel(row_dl, p, h, label, P.textDim, w - fw - dp(8));

    float fs = ImGui::GetFontSize();
    ImVec2 fp(p.x + w - fw, p.y + (h - fh) * 0.5f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(dp(10), (fh - fs) * 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(dp(4), dp(4)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(dp(4), dp(1)));
    ImGui::PushStyleColor(ImGuiCol_Border, P.borderStrong);
    ImGui::SetCursorScreenPos(fp);
    ImGui::SetNextItemWidth(fw);
    bool changed = false;
    // Empty preview: the value is drawn below with an ellipsis so it never runs under the chevron.
    bool open = ImGui::BeginCombo("##combo", "", ImGuiComboFlags_NoArrowButton | ImGuiComboFlags_HeightLarge);
    float ca = Anim(ImGui::GetID("##chev"), open ? 1.0f : 0.0f, 18.0f);
    TextFit(row_dl, F.regular, dp(kTextM), ImVec2(fp.x + dp(10), fp.y + (fh - LineH(F.regular, dp(kTextM))) * 0.5f), Col(P.text), items[*current], fw - dp(10) - dp(24));
    DrawIcon(row_dl, icon::ChevronDown, ImVec2(p.x + w - dp(14), p.y + h * 0.5f), dp(10), Col(Mix(P.textDim, P.accent, ca)));
    if (open)
    {
        for (int i = 0; i < count; i++)
        {
            bool sel = (i == *current);
            ImGui::PushID(i);
            if (ImGui::Selectable("##item", sel, 0, ImVec2(0, dp(28))))
            {
                *current = i;
                changed = true;
            }
            ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            float cy = (a.y + b.y) * 0.5f;
            TextFit(dl, F.regular, dp(kTextM), ImVec2(a.x + dp(8), cy - LineH(F.regular, dp(kTextM)) * 0.5f), Col(sel ? P.text : P.textDim), items[i], b.x - a.x - dp(30));
            if (sel)
            {
                DrawIcon(dl, icon::Check, ImVec2(b.x - dp(12), cy), dp(10), Col(P.accent));
                ImGui::SetItemDefaultFocus();
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(4);
    ImGui::SetCursorScreenPos(p);
    ImGui::Dummy(ImVec2(w, h));
    ImGui::PopID();
    return changed;
}

bool ColorRow(const char* label, float col[4])
{
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    if (win->SkipItems)
        return false;
    ReportLabel(label);
    ImGuiID id = win->GetID(label);
    float w = ImGui::GetContentRegionAvail().x, h = dp(30), sw = dp(16);
    ImVec2 p = win->DC.CursorPos;
    ImGui::ItemSize(ImVec2(w, h));
    char hex[16];
    snprintf(hex, sizeof(hex), "#%02X%02X%02X", (int)(col[0] * 255 + 0.5f), (int)(col[1] * 255 + 0.5f), (int)(col[2] * 255 + 0.5f));
    ImVec2 ts = Measure(F.regular, dp(kTextS), hex);
    RowLabel(win->DrawList, p, h, label, P.textDim, w - sw - ts.x - dp(16));
    float cy = IM_ROUND(p.y + h * 0.5f);
    TextAt(win->DrawList, F.regular, dp(kTextS), ImVec2(p.x + w - sw - dp(8) - ts.x, cy - ts.y * 0.5f), Col(P.textMute), hex);
    return SwatchAt(id, ImRect(ImVec2(p.x + w - sw, cy - sw * 0.5f), ImVec2(p.x + w, cy + sw * 0.5f)), col);
}

bool Pills(const char* str_id, int* current, const char* const* items, int count, float width)
{
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    if (win->SkipItems || count <= 0 || count > 16)
        return false;
    ImGuiID id = win->GetID(str_id);
    const float fs = dp(kTextM), h = dp(32), pad = dp(3), gap = dp(2);
    float widths[16], total;
    if (width < 0.0f)
    {
        total = pad * 2 + gap * (count - 1);
        for (int i = 0; i < count; i++)
            total += (widths[i] = Measure(F.semibold, fs, items[i]).x + dp(28));
    }
    else
    {
        total = width > 0.0f ? width : ImGui::GetContentRegionAvail().x;
        float each = (total - pad * 2 - gap * (count - 1)) / count;
        for (int i = 0; i < count; i++)
            widths[i] = each;
    }
    ImVec2 p = win->DC.CursorPos;
    ImRect bb(p, p + ImVec2(total, h));
    ImGui::ItemSize(bb.GetSize());
    ImDrawList* dl = win->DrawList;
    dl->AddRectFilled(bb.Min, bb.Max, Col(P.field), dp(8));
    dl->AddRect(bb.Min, bb.Max, Col(P.border), dp(8));

    float xs[16];
    float x = p.x + pad;
    for (int i = 0; i < count; i++) { xs[i] = x; x += widths[i] + gap; }
    float hx = Anim(SubId(id, 100), xs[*current], 18.0f);
    float hw = Anim(SubId(id, 101), widths[*current], 18.0f);
    ImVec2 ha(hx, p.y + pad), hb(hx + hw, p.y + h - pad);
    dl->AddRectFilled(ha, hb, Col(P.hover), dp(6));
    dl->AddLine(ImVec2(ha.x + dp(6), ha.y + 0.5f), ImVec2(hb.x - dp(6), ha.y + 0.5f), Col(ImVec4(1, 1, 1, 0.05f)));

    bool changed = false;
    for (int i = 0; i < count; i++)
    {
        ImRect r(ImVec2(xs[i], p.y + pad), ImVec2(xs[i] + widths[i], p.y + h - pad));
        ImGuiID cid = SubId(id, i + 1);
        if (!ImGui::ItemAdd(r, cid))
            continue;
        bool hov, held;
        if (ImGui::ButtonBehavior(r, cid, &hov, &held) && *current != i)
        {
            *current = i;
            changed = true;
            ImGui::MarkItemEdited(cid);
        }
        float on = ImSaturate(1.0f - fabsf(hx - xs[i]) / ImMax(1.0f, widths[i]));
        float hv = Anim(SubId(cid, 9), hov ? 1.0f : 0.0f, 18.0f);
        ImFont* f = (i == *current) ? F.semibold : F.regular;
        float tw = ImMin(Measure(f, fs, items[i]).x, widths[i] - dp(8));
        TextFit(dl, f, fs, ImVec2(r.GetCenter().x - tw * 0.5f, r.GetCenter().y - LineH(f, fs) * 0.5f), Col(Mix(Mix(P.textDim, P.text, hv * 0.6f), P.text, on)), items[i], widths[i] - dp(8));
        NavRing(r, cid, dp(6));
    }
    return changed;
}

bool AccentPicker()
{
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    if (win->SkipItems)
        return false;
    ImGui::PushID("##accent");
    float d = dp(18), gap = dp(10);
    ImVec2 p = win->DC.CursorPos;
    ImGui::ItemSize(ImVec2(ImGui::GetContentRegionAvail().x, d + dp(8)));
    ImDrawList* dl = win->DrawList;
    bool changed = false;
    for (int i = 0; i < kAccentCount; i++)
    {
        ImGuiID id = win->GetID(i);
        ImVec2 c(p.x + d * 0.5f + dp(4) + i * (d + gap), p.y + d * 0.5f + dp(4));
        ImRect r(c - ImVec2(d, d) * 0.5f, c + ImVec2(d, d) * 0.5f);
        if (!ImGui::ItemAdd(r, id))
            continue;
        bool hov, held;
        if (ImGui::ButtonBehavior(r, id, &hov, &held) && accent != i)
        {
            SetAccent(i);
            changed = true;
        }
        ItemTooltip(kAccents[i].name);
        float on = Anim(id, accent == i ? 1.0f : 0.0f, 14.0f);
        float hv = Anim(SubId(id, 1), hov ? 1.0f : 0.0f, 18.0f);
        float rad = d * 0.5f - dp(1) * (1.0f - hv) - dp(1.5f) * on;
        int v0 = dl->VtxBuffer.Size;
        dl->AddCircleFilled(c, rad, Col(ImVec4(1, 1, 1, 1)), 32);
        ShadeLinear(dl, v0, dl->VtxBuffer.Size, c - ImVec2(rad, rad), c + ImVec2(rad, rad), kAccents[i].a, kAccents[i].b);
        if (on > 0.01f)
        {
            dl->AddCircle(c, d * 0.5f + dp(2.5f), Col(kAccents[i].a, on), 32, dp(1.5f));
            DrawIcon(dl, icon::Check, c, dp(8), Col(ImVec4(1, 1, 1, 1), on));
        }
        NavRing(r, id, d * 0.5f);
    }
    ImGui::PopID();
    return changed;
}

void KeyValue(const char* key, const char* value, bool mono)
{
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    if (win->SkipItems)
        return;
    ImVec2 p = win->DC.CursorPos;
    float w = ImGui::GetContentRegionAvail().x, h = dp(26);
    ImGui::ItemSize(ImVec2(w, h));
    ImDrawList* dl = win->DrawList;
    float fs = dp(kTextS);
    float kw = Measure(F.regular, fs, key).x;
    TextAt(dl, F.regular, fs, ImVec2(p.x, p.y + (h - LineH(F.regular, fs)) * 0.5f), Col(P.textMute), key);
    ImFont* vf = F.semibold; // numbers use the UI font too (tabular digits); `mono` kept for API compatibility
    float vs = fs;
    (void)mono;
    float max_v = w - kw - dp(12);
    float vw = ImMin(Measure(vf, vs, value).x, max_v);
    TextFit(dl, vf, vs, ImVec2(p.x + w - vw, p.y + (h - LineH(vf, vs)) * 0.5f), Col(P.text), value, max_v);
}

//-----------------------------------------------------------------------------
// Buttons
//-----------------------------------------------------------------------------

bool Button(const char* label, unsigned ic, ButtonKind kind, ImVec2 size)
{
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    if (win->SkipItems)
        return false;
    ReportLabel(label);
    ImGuiID id = win->GetID(label);
    const char* end = ImGui::FindRenderedTextEnd(label);
    float fs = dp(kTextM);
    ImVec2 ts = Measure(F.semibold, fs, label, end);
    float iw = ic ? dp(13) + dp(7) : 0.0f;
    ImVec2 sz(size.x > 0 ? size.x : (size.x < 0 ? ImGui::GetContentRegionAvail().x : ts.x + iw + dp(26)), size.y > 0 ? size.y : dp(32));
    if (ic && ts.x + iw > sz.x - dp(16)) // too tight for icon + label: keep the label, drop the icon
    {
        ic = 0;
        iw = 0.0f;
    }
    ImVec2 p = win->DC.CursorPos;
    ImRect bb(p, p + sz);
    ImGui::ItemSize(sz);
    if (!ImGui::ItemAdd(bb, id))
        return false;
    bool hov, held;
    bool pressed = ImGui::ButtonBehavior(bb, id, &hov, &held);
    float hv = Anim(SubId(id, 1), hov ? 1.0f : 0.0f, 16.0f);
    float pr = Anim(SubId(id, 2), held ? 1.0f : 0.0f, 28.0f);
    ImDrawList* dl = win->DrawList;
    float r = RControl();
    ImVec4 white(1, 1, 1, 1), tc;
    switch (kind)
    {
    case Btn_Primary:
        if (glow)
            SoftShadow(dl, bb.Min, bb.Max, r, P.accent, 0.15f + 0.35f * hv, dp(8));
        GradientRect(dl, bb.Min, bb.Max, Mix(P.accent, white, 0.08f * hv - 0.08f * pr), Mix(P.accent2, white, 0.08f * hv - 0.08f * pr), r);
        dl->AddLine(ImVec2(bb.Min.x + r, bb.Min.y + 0.5f), ImVec2(bb.Max.x - r, bb.Min.y + 0.5f), Col(white, 0.18f));
        tc = white;
        break;
    case Btn_Ghost:
        dl->AddRectFilled(bb.Min, bb.Max, Col(Mix(P.field, P.hover, hv - pr * 0.4f)), r);
        dl->AddRect(bb.Min, bb.Max, Col(P.borderStrong), r);
        tc = Mix(P.textDim, P.text, hv);
        break;
    case Btn_Danger:
        dl->AddRectFilled(bb.Min, bb.Max, Col(P.danger, 0.10f + 0.10f * hv), r);
        dl->AddRect(bb.Min, bb.Max, Col(P.danger, 0.30f + 0.25f * hv), r);
        tc = Mix(P.danger, white, 0.2f + 0.3f * hv);
        break;
    }
    float avail = sz.x - dp(16) - iw;
    float tw = ImMin(ts.x, avail);
    float cx = bb.GetCenter().x - (tw + iw) * 0.5f;
    if (ic)
        DrawIcon(dl, ic, ImVec2(cx + dp(6.5f), bb.GetCenter().y), dp(13), Col(tc));
    TextFit(dl, F.semibold, fs, ImVec2(cx + iw, bb.GetCenter().y - ts.y * 0.5f), Col(tc), label, avail, end);
    NavRing(bb, id, r);
    return pressed;
}

bool IconButton(const char* str_id, unsigned ic, const char* tooltip, float size, bool danger)
{
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    if (win->SkipItems)
        return false;
    ImGuiID id = win->GetID(str_id);
    if (size <= 0.0f)
        size = dp(28);
    ImVec2 p = win->DC.CursorPos;
    ImRect bb(p, p + ImVec2(size, size));
    ImGui::ItemSize(bb.GetSize());
    if (!ImGui::ItemAdd(bb, id))
        return false;
    bool hov, held;
    bool pressed = ImGui::ButtonBehavior(bb, id, &hov, &held);
    float hv = Anim(SubId(id, 1), hov ? 1.0f : 0.0f, 18.0f);
    ImDrawList* dl = win->DrawList;
    if (hv > 0.01f)
        dl->AddRectFilled(bb.Min, bb.Max, danger ? Col(P.danger, 0.16f * hv) : Col(P.hover, hv), dp(7));
    ImVec4 ic_col = danger ? Mix(P.textDim, P.danger, hv) : Mix(P.textDim, P.text, hv);
    DrawIcon(dl, ic, bb.GetCenter(), dp(13), Col(ic_col));
    NavRing(bb, id, dp(7));
    if (tooltip)
        ItemTooltip(tooltip);
    return pressed;
}

void Badge(const char* text, ImVec4 col)
{
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    ImVec2 ts = Measure(F.semibold, dp(kTextXS), text);
    ImVec2 p = win->DC.CursorPos;
    ImVec2 sz(ts.x + dp(12), ts.y + dp(4));
    ImGui::ItemSize(sz);
    win->DrawList->AddRectFilled(p, p + sz, Col(col, 0.15f), sz.y * 0.5f);
    TextAt(win->DrawList, F.semibold, dp(kTextXS), p + ImVec2(dp(6), dp(2)), Col(Mix(col, ImVec4(1, 1, 1, 1), 0.2f)), text);
}

//-----------------------------------------------------------------------------
// Data viz
//-----------------------------------------------------------------------------

static void AreaFill(ImDrawList* dl, const ImVec2* pts, int n, float ybase, float ytop, ImVec4 col, float alpha)
{
    const ImVec2 uv = dl->_Data->TexUvWhitePixel;
    alpha *= ImGui::GetStyle().Alpha;
    dl->PrimReserve((n - 1) * 6, n * 2);
    const unsigned base = dl->_VtxCurrentIdx;
    for (int i = 0; i < n; i++)
    {
        float t = ImSaturate((ybase - pts[i].y) / ImMax(1.0f, ybase - ytop));
        dl->PrimWriteVtx(pts[i], uv, ImGui::ColorConvertFloat4ToU32(WithAlpha(col, alpha * (0.35f + 0.65f * t))));
        dl->PrimWriteVtx(ImVec2(pts[i].x, ybase), uv, ImGui::ColorConvertFloat4ToU32(WithAlpha(col, 0.0f)));
    }
    for (int i = 0; i < n - 1; i++)
    {
        ImDrawIdx t0 = (ImDrawIdx)(base + i * 2), b0 = (ImDrawIdx)(t0 + 1), t1 = (ImDrawIdx)(t0 + 2), b1 = (ImDrawIdx)(t0 + 3);
        dl->PrimWriteIdx(t0); dl->PrimWriteIdx(t1); dl->PrimWriteIdx(b1);
        dl->PrimWriteIdx(t0); dl->PrimWriteIdx(b1); dl->PrimWriteIdx(b0);
    }
}

static void BuildPoints(ImVector<ImVec2>& pts, ImVec2 a, ImVec2 b, const float* v, int n, int offset, float vmin, float vmax)
{
    pts.resize(n);
    for (int i = 0; i < n; i++)
    {
        float t = ImSaturate((v[(offset + i) % n] - vmin) / (vmax - vmin));
        pts[i] = ImVec2(ImLerp(a.x, b.x, (float)i / (n - 1)), ImLerp(b.y, a.y, t));
    }
}

void Sparkline(ImDrawList* dl, ImVec2 a, ImVec2 b, const float* v, int n, int offset, float vmin, float vmax, float alpha)
{
    if (n < 2)
        return;
    ImVector<ImVec2> pts;
    BuildPoints(pts, a, b, v, n, offset, vmin, vmax);
    AreaFill(dl, pts.Data, n, b.y, a.y, P.accent, 0.28f * alpha);
    int v0 = dl->VtxBuffer.Size;
    dl->AddPolyline(pts.Data, n, Col(ImVec4(1, 1, 1, 1), alpha), ImDrawFlags_None, dp(1.5f));
    ShadeLinear(dl, v0, dl->VtxBuffer.Size, a, ImVec2(b.x, a.y), P.accent2, P.accent);
    dl->AddCircleFilled(pts[n - 1], dp(2.5f), Col(P.accent, alpha));
}

void LineChart(const char* str_id, const float* v, int n, int offset, float vmin, float vmax, float height, const char* fmt)
{
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    if (win->SkipItems || n < 2)
        return;
    ImGuiID id = win->GetID(str_id);
    float w = ImGui::GetContentRegionAvail().x;
    ImVec2 p = win->DC.CursorPos;
    ImRect bb(p, p + ImVec2(w, height));
    ImGui::ItemSize(bb.GetSize());
    if (!ImGui::ItemAdd(bb, id))
        return;
    ImDrawList* dl = win->DrawList;
    float axis = dp(30);
    ImRect plot(ImVec2(bb.Min.x + axis, bb.Min.y + dp(6)), ImVec2(bb.Max.x, bb.Max.y - dp(4)));
    char buf[32];
    for (int g = 0; g <= 2; g++)
    {
        float y = IM_ROUND(ImLerp(plot.Max.y, plot.Min.y, g / 2.0f));
        dl->AddLine(ImVec2(plot.Min.x, y), ImVec2(plot.Max.x, y), Col(P.border, g == 0 ? 1.0f : 0.6f));
        snprintf(buf, sizeof(buf), fmt, ImLerp(vmin, vmax, g / 2.0f));
        ImVec2 ts = Measure(F.regular, dp(kTextXS), buf);
        TextAt(dl, F.regular, dp(kTextXS), ImVec2(plot.Min.x - ts.x - dp(8), y - ts.y * 0.5f), Col(P.textMute), buf);
    }
    ImVector<ImVec2> pts;
    BuildPoints(pts, plot.Min, plot.Max, v, n, offset, vmin, vmax);
    AreaFill(dl, pts.Data, n, plot.Max.y, plot.Min.y, P.accent, 0.32f);
    int v0 = dl->VtxBuffer.Size;
    dl->AddPolyline(pts.Data, n, Col(ImVec4(1, 1, 1, 1)), ImDrawFlags_None, dp(1.5f));
    ShadeLinear(dl, v0, dl->VtxBuffer.Size, plot.Min, ImVec2(plot.Max.x, plot.Min.y), P.accent2, P.accent);

    if (ImGui::IsItemHovered())
    {
        int idx = (int)roundf(ImSaturate((ImGui::GetIO().MousePos.x - plot.Min.x) / plot.GetWidth()) * (n - 1));
        ImVec2 pt = pts[idx];
        dl->AddLine(ImVec2(pt.x, plot.Min.y), ImVec2(pt.x, plot.Max.y), Col(P.text, 0.15f));
        dl->AddCircleFilled(pt, dp(4), Col(ImVec4(1, 1, 1, 1)));
        dl->AddCircleFilled(pt, dp(2), Col(P.accent));
        snprintf(buf, sizeof(buf), fmt, v[(offset + idx) % n]);
        ImVec2 ts = Measure(F.semibold, dp(kTextS), buf);
        ImVec2 ta(ImClamp(pt.x - ts.x * 0.5f - dp(7), plot.Min.x, plot.Max.x - ts.x - dp(14)), ImMax(bb.Min.y, pt.y - ts.y - dp(16)));
        ImVec2 tb = ta + ImVec2(ts.x + dp(14), ts.y + dp(6));
        dl->AddRectFilled(ta, tb, Col(Hex(0x0D0E13)), dp(5));
        dl->AddRect(ta, tb, Col(P.borderStrong), dp(5));
        TextAt(dl, F.semibold, dp(kTextS), ta + ImVec2(dp(7), dp(3)), Col(P.text), buf);
    }
}

void Ring(const char* str_id, float frac, const char* value, const char* label, float radius)
{
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    if (win->SkipItems)
        return;
    ImGuiID id = win->GetID(str_id);
    float th = dp(5);
    ImVec2 p = win->DC.CursorPos;
    float lh = dp(18);
    ImVec2 sz(radius * 2 + th, radius * 2 + th + lh);
    ImGui::ItemSize(sz);
    ImGui::ItemAdd(ImRect(p, p + sz), id);
    float f = Anim(id, ImSaturate(frac), 6.0f, false); // live data: allowed to step at the idle rate
    ImDrawList* dl = win->DrawList;
    ImVec2 c(p.x + sz.x * 0.5f, p.y + radius + th * 0.5f);
    dl->AddCircle(c, radius, Col(P.hover), 48, th);
    if (f > 0.002f)
    {
        float a0 = -IM_PI * 0.5f, a1 = a0 + IM_PI * 2.0f * f;
        int v0 = dl->VtxBuffer.Size;
        dl->PathArcTo(c, radius, a0, a1, 48);
        dl->PathStroke(Col(ImVec4(1, 1, 1, 1)), 0, th);
        dl->AddCircleFilled(ImVec2(c.x + cosf(a0) * radius, c.y + sinf(a0) * radius), th * 0.5f, Col(ImVec4(1, 1, 1, 1)), 12);
        dl->AddCircleFilled(ImVec2(c.x + cosf(a1) * radius, c.y + sinf(a1) * radius), th * 0.5f, Col(ImVec4(1, 1, 1, 1)), 12);
        ShadeLinear(dl, v0, dl->VtxBuffer.Size, c - ImVec2(radius, radius), c + ImVec2(radius, radius), P.accent, P.accent2);
    }
    ImVec2 vs = Measure(F.semibold, dp(kTextM), value);
    TextAt(dl, F.semibold, dp(kTextM), ImVec2(c.x - vs.x * 0.5f, c.y - vs.y * 0.5f), Col(P.text), value);
    ImVec2 ls = Measure(F.regular, dp(kTextXS), label);
    TextAt(dl, F.regular, dp(kTextXS), ImVec2(c.x - ls.x * 0.5f, p.y + radius * 2 + th + dp(4)), Col(P.textMute), label);
}

void StatCell(const char* str_id, const char* label, const char* value, const float* hist, int n, int offset, float vmin, float vmax, float width, float height)
{
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    if (win->SkipItems)
        return;
    ImGuiID id = win->GetID(str_id);
    ImVec2 p = win->DC.CursorPos;
    ImRect bb(p, p + ImVec2(width, height));
    ImGui::ItemSize(bb.GetSize());
    if (!ImGui::ItemAdd(bb, id))
        return;
    float hv = Anim(SubId(id, 1), ImGui::IsItemHovered() ? 1.0f : 0.0f, 14.0f);
    ImDrawList* dl = win->DrawList;
    dl->AddRectFilled(bb.Min, bb.Max, Col(Mix(P.field, P.hover, hv * 0.6f), 0.8f), dp(8));
    dl->AddRect(bb.Min, bb.Max, Col(Mix(P.border, P.borderStrong, hv)), dp(8));
    TextFit(dl, F.regular, dp(kTextXS), bb.Min + ImVec2(dp(10), dp(7)), Col(P.textMute), label, width - dp(20));
    TextFit(dl, F.semibold, dp(kTextXL), bb.Min + ImVec2(dp(10), dp(22)), Col(P.text), value, width * 0.55f - dp(12));
    if (hist && n > 1)
        Sparkline(dl, ImVec2(bb.Min.x + width * 0.55f, bb.Min.y + dp(12)), ImVec2(bb.Max.x - dp(10), bb.Max.y - dp(12)), hist, n, offset, vmin, vmax);
}

bool Equalizer(const char* str_id, float* bands, int n, const char* const* labels, float height)
{
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    if (win->SkipItems)
        return false;
    ImGuiID id = win->GetID(str_id);
    float w = ImGui::GetContentRegionAvail().x;
    ImVec2 p = win->DC.CursorPos;
    ImGui::ItemSize(ImVec2(w, height));
    ImDrawList* dl = win->DrawList;
    float colw = w / n;
    float top = p.y + dp(18), bottom = p.y + height - dp(20);
    float zero = (top + bottom) * 0.5f;
    for (float db = -12.0f; db <= 12.0f; db += 6.0f)
    {
        float y = IM_ROUND(ImLerp(bottom, top, (db + 12.0f) / 24.0f));
        dl->AddLine(ImVec2(p.x, y), ImVec2(p.x + w, y), Col(P.border, db == 0.0f ? 1.0f : 0.45f));
    }
    bool changed = false;
    IM_ASSERT(n <= 32);
    ImVec2 knobs[32];
    float hvs[32];
    int count = 0;
    for (int i = 0; i < n; i++)
    {
        ImGuiID bid = SubId(id, i + 1);
        float cx = IM_ROUND(p.x + colw * (i + 0.5f));
        ImRect hit(ImVec2(p.x + colw * i, top - dp(8)), ImVec2(p.x + colw * (i + 1), bottom + dp(8)));
        if (!ImGui::ItemAdd(hit, bid))
            continue;
        bool hovered, held;
        ImGui::ButtonBehavior(hit, bid, &hovered, &held, ImGuiButtonFlags_PressedOnClick);
        if (held)
        {
            float t = ImSaturate((bottom - ImGui::GetIO().MousePos.y) / (bottom - top));
            float nv = roundf((t * 24.0f - 12.0f) * 2.0f) * 0.5f;
            if (nv != bands[i]) { bands[i] = nv; changed = true; ImGui::MarkItemEdited(bid); }
        }
        if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) { bands[i] = 0.0f; changed = true; }
        // Focused band: Up/Down adjust by 0.5 dB (Left/Right still move between bands).
        if (ImGui::IsItemFocused() && !held)
        {
            ImGui::SetKeyOwner(ImGuiKey_UpArrow, bid);
            ImGui::SetKeyOwner(ImGuiKey_DownArrow, bid);
            float nv = bands[i];
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, ImGuiInputFlags_Repeat, bid))   nv = ImMin(12.0f, nv + 0.5f);
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, ImGuiInputFlags_Repeat, bid)) nv = ImMax(-12.0f, nv - 0.5f);
            if (nv != bands[i]) { bands[i] = nv; changed = true; ImGui::MarkItemEdited(bid); }
        }
        NavRing(hit, bid, dp(6));
        float val = Anim(bid, bands[i], 18.0f);
        float y = ImLerp(bottom, top, (val + 12.0f) / 24.0f);
        float tw = dp(4);
        dl->AddRectFilled(ImVec2(cx - tw * 0.5f, top), ImVec2(cx + tw * 0.5f, bottom), Col(P.hover), tw);
        ImVec2 fa(cx - tw * 0.5f, ImMin(y, zero)), fb(cx + tw * 0.5f, ImMax(y, zero));
        if (fb.y - fa.y > 1.0f)
            GradientRect(dl, fa, fb, y < zero ? P.accent : P.accent2, y < zero ? P.accent2 : P.accent, tw * 0.5f, 0, true);
        ImVec2 ls = Measure(F.regular, dp(kTextXS), labels[i]);
        TextAt(dl, F.regular, dp(kTextXS), ImVec2(cx - ls.x * 0.5f, bottom + dp(8)), Col(P.textMute), labels[i]);
        hvs[count] = Anim(SubId(bid, 1), (hovered || held || ImGui::IsItemFocused()) ? 1.0f : 0.0f, 18.0f);
        knobs[count++] = ImVec2(cx, y);
    }
    if (count > 1)
    {
        dl->PathLineTo(knobs[0]);
        for (int i = 0; i < count - 1; i++)
        {
            ImVec2 a = knobs[i], b = knobs[i + 1];
            float dx = (b.x - a.x) * 0.5f;
            dl->PathBezierCubicCurveTo(ImVec2(a.x + dx, a.y), ImVec2(b.x - dx, b.y), b, 12);
        }
        dl->PathStroke(Col(P.accent, 0.45f), 0, dp(1.5f));
    }
    for (int i = 0; i < count; i++)
    {
        ImVec2 k = knobs[i];
        float kr = dp(5.5f) + dp(1) * hvs[i];
        if (glow)
            RadialGlow(dl, k, dp(18), P.accent, 0.20f + 0.30f * hvs[i]);
        dl->AddCircleFilled(k, kr, Col(P.accent), 24);
        dl->AddCircleFilled(k, kr - dp(2), Col(ImVec4(1, 1, 1, 1)), 24);
        if (hvs[i] > 0.01f)
        {
            char buf[16];
            snprintf(buf, sizeof(buf), "%+.1f", bands[i]);
            ImVec2 ts = Measure(F.regular, dp(kTextXS), buf);
            TextAt(dl, F.regular, dp(kTextXS), ImVec2(k.x - ts.x * 0.5f, p.y), Col(P.text, hvs[i]), buf);
        }
    }
    return changed;
}

void LevelMeter(const char* str_id, float level, float height)
{
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    if (win->SkipItems)
        return;
    (void)str_id;
    float w = ImGui::GetContentRegionAvail().x;
    ImVec2 p = win->DC.CursorPos;
    ImGui::ItemSize(ImVec2(w, height));
    float lv = ImSaturate(level);
    ImDrawList* dl = win->DrawList;
    const int segs = 28;
    float gap = dp(2);
    float sw = (w - gap * (segs - 1)) / segs;
    for (int i = 0; i < segs; i++)
    {
        float t = (float)i / (segs - 1);
        ImVec4 c = t < 0.65f ? P.success : (t < 0.85f ? P.warning : P.danger);
        bool lit = t <= lv;
        ImVec2 a(p.x + i * (sw + gap), p.y), b(a.x + sw, p.y + height);
        dl->AddRectFilled(a, b, Col(lit ? c : P.hover), dp(1.5f));
    }
}

void Progress(float frac, float height)
{
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    float w = ImGui::GetContentRegionAvail().x;
    ImVec2 p = win->DC.CursorPos;
    ImGui::ItemSize(ImVec2(w, height));
    ImDrawList* dl = win->DrawList;
    dl->AddRectFilled(p, p + ImVec2(w, height), Col(P.hover), height * 0.5f);
    if (frac > 0.0f)
        GradientRect(dl, p, p + ImVec2(ImMax(height, w * ImSaturate(frac)), height), P.accent2, P.accent, height * 0.5f);
}

//-----------------------------------------------------------------------------
// Notifications
//-----------------------------------------------------------------------------

struct Toast
{
    ToastKind kind;
    std::string text, highlight;
    double t0;
    float life;
};
static std::vector<Toast> s_toasts;

void Notify(ToastKind kind, const char* text, const char* highlight)
{
    s_toasts.push_back({ kind, text, highlight ? highlight : "", ImGui::GetTime(), 3.0f });
    if (s_toasts.size() > 5)
        s_toasts.erase(s_toasts.begin());
}

bool ToastsActive() { return !s_toasts.empty(); }

// One line per event: [mark] | text highlight. Hairline timer along the bottom edge.
void RenderToasts()
{
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    double now = ImGui::GetTime();
    const float h = dp(34), fs = dp(kTextS), mark = dp(15), cell = dp(34), r = dp(5);
    const float max_w = ImMin(dp(360), ds.x - dp(24));
    float y = ds.y - dp(12);
    for (int i = (int)s_toasts.size() - 1; i >= 0; --i)
    {
        Toast& t = s_toasts[i];
        float age = (float)(now - t.t0);
        if (age > t.life + 0.25f)
        {
            s_toasts.erase(s_toasts.begin() + i);
            continue;
        }
        float in = Ease(age / 0.22f);
        float out = age > t.life ? 1.0f - ImSaturate((age - t.life) / 0.25f) : 1.0f;
        float a = in * out;
        bool alert = t.kind == Toast_Error || t.kind == Toast_Warning;
        ImVec4 hc = t.kind == Toast_Error ? P.danger : t.kind == Toast_Warning ? P.warning : P.accent;

        // Fit "text highlight" into the max width: the highlight keeps priority, the text gets an ellipsis.
        const float chrome = cell + dp(10) + dp(12), sep = t.highlight.empty() ? 0.0f : dp(4);
        float hw = t.highlight.empty() ? 0.0f : ImMin(Measure(F.semibold, fs, t.highlight.c_str()).x, (max_w - chrome) * 0.6f);
        float tw = ImMin(Measure(F.regular, fs, t.text.c_str()).x, max_w - chrome - sep - hw);
        float w = chrome + tw + sep + hw;
        float slide = (2.0f - in - out) * dp(18);
        ImVec2 pa(ds.x - dp(12) - w + slide, y - h), pb(pa.x + w, y);

        dl->AddRectFilled(pa + ImVec2(0, dp(2)), pb + ImVec2(0, dp(3)), Col(ImVec4(0, 0, 0, 1), 0.35f * a), r);
        dl->AddRectFilled(pa, pb, Col(Hex(0x0D0E13), 0.97f * a), r);
        dl->AddRect(pa, pb, Col(P.borderStrong, a), r);
        BrandMark(dl, ImVec2(pa.x + (cell - mark) * 0.5f, pa.y + (h - mark) * 0.5f), mark, alert ? hc : P.accent, alert ? hc : P.accent2, a);
        dl->AddLine(ImVec2(pa.x + cell, pa.y + dp(8)), ImVec2(pa.x + cell, pb.y - dp(8)), Col(P.borderStrong, a));

        float tx = pa.x + cell + dp(10), ty = pa.y + (h - LineH(F.regular, fs)) * 0.5f;
        float drawn = TextFit(dl, F.regular, fs, ImVec2(tx, ty), Col(P.text, a), t.text.c_str(), tw + 0.5f);
        if (!t.highlight.empty())
            TextFit(dl, F.semibold, fs, ImVec2(tx + drawn + sep, ty), Col(hc, a), t.highlight.c_str(), hw + 0.5f);

        float rem = 1.0f - ImSaturate(age / t.life);
        dl->AddLine(ImVec2(pa.x + r, pb.y - dp(1.5f)), ImVec2(pa.x + r + (w - 2 * r) * rem, pb.y - dp(1.5f)), Col(hc, 0.85f * a), 1.0f);
        y -= (h + dp(6)) * a;
    }
}
}
