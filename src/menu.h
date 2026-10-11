#pragma once

namespace menu
{
void  Init();
void  Render();
float IdleDelay();         // seconds until another frame is needed if no input arrives (0 = keep animating)
void  OnDpiChanged();      // call after theme::SetDpi()
int   ViewCount();         // every page/sub-tab combination (used by --shots)
void  SetView(int index);
}
