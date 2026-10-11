#include "internal.h"
#include <cstdio>

using namespace theme;
using namespace ui;

namespace menu
{
void SettingsAppearance()
{
    static const char* langs[] = { "English", "Deutsch", "Espa\xC3\xB1ol", "Fran\xC3\xA7" "ais", "Portugu\xC3\xAAs", "Polski" };
    if (!BeginColumns("##set_look"))
        return;
    if (BeginGroup("##g_theme", icon::Palette, "Theme"))
    {
        Caption("ACCENT");
        if (AccentPicker())
            Notice(Toast_Info, icon::Palette, "Accent", kAccents[accent].name);
        ImGui::Dummy(ImVec2(0, dp(2)));
        Slider("Corner radius", &rounding, 0, 16, "%.0f px");
        Checkbox("Animations", &animations);
        Checkbox("Glow effects", &glow);
        Checkbox("Compact sidebar", &S.compact);
    }
    EndGroup();
    NextColumn();
    if (BeginGroup("##g_general", icon::Sliders, "General"))
    {
        if (Combo("Language", &S.language, langs, IM_ARRAYSIZE(langs)))
            Log(icon::Globe, "Language: %s", langs[S.language]);
        Checkbox("Launch at startup", &S.startup);
        Checkbox("Minimize to tray", &S.tray);
        Checkbox("Send crash reports", &S.crash);
        Checkbox("Automatic updates", &S.updates);
    }
    EndGroup();
    EndColumns();
}

void SettingsAbout()
{
    if (!BeginColumns("##set_about"))
        return;
    if (BeginGroup("##g_about", icon::Info, "About"))
    {
        char buf[48], up[16];
        KeyValue("Version", "2.4.0", true);
        KeyValue("Build", __DATE__, true);
        KeyValue("Dear ImGui", IMGUI_VERSION, true);
        snprintf(buf, sizeof(buf), "%.0f%%", dpi * 100.0f);
        KeyValue("Display scale", buf, true);
        FormatUptime(up, sizeof(up));
        KeyValue("Uptime", up, true);
        ImGui::Dummy(ImVec2(0, dp(4)));
        float bw = (ImGui::GetContentRegionAvail().x - dp(8)) * 0.5f;
        if (Button(S.showDemo ? "Hide demo" : "ImGui demo", icon::Code, Btn_Primary, ImVec2(bw, 0)))
            S.showDemo = !S.showDemo;
        ImGui::SameLine(0, dp(8));
        if (Button("Updates", icon::Sync, Btn_Ghost, ImVec2(bw, 0)))
            Notify(Toast_Info, "Updater not wired up", "stub");
    }
    EndGroup();
    NextColumn();
    if (BeginGroup("##g_keys", icon::Keyboard, "Shortcuts"))
    {
        KeyValue("Search", "Ctrl K", true);
        KeyValue("Switch page", "Ctrl 1-6", true);
        KeyValue("Navigate", "Arrows / Tab", true);
        KeyValue("Close popup", "Esc", true);
        KeyValue("Clear keybind", "Right click", true);
    }
    EndGroup();
    if (BeginGroup("##g_reset", icon::Warning, "Reset"))
    {
        if (Button("Reset all settings", icon::Refresh, Btn_Danger, ImVec2(-1, 0)))
        {
            std::vector<Config> keep = S.configs;
            int active = S.activeConfig;
            S = State();
            S.page = Page_Settings;
            S.sub[Page_Settings] = 1;
            S.configs = keep;
            S.activeConfig = active;
            rounding = 10.0f;
            animations = true;
            glow = true;
            SetAccent(0);
            Notice(Toast_Warning, icon::Refresh, "Settings reset", "configs kept");
        }
    }
    EndGroup();
    EndColumns();
}
}
