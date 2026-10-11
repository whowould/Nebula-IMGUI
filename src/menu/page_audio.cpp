#include "internal.h"
#include "imgui_internal.h"
#include <cmath>
#include <cstring>

using namespace theme;
using namespace ui;

namespace menu
{
void AudioMixer()
{
    static const char* outs[] = { "Speakers", "Headphones", "HDMI" };
    static const char* ins[]  = { "USB mic", "Line in", "Disabled" };
    if (!BeginColumns("##audio_mix"))
        return;
    if (BeginGroup("##g_mixer", icon::Volume, "Mixer"))
    {
        Slider("Master", &S.master, 0, 100, "%.0f%%");
        Slider("Music", &S.music, 0, 100, "%.0f%%");
        Slider("Effects", &S.sfx, 0, 100, "%.0f%%");
        Slider("Voice chat", &S.voice, 0, 100, "%.0f%%");
        Checkbox("Mute when unfocused", &S.muteUnfocused);
    }
    EndGroup();
    NextColumn();
    if (BeginGroup("##g_devices", icon::Headphone, "Devices"))
    {
        if (Combo("Output", &S.outDev, outs, IM_ARRAYSIZE(outs)))
            Log(icon::Headphone, "Output: %s", outs[S.outDev]);
        Combo("Input", &S.inDev, ins, IM_ARRAYSIZE(ins));
        Checkbox("Spatial audio", &S.spatial);
        CheckboxKey("Push to talk", &S.ptt, &S.talkKey);
    }
    EndGroup();
    if (BeginGroup("##g_voice", icon::Mic, "Voice"))
    {
        float t = (float)ImGui::GetTime();
        float level = S.inDev == 2 ? 0.0f : ImSaturate(0.35f + 0.25f * sinf(t * 3.1f) * sinf(t * 1.3f) + 0.18f * sinf(t * 11.0f)) * S.micSens / 70.0f;
        Caption("INPUT LEVEL (PREVIEW)");
        LevelMeter("##meter", level, dp(6));
        ImGui::Dummy(ImVec2(0, dp(2)));
        Slider("Sensitivity", &S.micSens, 0, 100, "%.0f%%");
        Checkbox("Noise suppression", &S.noiseSupp);
    }
    EndGroup();
    EndColumns();
}

void AudioEqualizer()
{
    static const char* eqp[]   = { "Flat", "Bass", "Vocal", "Treble", "Custom" };
    static const char* bands[] = { "32", "64", "125", "250", "500", "1K", "2K", "4K", "8K", "16K" };
    static const float presets[4][10] = {
        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
        { 7, 6, 4, 2, 0, -1, -1, 0, 0, 0 },
        { -2, -1, 0, 2, 4, 4.5f, 3, 1, 0, -1 },
        { 0, 0, 0, 0, 0, 1, 2.5f, 4, 5.5f, 7 },
    };
    if (BeginGroup("##g_eq", icon::Sliders, "Equalizer"))
    {
        if (Pills("##eqp", &S.eqPreset, eqp, IM_ARRAYSIZE(eqp)) && S.eqPreset < 4)
        {
            memcpy(S.eq, presets[S.eqPreset], sizeof(S.eq));
            Log(icon::Music, "EQ preset: %s", eqp[S.eqPreset]);
        }
        ImGui::Dummy(ImVec2(0, dp(4)));
        if (Equalizer("##eq", S.eq, 10, bands, dp(190)))
            S.eqPreset = 4;
    }
    EndGroup();
    if (BeginColumns("##eq_more"))
    {
        if (BeginGroup("##g_loud", icon::Music, "Loudness"))
        {
            Slider("Bass boost", &S.bassBoost, 0, 12, "%.0f dB");
            Checkbox("Loudness equalization", &S.loudness);
        }
        EndGroup();
        NextColumn();
        if (BeginGroup("##g_eqinfo", icon::Info, "Tips"))
        {
            KeyValue("Drag a band", "adjust");
            KeyValue("Double-click", "reset band");
            KeyValue("Up / Down", "fine tune");
        }
        EndGroup();
        EndColumns();
    }
}
}
