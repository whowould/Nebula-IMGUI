// Nebula: a compact, styled Dear ImGui menu (Win32 + Direct3D 9).
// Fixed-size borderless window with DWM shadow and Windows 11 rounded corners;
// the header, sidebar and pages are all drawn by Dear ImGui (see src/menu/).
// Frames are rendered on demand: on input, while something animates, and at a slow
// heartbeat otherwise (menu::IdleDelay), so an idle menu costs almost no CPU.
//
// Usage:  nebula.exe                 normal run
//         nebula.exe --shots <dir>   render every page/sub-tab, save <dir>\view_N.bmp, exit
//         nebula.exe --dpi <n>       apply a DPI change at startup (e.g. 144 = 150%), same path as moving monitors

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx9.h"
#include "app.h"
#include "menu.h"
#include "theme.h"

#include <windows.h>
#include <windowsx.h>
#include <objbase.h>
#include <wincodec.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <d3d9.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <vector>

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

// Direct3D 9Ex: still d3d9.dll and an IDirect3DDevice9 for ImGui's DX9 backend, but with the FLIPEX
// swap effect. Plain D3D9 windowed presents are copied into DWM's redirection surface, which cost
// ~5 ms of CPU per frame here; flip-model presents hand the buffer to DWM directly.
static IDirect3D9Ex*          g_d3d = nullptr;
static IDirect3DDevice9Ex*    g_device = nullptr;
static D3DPRESENT_PARAMETERS  g_d3dpp = {};
static bool                   g_deviceRemoved = false;
static bool                   g_occluded = false;
static IDirect3DTexture9*     g_avatarTex = nullptr;
static HANDLE                 g_timer = nullptr; // high-resolution waitable timer for frame pacing
static UINT                   g_resizeW = 0, g_resizeH = 0;
static HWND                   g_hwnd = nullptr;
static bool                   g_ready = false;
static bool                   g_inSizeMove = false;
static float                  g_frameWorkMs = 0.0f;
static float                  g_nextDelay = 0.0f; // from menu::IdleDelay() after the last frame
static const int              kWindowW = 800, kWindowH = 530; // at 100% scale

struct DragRect { float x0, y0, x1, y1; };
static float                 g_captionHeight = 0.0f;
static std::vector<DragRect> g_noDrag, g_noDragBuild;
static bool                  g_itemHovered = false;

static bool CreateDeviceD3D(HWND hwnd);
static void CleanupDeviceD3D();
static void ResetDevice();
static void RecreateDevice();
static LRESULT WINAPI WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

//-----------------------------------------------------------------------------
// app:: hooks used by the menu's custom title bar
//-----------------------------------------------------------------------------

namespace app
{
void Minimize()       { ::ShowWindow(g_hwnd, SW_MINIMIZE); }
void Close()          { ::PostMessageW(g_hwnd, WM_CLOSE, 0, 0); }
void SetCaptionHeight(float px) { g_captionHeight = px; }
void AddNoDragRect(const ImVec2& a, const ImVec2& b) { g_noDragBuild.push_back({ a.x, a.y, b.x, b.y }); }
float FrameWorkMs() { return g_frameWorkMs; }

// Decodes any WIC-supported image, center-crops it to a square and scales it to size x size BGRA
// (the byte order of D3DFMT_A8R8G8B8).
static bool DecodeSquare(const std::wstring& path, UINT size, std::vector<BYTE>& out)
{
    IWICImagingFactory* factory = nullptr;
    if (FAILED(::CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))))
        return false;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICBitmapClipper* clipper = nullptr;
    IWICBitmapScaler* scaler = nullptr;
    IWICFormatConverter* converter = nullptr;
    UINT w = 0, h = 0;
    bool ok = SUCCEEDED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder))
           && SUCCEEDED(decoder->GetFrame(0, &frame))
           && SUCCEEDED(frame->GetSize(&w, &h)) && w > 0 && h > 0;
    if (ok)
    {
        UINT side = w < h ? w : h;
        WICRect rc = { (INT)((w - side) / 2), (INT)((h - side) / 2), (INT)side, (INT)side };
        ok = SUCCEEDED(factory->CreateBitmapClipper(&clipper)) && SUCCEEDED(clipper->Initialize(frame, &rc))
          && SUCCEEDED(factory->CreateBitmapScaler(&scaler)) && SUCCEEDED(scaler->Initialize(clipper, size, size, WICBitmapInterpolationModeFant))
          && SUCCEEDED(factory->CreateFormatConverter(&converter))
          && SUCCEEDED(converter->Initialize(scaler, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom));
    }
    if (ok)
    {
        out.resize((size_t)size * size * 4);
        ok = SUCCEEDED(converter->CopyPixels(nullptr, size * 4, (UINT)out.size(), out.data()));
    }
    if (converter) converter->Release();
    if (scaler) scaler->Release();
    if (clipper) clipper->Release();
    if (frame) frame->Release();
    if (decoder) decoder->Release();
    factory->Release();
    return ok;
}

ImTextureID LoadAvatar(int size)
{
    wchar_t exe[MAX_PATH] = {};
    ::GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir(exe);
    dir = dir.substr(0, dir.find_last_of(L"\\/") + 1);
    for (const wchar_t* name : { L"avatar.png", L"avatar.jpg", L"avatar.jpeg" })
    {
        std::vector<BYTE> px;
        if (!DecodeSquare(dir + name, (UINT)size, px))
            continue;
        // D3D9Ex has no managed pool; a dynamic default-pool texture survives ResetEx on Ex devices.
        IDirect3DTexture9* tex = nullptr;
        if (FAILED(g_device->CreateTexture((UINT)size, (UINT)size, 1, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &tex, nullptr)))
            continue;
        D3DLOCKED_RECT lr;
        if (FAILED(tex->LockRect(0, &lr, nullptr, D3DLOCK_DISCARD)))
        {
            tex->Release();
            continue;
        }
        for (int y = 0; y < size; y++)
            memcpy((BYTE*)lr.pBits + y * lr.Pitch, px.data() + (size_t)y * size * 4, (size_t)size * 4);
        tex->UnlockRect(0);
        if (g_avatarTex)
            g_avatarTex->Release();
        g_avatarTex = tex;
        return (ImTextureID)(intptr_t)g_avatarTex;
    }
    return (ImTextureID)0;
}
}

//-----------------------------------------------------------------------------
// Frame
//-----------------------------------------------------------------------------

static bool SaveBackbufferBMP(const std::wstring& path)
{
    IDirect3DSurface9* back = nullptr;
    if (FAILED(g_device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back)))
        return false;
    D3DSURFACE_DESC desc;
    back->GetDesc(&desc);
    IDirect3DSurface9* sys = nullptr;
    bool ok = (desc.Format == D3DFMT_X8R8G8B8 || desc.Format == D3DFMT_A8R8G8B8)
           && SUCCEEDED(g_device->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format, D3DPOOL_SYSTEMMEM, &sys, nullptr))
           && SUCCEEDED(g_device->GetRenderTargetData(back, sys));
    D3DLOCKED_RECT lr;
    if (ok && SUCCEEDED(sys->LockRect(&lr, nullptr, D3DLOCK_READONLY)))
    {
        FILE* f = _wfopen(path.c_str(), L"wb");
        ok = f != nullptr;
        if (f)
        {
            BITMAPINFOHEADER ih = {};
            ih.biSize = sizeof(ih);
            ih.biWidth = (LONG)desc.Width;
            ih.biHeight = -(LONG)desc.Height; // top-down
            ih.biPlanes = 1;
            ih.biBitCount = 32;
            ih.biCompression = BI_RGB;
            BITMAPFILEHEADER fh = {};
            fh.bfType = 0x4D42;
            fh.bfOffBits = sizeof(fh) + sizeof(ih);
            fh.bfSize = fh.bfOffBits + desc.Width * desc.Height * 4;
            fwrite(&fh, sizeof(fh), 1, f);
            fwrite(&ih, sizeof(ih), 1, f);
            std::vector<unsigned char> row(desc.Width * 4);
            for (UINT y = 0; y < desc.Height; y++)
            {
                memcpy(row.data(), (const unsigned char*)lr.pBits + y * lr.Pitch, row.size()); // already BGRA
                for (UINT x = 0; x < desc.Width; x++)
                    row[x * 4 + 3] = 255;
                fwrite(row.data(), row.size(), 1, f);
            }
            fclose(f);
        }
        sys->UnlockRect();
    }
    if (sys) sys->Release();
    back->Release();
    return ok;
}

static double Seconds()
{
    static LARGE_INTEGER freq = [] { LARGE_INTEGER f; ::QueryPerformanceFrequency(&f); return f; }();
    LARGE_INTEGER c;
    ::QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)freq.QuadPart;
}

// Next display refresh after `after`, from DWM's composition clock (QPC based, like Seconds()).
static double NextVBlank(double after)
{
    static const double freq = [] { LARGE_INTEGER f; ::QueryPerformanceFrequency(&f); return (double)f.QuadPart; }();
    DWM_TIMING_INFO ti = {};
    ti.cbSize = sizeof(ti);
    if (SUCCEEDED(::DwmGetCompositionTimingInfo(nullptr, &ti)) && ti.qpcRefreshPeriod > 0)
    {
        const double period = (double)ti.qpcRefreshPeriod / freq, vblank = (double)ti.qpcVBlank / freq;
        double next = vblank + period * ceil((after - vblank) / period);
        return next > after + 0.0005 ? next : next + period;
    }
    return after + 1.0 / 60.0;
}

// Sleeps until `t` (seconds, Seconds() clock) or until any input arrives, whichever comes first.
static void WaitUntil(double t)
{
    const double wait = t - Seconds();
    if (wait <= 0.0)
        return;
    LARGE_INTEGER due;
    due.QuadPart = -(LONGLONG)(wait * 1e7); // relative, 100 ns units
    if (g_timer && ::SetWaitableTimer(g_timer, &due, 0, nullptr, nullptr, FALSE))
        ::MsgWaitForMultipleObjects(1, &g_timer, FALSE, INFINITE, QS_ALLINPUT);
    else
        ::MsgWaitForMultipleObjects(0, nullptr, FALSE, (DWORD)(wait * 1000.0) + 1, QS_ALLINPUT);
}

static void RenderFrame(const std::wstring* capture_path = nullptr)
{
    const double t0 = Seconds();
    if (g_resizeW != 0 && g_resizeH != 0)
    {
        g_d3dpp.BackBufferWidth = g_resizeW;
        g_d3dpp.BackBufferHeight = g_resizeH;
        g_resizeW = g_resizeH = 0;
        ResetDevice();
    }

    ImGui_ImplDX9_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    g_noDragBuild.clear();
    menu::Render();
    g_noDrag.swap(g_noDragBuild);
    g_itemHovered = ImGui::IsAnyItemHovered();
    ImGui::EndFrame();

    const ImVec4& bg = theme::P.bg;
    g_device->SetRenderState(D3DRS_ZENABLE, FALSE);
    g_device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    g_device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    g_device->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_RGBA((int)(bg.x * 255.0f), (int)(bg.y * 255.0f), (int)(bg.z * 255.0f), 255), 1.0f, 0);
    if (g_device->BeginScene() >= 0)
    {
        ImGui::Render();
        ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
        g_device->EndScene();
    }
    g_frameWorkMs = (float)((Seconds() - t0) * 1000.0);
    if (capture_path)
        SaveBackbufferBMP(*capture_path);

    g_nextDelay = menu::IdleDelay();
    // Presents immediately: the main loop already waits for the next vblank on a timer, so the
    // driver never blocks (or spins) here. Flip-model windowed presents are composed by DWM: no tearing.
    HRESULT hr = g_device->PresentEx(nullptr, nullptr, nullptr, nullptr, 0);
    if (hr == S_PRESENT_OCCLUDED)
        g_occluded = true;
    else if (hr == D3DERR_DEVICEREMOVED || hr == D3DERR_DEVICEHUNG)
        g_deviceRemoved = true;
}

//-----------------------------------------------------------------------------
// Entry point
//-----------------------------------------------------------------------------

int WINAPI WinMain(HINSTANCE hinst, HINSTANCE, LPSTR, int)
{
    // --shots <dir>, --dpi <n>
    std::wstring shots_dir;
    int dpi_override = 0;
    {
        int argc = 0;
        LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
        for (int i = 1; argv && i < argc; i++)
        {
            if (wcscmp(argv[i], L"--shots") == 0 && i + 1 < argc)
                shots_dir = argv[++i];
            else if (wcscmp(argv[i], L"--dpi") == 0 && i + 1 < argc)
                dpi_override = _wtoi(argv[++i]);
        }
        ::LocalFree(argv);
    }

    ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); // WIC (avatar decoding)
    ImGui_ImplWin32_EnableDpiAwareness();
    HMONITOR monitor = ::MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
    float scale = ImGui_ImplWin32_GetDpiScaleForMonitor(monitor);

    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, hinst, ::LoadIcon(nullptr, IDI_APPLICATION),
                       ::LoadCursor(nullptr, IDC_ARROW), nullptr, nullptr, L"NebulaMenu", nullptr };
    ::RegisterClassExW(&wc);

    MONITORINFO mi = { sizeof(mi) };
    ::GetMonitorInfoW(monitor, &mi);
    int work_w = mi.rcWork.right - mi.rcWork.left, work_h = mi.rcWork.bottom - mi.rcWork.top;
    int w = (int)(kWindowW * scale), h = (int)(kWindowH * scale);
    int x = mi.rcWork.left + (work_w - w) / 2, y = mi.rcWork.top + (work_h - h) / 2;

    // WS_THICKFRAME/WS_CAPTION are kept only so DWM draws the shadow and rounded corners;
    // the hit test below never reports resize edges, so the menu stays a fixed size.
    g_hwnd = ::CreateWindowExW(0, wc.lpszClassName, L"Nebula", WS_POPUP | WS_THICKFRAME | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                               x, y, w, h, nullptr, nullptr, hinst, nullptr);

    // Borderless but keep DWM shadow and Win11 rounded corners.
    MARGINS margins = { 1, 1, 1, 1 };
    ::DwmExtendFrameIntoClientArea(g_hwnd, &margins);
    BOOL dark = TRUE;
    ::DwmSetWindowAttribute(g_hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
    int corners = 2; /* DWMWCP_ROUND */
    ::DwmSetWindowAttribute(g_hwnd, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &corners, sizeof(corners));
    COLORREF border = RGB(0x2A, 0x2F, 0x40);
    ::DwmSetWindowAttribute(g_hwnd, 34 /* DWMWA_BORDER_COLOR */, &border, sizeof(border));
    ::SetWindowPos(g_hwnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);

    if (!CreateDeviceD3D(g_hwnd))
    {
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    theme::dpi = scale;
    theme::LoadFonts();
    theme::Setup();

    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplDX9_Init(g_device);
    menu::Init();
    g_ready = true;

    ::ShowWindow(g_hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(g_hwnd);

    // Same code path Windows uses when the window moves to a monitor with another scale.
    if (dpi_override > 0)
    {
        RECT rc;
        ::GetWindowRect(g_hwnd, &rc);
        ::SendMessageW(g_hwnd, WM_DPICHANGED, MAKEWPARAM(dpi_override, dpi_override), (LPARAM)&rc);
    }

    // Frame scheduling. At most one frame per display refresh (`earliest` is the next vblank), and
    // only when needed: right after input (`eager`), while something animates (IdleDelay() == 0),
    // or at the idle heartbeat. Waits sleep on a high-resolution timer, so nothing spins.
    g_timer = ::CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!g_timer)
        g_timer = ::CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS); // pre-1803 Windows 10
    const bool on_demand = shots_dir.empty();
    const int frames_per_shot = 50;
    int frame = 0;
    int eager = 2;          // frames to render right after input, so hover states settle
    double earliest = 0.0, scheduled = 0.0;
    bool done = false;
    while (!done)
    {
        WaitUntil(eager > 0 ? earliest : (earliest > scheduled ? earliest : scheduled));
        MSG msg;
        bool had_input = false;
        while (::PeekMessageW(&msg, nullptr, 0U, 0U, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
            had_input = true;
            if (msg.message == WM_QUIT)
                done = true;
        }
        if (done)
            break;
        if (had_input)
            eager = 3;
        const double due = eager > 0 ? earliest : (earliest > scheduled ? earliest : scheduled);
        if (Seconds() < due)
            continue; // input arrived before the next refresh: it is handled by that frame

        if (g_deviceRemoved) // driver update / TDR: rebuild the device and everything on it
        {
            RecreateDevice();
            continue;
        }
        if (g_occluded || (on_demand && ::IsIconic(g_hwnd))) // minimized or fully covered: nothing to draw
        {
            if (g_device->CheckDeviceState(g_hwnd) == S_PRESENT_OCCLUDED || ::IsIconic(g_hwnd))
            {
                ::MsgWaitForMultipleObjects(0, nullptr, FALSE, 100, QS_ALLINPUT);
                continue;
            }
            g_occluded = false;
        }

        std::wstring shot;
        if (!on_demand) // screenshot mode: let each view settle, then grab the back buffer
        {
            int view = frame / frames_per_shot;
            if (view >= menu::ViewCount())
                break;
            if (frame % frames_per_shot == 0)
                menu::SetView(view);
            if (frame % frames_per_shot == frames_per_shot - 1)
                shot = shots_dir + L"\\view_" + std::to_wstring(view) + L".bmp";
            frame++;
        }
        RenderFrame(shot.empty() ? nullptr : &shot);
        if (eager > 0)
            eager--;
        const double now = Seconds();
        earliest = NextVBlank(now);
        scheduled = (!on_demand || g_nextDelay <= 0.0f) ? earliest : now + g_nextDelay;
    }
    if (g_timer)
        ::CloseHandle(g_timer);

    ImGui_ImplDX9_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    ::DestroyWindow(g_hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
    ::CoUninitialize();
    return 0;
}

//-----------------------------------------------------------------------------
// D3D9
//-----------------------------------------------------------------------------

static bool CreateDeviceD3D(HWND hwnd)
{
    if (FAILED(::Direct3DCreate9Ex(D3D_SDK_VERSION, &g_d3d)))
        return false;
    RECT rc;
    ::GetClientRect(hwnd, &rc);
    ZeroMemory(&g_d3dpp, sizeof(g_d3dpp));
    g_d3dpp.Windowed = TRUE;
    g_d3dpp.SwapEffect = D3DSWAPEFFECT_FLIPEX;                    // flip model (no redirection copy)
    g_d3dpp.BackBufferCount = 2;
    g_d3dpp.BackBufferFormat = D3DFMT_X8R8G8B8;
    g_d3dpp.BackBufferWidth = (UINT)(rc.right - rc.left);
    g_d3dpp.BackBufferHeight = (UINT)(rc.bottom - rc.top);
    g_d3dpp.EnableAutoDepthStencil = FALSE;                       // 2D UI only
    g_d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE; // pacing is done by the main loop
    // FPU_PRESERVE: by default D3D9 switches the FPU to single precision, which breaks double timers.
    const DWORD flags = D3DCREATE_FPU_PRESERVE;
    HRESULT hr = g_d3d->CreateDeviceEx(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd, flags | D3DCREATE_HARDWARE_VERTEXPROCESSING, &g_d3dpp, nullptr, &g_device);
    if (FAILED(hr))
        hr = g_d3d->CreateDeviceEx(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd, flags | D3DCREATE_SOFTWARE_VERTEXPROCESSING, &g_d3dpp, nullptr, &g_device);
    if (FAILED(hr))
        return false;
    g_device->SetMaximumFrameLatency(1);
    return true;
}

static void CleanupDeviceD3D()
{
    if (g_avatarTex) { g_avatarTex->Release(); g_avatarTex = nullptr; }
    if (g_device)    { g_device->Release();    g_device = nullptr; }
    if (g_d3d)       { g_d3d->Release();       g_d3d = nullptr; }
}

// Back buffer size changes go through ResetEx(). Ex devices keep default-pool resources across it,
// but the ImGui backend's buffers are dropped and rebuilt anyway to stay on the documented path.
static void ResetDevice()
{
    ImGui_ImplDX9_InvalidateDeviceObjects();
    HRESULT hr = g_device->ResetEx(&g_d3dpp, nullptr);
    if (hr == D3DERR_DEVICEREMOVED || hr == D3DERR_DEVICEHUNG)
        g_deviceRemoved = true;
    IM_ASSERT(hr != D3DERR_INVALIDCALL);
    ImGui_ImplDX9_CreateDeviceObjects();
}

// A removed or hung device can't be reset: release it and build a new one (backend, fonts, avatar).
static void RecreateDevice()
{
    ImGui_ImplDX9_Shutdown();
    CleanupDeviceD3D();
    g_deviceRemoved = false;
    if (!CreateDeviceD3D(g_hwnd))
    {
        ::PostQuitMessage(1);
        return;
    }
    ImGui_ImplDX9_Init(g_device);
    menu::OnDpiChanged(); // reloads the avatar texture
}

//-----------------------------------------------------------------------------
// Window procedure
//-----------------------------------------------------------------------------

static LRESULT HitTest(HWND hwnd, POINT screen)
{
    POINT pt = screen;
    ::ScreenToClient(hwnd, &pt);
    if (pt.y < g_captionHeight && !g_itemHovered)
    {
        for (const DragRect& d : g_noDrag)
            if (pt.x >= d.x0 && pt.x < d.x1 && pt.y >= d.y0 && pt.y < d.y1)
                return HTCLIENT;
        return HTCAPTION;
    }
    return HTCLIENT;
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static LRESULT WINAPI WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_NCCALCSIZE:
        if (wParam == TRUE)
            return 0; // client area = whole window (no standard frame)
        break;
    case WM_NCHITTEST:
        return HitTest(hwnd, POINT{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) });
    case WM_NCACTIVATE:
        return ::DefWindowProcW(hwnd, msg, wParam, -1); // don't repaint a non-client frame we don't have
    case WM_ENTERSIZEMOVE:
        g_inSizeMove = true;
        return 0;
    case WM_EXITSIZEMOVE:
        g_inSizeMove = false;
        return 0;
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED)
            return 0;
        g_resizeW = (UINT)LOWORD(lParam);
        g_resizeH = (UINT)HIWORD(lParam);
        if (g_ready && g_inSizeMove)
            RenderFrame(); // keep drawing during the modal resize loop
        return 0;
    case WM_DPICHANGED:
    {
        // Moved to a monitor with a different scale: resize to the exact scaled size and
        // rebuild every DPI-dependent value (style sizes, font scale, avatar texture).
        const float scale = HIWORD(wParam) / 96.0f;
        const RECT* r = reinterpret_cast<RECT*>(lParam);
        ::SetWindowPos(hwnd, nullptr, r->left, r->top, (int)(kWindowW * scale), (int)(kWindowH * scale), SWP_NOZORDER | SWP_NOACTIVATE);
        if (g_ready)
        {
            theme::SetDpi(scale);
            menu::OnDpiChanged();
        }
        return 0;
    }
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
            return 0;
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hwnd, msg, wParam, lParam);
}
