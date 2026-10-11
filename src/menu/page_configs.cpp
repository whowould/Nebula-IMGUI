#include "internal.h"
#include "imgui_internal.h"
#include <cstdio>
#include <cstring>

using namespace theme;
using namespace ui;

namespace menu
{
static bool ConfigRow(int i)
{
    const Config& c = S.configs[i];
    bool sel = i == S.selConfig, active = i == S.activeConfig;
    ImGui::PushID(i);
    bool clicked = ImGui::Selectable("##cfg", sel, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0, dp(42)));
    bool dbl = clicked && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float cy = (a.y + b.y) * 0.5f;
    ImVec2 ic(a.x + dp(16), cy);
    dl->AddRectFilled(ic - ImVec2(dp(11), dp(11)), ic + ImVec2(dp(11), dp(11)), Col(active ? WithAlpha(P.accent, 0.18f) : P.field), dp(6));
    DrawIcon(dl, active ? icon::Star : icon::Folder, ic, dp(11), Col(active ? P.accent : P.textDim));

    float right = b.x - dp(8);
    if (active)
    {
        const char* t = "ACTIVE";
        ImVec2 ts = Measure(F.semibold, dp(kTextXS), t);
        ImVec2 ba(b.x - ts.x - dp(18), cy - ts.y * 0.5f - dp(2));
        dl->AddRectFilled(ba, ba + ImVec2(ts.x + dp(10), ts.y + dp(4)), Col(P.success, 0.14f), dp(8));
        TextAt(dl, F.semibold, dp(kTextXS), ba + ImVec2(dp(5), dp(2)), Col(P.success), t);
        right = ba.x - dp(8);
    }
    float tx = a.x + dp(36);
    TextFit(dl, F.semibold, dp(kTextM), ImVec2(tx, a.y + dp(3)), Col(P.text), c.name.c_str(), right - tx);
    char ago[16], meta[48];
    Ago(c.used, ago, sizeof(ago));
    if (!strcmp(ago, "now"))
        snprintf(meta, sizeof(meta), active ? "loaded just now" : "used just now");
    else
        snprintf(meta, sizeof(meta), active ? "loaded %s ago" : "used %s ago", ago);
    TextFit(dl, F.regular, dp(kTextXS), ImVec2(tx, a.y + dp(22)), Col(P.textMute), meta, right - tx);
    ImGui::PopID();
    if (clicked)
        S.selConfig = i;
    return dbl;
}

static void LoadConfig(int i)
{
    S.activeConfig = i;
    S.configs[i].used = ImGui::GetTime();
    Notice(Toast_Success, icon::Folder, "Config loaded", S.configs[i].name.c_str());
}

void PageConfigs()
{
    if (!BeginColumns("##cfg", 0.55f))
        return;
    if (BeginGroup("##g_list", icon::Database, "Configs"))
    {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, dp(2)));
        for (int i = 0; i < (int)S.configs.size(); i++)
            if (ConfigRow(i))
                LoadConfig(i);
        ImGui::PopStyleVar();
        Divider();
        float bw = dp(88);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(dp(10), (dp(32) - ImGui::GetFontSize()) * 0.5f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Border, P.borderStrong);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - bw - dp(8));
        bool enter = ImGui::InputTextWithHint("##newcfg", "New config name", S.newConfig, sizeof(S.newConfig), ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
        ImGui::SameLine(0, dp(8));
        if (Button("Create", icon::Plus, Btn_Primary, ImVec2(bw, dp(32))) || enter)
        {
            if (S.newConfig[0])
            {
                S.configs.push_back({ S.newConfig, "Captured from your current settings.", ImGui::GetTime(), ImGui::GetTime() });
                S.selConfig = (int)S.configs.size() - 1;
                Notice(Toast_Success, icon::Plus, "Config created", S.newConfig);
                S.newConfig[0] = 0;
            }
            else
                Notify(Toast_Warning, "Name required");
        }
    }
    EndGroup();
    NextColumn();

    if (S.selConfig >= (int)S.configs.size())
        S.selConfig = (int)S.configs.size() - 1;
    if (BeginGroup("##g_detail", icon::Info, "Details") && S.selConfig >= 0)
    {
        Config& c = S.configs[S.selConfig];
        ImDrawList* dl = ImGui::GetWindowDrawList();
        TextFit(dl, F.semibold, dp(kTextL), ImGui::GetCursorScreenPos(), Col(P.text), c.name.c_str(), ImGui::GetContentRegionAvail().x);
        ImGui::Dummy(ImVec2(0, dp(20)));
        ImGui::PushStyleColor(ImGuiCol_Text, P.textDim);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(c.desc.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, dp(2)));
        char ago[16], buf[48];
        Ago(c.created, ago, sizeof(ago));
        snprintf(buf, sizeof(buf), "%s ago", ago);
        KeyValue("Created", strcmp(ago, "now") ? buf : "just now");
        KeyValue("Status", S.selConfig == S.activeConfig ? "Active" : "Saved");
        ImGui::Dummy(ImVec2(0, dp(4)));
        float bw = (ImGui::GetContentRegionAvail().x - dp(8) * 2) / 3.0f;
        if (Button("Load", icon::Refresh, Btn_Primary, ImVec2(bw, 0)))
            LoadConfig(S.selConfig);
        ImGui::SameLine(0, dp(8));
        if (Button("Copy", icon::Copy, Btn_Ghost, ImVec2(bw, 0)))
        {
            Config copy = c;
            copy.name += " copy";
            copy.created = copy.used = ImGui::GetTime();
            S.configs.push_back(copy);
            Log(icon::Copy, "Duplicated %s", c.name.c_str());
        }
        ImGui::SameLine(0, dp(8));
        ImGui::BeginDisabled(S.configs.size() <= 1);
        if (Button("Delete", icon::Trash, Btn_Danger, ImVec2(bw, 0)))
            S.pendingDelete = S.selConfig;
        ImGui::EndDisabled();
    }
    EndGroup();

    if (BeginGroup("##g_share", icon::Copy, "Share"))
    {
        float bw = (ImGui::GetContentRegionAvail().x - dp(8)) * 0.5f;
        if (Button("Export", icon::Copy, Btn_Ghost, ImVec2(bw, 0)) && S.selConfig >= 0)
        {
            std::string code = "nebula:cfg:" + S.configs[S.selConfig].name;
            ImGui::SetClipboardText(code.c_str());
            Notice(Toast_Success, icon::Copy, "Copied to clipboard", S.configs[S.selConfig].name.c_str());
        }
        ImGui::SameLine(0, dp(8));
        if (Button("Import", icon::Plus, Btn_Ghost, ImVec2(bw, 0)))
        {
            const char* clip = ImGui::GetClipboardText();
            const char* prefix = "nebula:cfg:";
            if (clip && !strncmp(clip, prefix, strlen(prefix)) && clip[strlen(prefix)])
            {
                S.configs.push_back({ std::string(clip + strlen(prefix)) + " (imported)", "Imported from clipboard.", ImGui::GetTime(), ImGui::GetTime() });
                S.selConfig = (int)S.configs.size() - 1;
                Notice(Toast_Success, icon::Plus, "Config", "imported");
            }
            else
                Notify(Toast_Warning, "Nothing to import", "clipboard");
        }
    }
    EndGroup();
    EndColumns();

    // Delete confirmation
    if (S.pendingDelete >= 0)
        ImGui::OpenPopup("##delete");
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(dp(300), 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(dp(16), dp(16)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, RGroup());
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, Hex(0x14151C));
    bool open = ImGui::BeginPopupModal("##delete", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
    if (open)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        float w = ImGui::GetContentRegionAvail().x;
        dl->AddCircleFilled(p + ImVec2(dp(16), dp(16)), dp(16), Col(P.danger, 0.15f));
        DrawIcon(dl, icon::Trash, p + ImVec2(dp(16), dp(16)), dp(13), Col(P.danger));
        TextAt(dl, F.semibold, dp(kTextL), p + ImVec2(dp(44), 0), Col(P.text), "Delete config?");
        const char* name = (S.pendingDelete >= 0 && S.pendingDelete < (int)S.configs.size()) ? S.configs[S.pendingDelete].name.c_str() : "";
        TextFit(dl, F.regular, dp(kTextS), p + ImVec2(dp(44), dp(19)), Col(P.textDim), name, w - dp(44));
        ImGui::Dummy(ImVec2(0, dp(42)));
        float bw = (w - dp(8)) * 0.5f;
        if (Button("Cancel", 0, Btn_Ghost, ImVec2(bw, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            S.pendingDelete = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine(0, dp(8));
        if (Button("Delete", icon::Trash, Btn_Danger, ImVec2(bw, 0)))
        {
            int d = S.pendingDelete;
            if (d >= 0 && d < (int)S.configs.size() && S.configs.size() > 1)
            {
                std::string gone = S.configs[d].name;
                S.configs.erase(S.configs.begin() + d);
                if (S.activeConfig >= d && S.activeConfig > 0)
                    S.activeConfig--;
                S.selConfig = ImMin(S.selConfig, (int)S.configs.size() - 1);
                Notice(Toast_Error, icon::Trash, "Config deleted", gone.c_str());
            }
            S.pendingDelete = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}
}
