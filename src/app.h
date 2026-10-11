#pragma once
#include "imgui.h"

// Platform hooks implemented in main.cpp. The menu draws its own title bar,
// so it tells the window which areas are draggable and drives minimize/close.
namespace app
{
void Minimize();
void Close();
void SetCaptionHeight(float px);                          // drag region, from the top of the client area
void AddNoDragRect(const ImVec2& min, const ImVec2& max);  // interactive rect inside the caption (rebuilt every frame)

// avatar.png / avatar.jpg next to the exe, center-cropped and scaled to `size` px. 0 if missing.
ImTextureID LoadAvatar(int size);

// CPU time the previous frame spent building and submitting the UI (excludes the vsync wait).
float FrameWorkMs();
}
