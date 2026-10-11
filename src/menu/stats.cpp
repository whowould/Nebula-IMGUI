#include "internal.h"
#include "imgui_internal.h"

#include <windows.h>
#include <psapi.h>
#include <cstdarg>
#include <cstdio>

namespace menu
{
namespace stats
{
Live L;

static ULONGLONG FtToU64(FILETIME f) { return ((ULONGLONG)f.dwHighDateTime << 32) | f.dwLowDateTime; }

static float SampleCpuOneCore()
{
    static ULONGLONG lastProc = 0, lastWall = 0;
    FILETIME c, e, k, u, now;
    GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u);
    GetSystemTimeAsFileTime(&now);
    ULONGLONG proc = FtToU64(k) + FtToU64(u), wall = FtToU64(now);
    float pct = 0.0f;
    if (lastWall && wall > lastWall)
        pct = (float)(proc - lastProc) / (float)(wall - lastWall) * 100.0f;
    lastProc = proc;
    lastWall = wall;
    return ImClamp(pct, 0.0f, 100.0f);
}

static float SampleMemMB()
{
    PROCESS_MEMORY_COUNTERS pmc = {};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
        return (float)pmc.WorkingSetSize / (1024.0f * 1024.0f);
    return 0.0f;
}

void Init() { L.startTime = ImGui::GetTime(); }

// frame_work_ms: CPU time the previous frame spent building and submitting the UI (excludes vsync wait).
void Update(float frame_work_ms)
{
    static int frames = 0;
    static double last_sample = -1.0;
    frames++;
    if (frames < 10) // first frames include font baking and device warm-up
        return;
    if (frames < 30)
        for (float& f : L.renderMs) f = frame_work_ms;
    L.renderMs[L.renderOff] = frame_work_ms;
    L.renderOff = (L.renderOff + 1) % kHist;

    double now = ImGui::GetTime();
    if (last_sample >= 0.0 && now - last_sample < 0.25)
        return;
    bool first = last_sample < 0.0;
    last_sample = now;
    float sum = 0.0f;
    for (float f : L.renderMs) sum += f;
    L.renderAvg = sum / kHist;
    L.mem = SampleMemMB();
    L.cpu = SampleCpuOneCore();
    if (first)
        for (int i = 0; i < kSpark; i++) { L.renderSpark[i] = L.renderAvg; L.memSpark[i] = L.mem; L.cpuSpark[i] = L.cpu; }
    L.renderSpark[L.sparkOff] = L.renderAvg;
    L.memSpark[L.sparkOff] = L.mem;
    L.cpuSpark[L.sparkOff] = L.cpu;
    L.sparkOff = (L.sparkOff + 1) % kSpark;
}

void Range(const float* v, int n, float* lo, float* hi)
{
    float a = FLT_MAX, b = -FLT_MAX;
    for (int i = 0; i < n; i++) { a = ImMin(a, v[i]); b = ImMax(b, v[i]); }
    float pad = ImMax((b - a) * 0.25f, ImMax(fabsf(b) * 0.05f, 0.05f));
    *lo = a - pad;
    *hi = b + pad;
}
}

//-----------------------------------------------------------------------------
// Activity log
//-----------------------------------------------------------------------------

static std::vector<Activity> s_activity;

const std::vector<Activity>& Activities() { return s_activity; }

void Log(unsigned ic, const char* fmt, ...)
{
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    s_activity.insert(s_activity.begin(), { ImGui::GetTime(), ic, buf });
    if (s_activity.size() > 10)
        s_activity.pop_back();
}

void Ago(double t, char* out, size_t n)
{
    double age = ImGui::GetTime() - t;
    if (age < 10.0)         snprintf(out, n, "now");
    else if (age < 60.0)    snprintf(out, n, "%ds", (int)age);
    else if (age < 3600.0)  snprintf(out, n, "%dm", (int)(age / 60.0));
    else if (age < 86400.0) snprintf(out, n, "%dh", (int)(age / 3600.0));
    else                    snprintf(out, n, "%dd", (int)(age / 86400.0));
}

void FormatUptime(char* out, size_t n)
{
    int s = (int)(ImGui::GetTime() - stats::L.startTime);
    snprintf(out, n, "%02d:%02d:%02d", s / 3600, (s / 60) % 60, s % 60);
}
}
