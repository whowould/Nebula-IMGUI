#include "internal.h"
#include <cstring>

using namespace theme;
using namespace ui;

namespace menu
{
void ControlsMouse()
{
    static const char* layouts[] = { "Default", "Southpaw", "Legacy" };
    if (!BeginColumns("##ctl_mouse"))
        return;
    if (BeginGroup("##g_mouse", icon::Mouse, "Mouse"))
    {
        Slider("Sensitivity", &S.sens, 0.1f, 5.0f, "%.2f");
        Slider("ADS multiplier", &S.ads, 0.1f, 2.0f, "%.2fx");
        Checkbox("Raw input", &S.rawInput);
        Checkbox("Invert Y axis", &S.invertY);
        Checkbox("Acceleration", &S.accel);
    }
    EndGroup();
    NextColumn();
    if (BeginGroup("##g_pad", icon::Game, "Controller", &S.vibration))
    {
        Caption("STICK LAYOUT");
        Pills("##layout", &S.padLayout, layouts, IM_ARRAYSIZE(layouts));
        ImGui::Dummy(ImVec2(0, dp(1)));
        Slider("Deadzone", &S.deadzone, 0, 30, "%.0f%%");
        Slider("Trigger threshold", &S.trigger, 0, 50, "%.0f%%");
    }
    EndGroup();
    EndColumns();
}

void ControlsKeybinds()
{
    if (!BeginColumns("##ctl_keys"))
        return;
    auto binds = [](int from, int to) {
        for (int i = from; i < to; i++)
            if (Keybind(kBindNames[i], &S.binds[i]))
                Log(icon::Keyboard, "%s bound to %s", kBindNames[i], KeyLabel(S.binds[i]));
    };
    if (BeginGroup("##g_move", icon::Game, "Movement"))
        binds(0, kMovementBinds);
    EndGroup();
    NextColumn();
    if (BeginGroup("##g_actions", icon::Keyboard, "Actions"))
    {
        binds(kMovementBinds, kBindCount);
        ImGui::Dummy(ImVec2(0, dp(2)));
        if (Button("Restore defaults", icon::Refresh, Btn_Ghost, ImVec2(-1, 0)))
        {
            memcpy(S.binds, kBindDefaults, sizeof(S.binds));
            Notice(Toast_Info, icon::Refresh, "Keybinds", "restored");
        }
    }
    EndGroup();
    EndColumns();
}
}
