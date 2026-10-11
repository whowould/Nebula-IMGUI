#include "internal.h"

using namespace theme;
using namespace ui;

namespace menu
{
void VisualsDisplay()
{
    static const char* modes[]   = { "Fullscreen", "Borderless", "Windowed" };
    static const char* res[]     = { "3840 x 2160", "2560 x 1440", "1920 x 1080", "1600 x 900" };
    static const char* aa[]      = { "Off", "FXAA", "TAA", "MSAA 4x" };
    static const char* ups[]     = { "Native", "Quality", "Balanced", "Performance" };
    static const char* presets[] = { "Low", "Med", "High", "Ultra" };
    if (!BeginColumns("##vis_display"))
        return;
    if (BeginGroup("##g_display", icon::Monitor, "Display"))
    {
        if (Combo("Mode", &S.displayMode, modes, IM_ARRAYSIZE(modes)))
            Log(icon::Monitor, "Display mode: %s", modes[S.displayMode]);
        if (Combo("Resolution", &S.resolution, res, IM_ARRAYSIZE(res)))
            Log(icon::Monitor, "Resolution: %s", res[S.resolution]);
        if (Checkbox("Vertical sync", &S.vsync))
            Log(icon::Monitor, "Vertical sync %s", S.vsync ? "on" : "off");
        Checkbox("HDR output", &S.hdr);
        ImGui::BeginDisabled(S.vsync);
        Slider("Frame cap", &S.frameCap, 30, 360, "%.0f fps");
        ImGui::EndDisabled();
        Slider("Field of view", &S.fov, 60, 120, "%.0f\xC2\xB0");
    }
    EndGroup();
    NextColumn();
    if (BeginGroup("##g_quality", icon::Zap, "Quality"))
    {
        if (Pills("##preset", &S.quality, presets, IM_ARRAYSIZE(presets)))
        {
            static const int   kAA[]    = { 1, 1, 2, 3 };
            static const float kScale[] = { 75, 90, 100, 125 };
            S.aa = kAA[S.quality];
            S.renderScale = kScale[S.quality];
            S.bloom = S.quality > 0;
            S.motionBlur = S.quality == 3;
            Notice(Toast_Info, icon::Zap, "Preset applied", presets[S.quality]);
        }
        ImGui::Dummy(ImVec2(0, dp(1)));
        Combo("Anti-aliasing", &S.aa, aa, IM_ARRAYSIZE(aa));
        Combo("Upscaler", &S.upscaler, ups, IM_ARRAYSIZE(ups));
        Slider("Render scale", &S.renderScale, 50, 200, "%.0f%%");
    }
    EndGroup();
    if (BeginGroup("##g_image", icon::Sun, "Image"))
    {
        Slider("Brightness", &S.brightness, 0, 100, "%.0f%%");
        Slider("Contrast", &S.contrast, 0, 100, "%.0f%%");
        Slider("Gamma", &S.gamma, 1.6f, 2.8f, "%.2f");
    }
    EndGroup();
    EndColumns();
}

void VisualsEffects()
{
    if (!BeginColumns("##vis_fx"))
        return;
    if (BeginGroup("##g_post", icon::Sparkle, "Post-processing", &S.post))
    {
        CheckboxColor("Bloom", &S.bloom, S.bloomCol);
        Checkbox("Motion blur", &S.motionBlur);
        CheckboxColor("Vignette", &S.vignette, S.vignetteCol);
        Checkbox("Film grain", &S.grain);
        Checkbox("Chromatic aberration", &S.chroma);
        if (CheckboxKey("Photo mode", &S.photoMode, &S.photoKey))
            Log(icon::Flag, "Photo mode %s", S.photoMode ? "on" : "off");
    }
    EndGroup();
    NextColumn();
    if (BeginGroup("##g_bloom", icon::Sun, "Bloom", &S.bloom))
    {
        Slider("Intensity", &S.bloomAmt, 0.0f, 1.0f, "%.2f");
        Slider("Threshold", &S.bloomThreshold, 0.0f, 2.0f, "%.2f");
        Slider("Radius", &S.bloomRadius, 1.0f, 12.0f, "%.0f px");
    }
    EndGroup();
    if (BeginGroup("##g_lens", icon::Layers, "Lens"))
    {
        ImGui::BeginDisabled(!S.vignette);
        Slider("Vignette", &S.vignetteAmt, 0, 100, "%.0f%%");
        ImGui::EndDisabled();
        ImGui::BeginDisabled(!S.grain);
        Slider("Grain", &S.grainAmt, 0, 100, "%.0f%%");
        ImGui::EndDisabled();
    }
    EndGroup();
    EndColumns();
}

void VisualsColors()
{
    static const char* grades[] = { "Neutral", "Warm", "Cool", "Vivid" };
    if (!BeginColumns("##vis_colors"))
        return;
    if (BeginGroup("##g_grade", icon::Palette, "Color grading"))
    {
        if (Pills("##grade", &S.grade, grades, IM_ARRAYSIZE(grades)))
        {
            static const float kTemp[] = { 0, 18, -16, 4 }, kSat[] = { 100, 105, 95, 135 };
            S.temperature = kTemp[S.grade];
            S.saturation = kSat[S.grade];
            Log(icon::Palette, "Grade: %s", grades[S.grade]);
        }
        ImGui::Dummy(ImVec2(0, dp(1)));
        Slider("Temperature", &S.temperature, -50, 50, "%+.0f");
        Slider("Tint", &S.tint, -50, 50, "%+.0f");
        Slider("Saturation", &S.saturation, 0, 200, "%.0f%%");
        Slider("Exposure", &S.exposure, -2, 2, "%+.1f EV");
    }
    EndGroup();
    NextColumn();
    if (BeginGroup("##g_tints", icon::Sparkle, "Tints"))
    {
        ColorRow("Ambient", S.ambient);
        ColorRow("Shadows", S.shadows);
        ColorRow("Highlights", S.highlights);
        ColorRow("Interface", S.uiTint);
    }
    EndGroup();
    EndColumns();
}
}
