#include "internal.h"
#include "imgui_internal.h"
#include <cmath>
#include <cstdio>
#include <initializer_list>

using namespace theme;
using namespace ui;

namespace menu
{
static void ActivityList(int max_items)
{
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    ImDrawList* dl = win->DrawList;
    const std::vector<Activity>& log = Activities();
    int n = ImMin((int)log.size(), max_items);
    if (n == 0)
        return;
    float row = dp(34), w = ImGui::GetContentRegionAvail().x;
    ImVec2 p = win->DC.CursorPos;
    float lx = p.x + dp(11);
    dl->AddLine(ImVec2(lx, p.y + row * 0.5f), ImVec2(lx, p.y + row * (n - 0.5f)), Col(P.border), dp(1.5f));
    char ago[16];
    for (int i = 0; i < n; i++)
    {
        const Activity& a = log[i];
        float cy = p.y + row * (i + 0.5f);
        dl->AddCircleFilled(ImVec2(lx, cy), dp(11), Col(P.group));
        dl->AddCircleFilled(ImVec2(lx, cy), dp(11), Col(i == 0 ? WithAlpha(P.accent, 0.20f) : P.field));
        DrawIcon(dl, a.icon, ImVec2(lx, cy), dp(10), Col(i == 0 ? P.accent : P.textDim));
        Ago(a.t, ago, sizeof(ago));
        ImVec2 as = Measure(F.regular, dp(kTextXS), ago);
        TextFit(dl, F.regular, dp(kTextS), ImVec2(p.x + dp(30), cy - LineH(F.regular, dp(kTextS)) * 0.5f), Col(i == 0 ? P.text : P.textDim), a.text.c_str(), w - dp(30) - as.x - dp(10));
        TextAt(dl, F.regular, dp(kTextXS), ImVec2(p.x + w - as.x, cy - as.y * 0.5f), Col(P.textMute), ago);
    }
    ImGui::ItemSize(ImVec2(w, row * n));
}

void HomeOverview()
{
    using namespace stats;
    if (!BeginColumns("##home", 0.54f))
        return;
    if (BeginGroup("##g_perf", icon::Pulse, "Performance"))
    {
        float gap = dp(8), cw = (ImGui::GetContentRegionAvail().x - gap * 2) / 3.0f, chh = dp(62);
        char v[32];
        float lo, hi;
        // Units live in the labels so the numbers always fit next to the sparkline.
        Range(L.renderSpark, kSpark, &lo, &hi);
        snprintf(v, sizeof(v), "%.2f", L.renderAvg);
        StatCell("##c_render", "Render ms", v, L.renderSpark, kSpark, L.sparkOff, ImMax(0.0f, lo), hi, cw, chh);
        ImGui::SameLine(0, gap);
        Range(L.memSpark, kSpark, &lo, &hi);
        snprintf(v, sizeof(v), "%.0f", L.mem);
        StatCell("##c_mem", "Memory MB", v, L.memSpark, kSpark, L.sparkOff, lo, hi, cw, chh);
        ImGui::SameLine(0, gap);
        Range(L.cpuSpark, kSpark, &lo, &hi);
        snprintf(v, sizeof(v), "%.1f", L.cpu);
        StatCell("##c_cpu", "CPU %", v, L.cpuSpark, kSpark, L.sparkOff, ImMin(lo, 0.0f), ImMax(hi, 2.0f), cw, chh);

        float worst = 0.0f;
        for (float f : L.renderMs) worst = ImMax(worst, f);
        float top = 1.0f;
        for (float s : { 1.0f, 2.0f, 5.0f, 10.0f, 20.0f, 50.0f })
            if ((top = s) >= worst * 1.25f)
                break;
        ImGui::Dummy(ImVec2(0, dp(2)));
        LineChart("##frames", L.renderMs, kHist, L.renderOff, 0.0f, top, dp(96), "%.1f");
    }
    EndGroup();

    if (BeginGroup("##g_session", icon::Clock, "Session"))
    {
        char buf[48];
        KeyValue("Config", S.configs.empty() ? "-" : S.configs[S.activeConfig].name.c_str());
        FormatUptime(buf, sizeof(buf));
        KeyValue("Uptime", buf, true);
        snprintf(buf, sizeof(buf), "%.1f%% of 1 core", L.cpu);
        KeyValue("CPU", buf, true);
        KeyValue("Renderer", "Direct3D 9, on demand");
    }
    EndGroup();

    NextColumn();
    if (BeginGroup("##g_quick", icon::Zap, "Quick actions"))
    {
        if (CheckboxKey("Performance mode", &S.perfMode, &S.perfKey))
            Notice(Toast_Info, icon::Zap, "Performance mode", S.perfMode ? "enabled" : "disabled");
        if (CheckboxKey("Overlay", &S.overlay, &S.overlayKey))
            Notice(Toast_Info, icon::Layers, "Overlay", S.overlay ? "enabled" : "disabled");
        if (Checkbox("Auto-save", &S.autosave))
            Log(icon::Save, "Auto-save %s", S.autosave ? "on" : "off");
        if (Checkbox("Cloud sync", &S.cloudSync))
            Log(icon::Sync, "Cloud sync %s", S.cloudSync ? "on" : "off");
        if (Checkbox("Do not disturb", &S.dnd))
            Log(icon::Bell, "Do not disturb %s", S.dnd ? "on" : "off");
    }
    EndGroup();

    if (BeginGroup("##g_health", icon::Shield, "Health"))
    {
        // Budget: share of a 60 Hz frame (16.7 ms) the UI needs per frame.
        float budget = L.renderAvg / 16.67f;
        float r = dp(24), iw = r * 2 + dp(5);
        float sp = ImMax(dp(4), (ImGui::GetContentRegionAvail().x - iw * 3) / 2.0f);
        char v[16];
        snprintf(v, sizeof(v), "%.0f%%", L.cpu);
        Ring("##r_cpu", L.cpu / 100.0f, v, "CPU", r);
        ImGui::SameLine(0, sp);
        snprintf(v, sizeof(v), "%.0f", L.mem);
        Ring("##r_mem", L.mem / 256.0f, v, "MB", r);
        ImGui::SameLine(0, sp);
        snprintf(v, sizeof(v), "%.0f%%", budget * 100.0f);
        Ring("##r_budget", budget, v, "Budget", r);
    }
    EndGroup();
    EndColumns();
}

void HomeActivity()
{
    if (!BeginColumns("##activity", 0.58f))
        return;
    if (BeginGroup("##g_log", icon::History, "Recent activity"))
        ActivityList(10);
    EndGroup();
    NextColumn();
    if (BeginGroup("##g_diag", icon::Pulse, "Diagnostics"))
    {
        ImGui::PushStyleColor(ImGuiCol_Text, P.textDim);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted("Checks fonts, renderer, memory and render time of this session.");
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, dp(2)));
        if (S.diag < 0.0f)
        {
            if (Button("Run diagnostics", icon::Pulse, Btn_Primary, ImVec2(-1, 0)))
            {
                S.diag = 0.0f;
                Log(icon::Pulse, "Diagnostics started");
            }
        }
        else
        {
            S.diag += ImMin(ImGui::GetIO().DeltaTime, 1.0f / 30.0f) / 1.8f;
            Progress(S.diag, dp(6));
            if (S.diag >= 1.0f)
            {
                S.diag = -1.0f;
                int fonts = 1 + (F.semibold != F.regular) + (F.bold != F.semibold) + (F.icons ? 1 : 0) + (F.mono != F.regular);
                snprintf(S.diagResult, sizeof(S.diagResult), "%d fonts, %.0f%% scale, %.2f ms, %.0f MB", fonts, dpi * 100.0f, stats::L.renderAvg, stats::L.mem);
                Notice(Toast_Success, icon::Check, "Diagnostics passed", S.diagResult);
            }
        }
        if (S.diagResult[0])
        {
            ImGui::Dummy(ImVec2(0, dp(2)));
            KeyValue("Last result", S.diagResult);
        }
    }
    EndGroup();
    EndColumns();
}
}
