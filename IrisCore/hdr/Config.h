#pragma once
#include <windows.h>
#include <conio.h>
#include <cstdio>
#include <cmath>
#include <string>
#include <algorithm>

// ── Palette ───────────────────────────────────────────────────────────────────
#define CC_RST    "\033[0m"
#define CC_BORDER "\033[38;5;189m"
#define CC_ACCENT "\033[38;5;219m"
#define CC_DIM    "\033[38;5;103m"
#define CC_WHITE  "\033[38;5;255m"
#define CC_OK     "\033[38;5;147m"
#define CC_INFO   "\033[38;5;153m"
#define CC_ERR    "\033[38;5;210m"
#define CC_BOLD   "\033[1m"

#define CONFIG_PATH "C:\\Windows\\Temp\\wdiag_prefs.dat"

// CONFIG_LINES: exact number of lines RenderConfig prints — keep in sync
static const int CONFIG_LINES = 28;

// ── ROI presets ───────────────────────────────────────────────────────────────
static const int kROISizes[] = { 240, 320, 480, 640, 800, 960, 1080, 1280, 1440, 1600 };
static const char* kROINames[] = { "240", "320", "480", "640", "800", "960", "1080", "1280", "1440", "1600" };
static const int kROICount = 10;

// ── Config struct ─────────────────────────────────────────────────────────────
struct IrisConfig {
    float confidence = 0.65f;
    int   priority   = 1;       // 0=Body  1=Head  2=Body+Head
    bool  show_boxes = false;
    int   roi_idx    = 5;       // index into kROISizes (default 960)

    bool  autofire         = true;
    int   autofire_initial = 2;  // delay before first shot: tenths of seconds (1-50 = 0.1s–5.0s)
    int   autofire_delay   = 2;  // delay between shots:     tenths of seconds (1-50 = 0.1s–5.0s)

    int   overshooting = 70;    // pixels_per_degree factor * 10 (10-200)

    float fov_h       = 103.0f;
    float fov_v       =  70.0f;
    float sensitivity =   1.0f;
    int   dpi         =   800;
    float smoothing   =  0.0f; // 0 = Off
    float deadzone    =  0.0f; // 0 = Off
    float max_speed   = 1600.0f;
};

// ── Persistence ───────────────────────────────────────────────────────────────
static void SaveConfig(const IrisConfig& c) {
    FILE* f = nullptr;
    fopen_s(&f, CONFIG_PATH, "w");
    if (!f) return;
    fprintf(f, "confidence=%.3f\n",  c.confidence);
    fprintf(f, "priority=%d\n",      c.priority);
    fprintf(f, "show_boxes=%d\n",    c.show_boxes ? 1 : 0);
    fprintf(f, "roi_idx=%d\n",       c.roi_idx);
    fprintf(f, "autofire=%d\n",         c.autofire ? 1 : 0);
    fprintf(f, "autofire_initial=%d\n", c.autofire_initial);
    fprintf(f, "autofire_delay=%d\n",   c.autofire_delay);
    fprintf(f, "overshooting=%d\n",    c.overshooting);
    fprintf(f, "fov_h=%.2f\n",       c.fov_h);
    fprintf(f, "fov_v=%.2f\n",       c.fov_v);
    fprintf(f, "sensitivity=%.3f\n", c.sensitivity);
    fprintf(f, "dpi=%d\n",           c.dpi);
    fprintf(f, "smoothing=%.3f\n",   c.smoothing);
    fprintf(f, "deadzone=%.2f\n",    c.deadzone);
    fprintf(f, "max_speed=%.1f\n",   c.max_speed);
    fclose(f);
}

static void LoadConfig(IrisConfig& c) {
    FILE* f = nullptr;
    fopen_s(&f, CONFIG_PATH, "r");
    if (!f) return;
    char key[64]; float val;
    while (fscanf_s(f, "%63[^=]=%f\n", key, (unsigned)sizeof(key), &val) == 2) {
        std::string k(key);
        if      (k == "confidence")  c.confidence  = val;
        else if (k == "priority")    c.priority    = std::clamp((int)val, 0, 2);
        else if (k == "show_boxes")  c.show_boxes  = (int)val != 0;
        else if (k == "roi_idx")     c.roi_idx     = std::clamp((int)val, 0, kROICount - 1);
        else if (k == "autofire")         c.autofire         = (int)val != 0;
        else if (k == "autofire_initial") c.autofire_initial = std::clamp((int)val, 1, 50);
        else if (k == "autofire_delay")   c.autofire_delay   = std::clamp((int)val, 1, 50);
        else if (k == "overshooting") c.overshooting = std::clamp((int)val, 10, 200);
        else if (k == "fov_h")       c.fov_h       = val;
        else if (k == "fov_v")       c.fov_v       = val;
        else if (k == "sensitivity") c.sensitivity = val;
        else if (k == "dpi")         c.dpi         = (int)val;
        else if (k == "smoothing")   c.smoothing   = val;
        else if (k == "deadzone")    c.deadzone    = val;
        else if (k == "max_speed")   c.max_speed   = val;
    }
    fclose(f);
}

// ── Row definitions ───────────────────────────────────────────────────────────
enum RowId {
    ROW_CONFIDENCE = 0,
    ROW_PRIORITY,
    ROW_SHOW_BOXES,
    ROW_ROI,
    ROW_AUTOFIRE,
    ROW_AUTOFIRE_INITIAL,  // AutoFire Delay — espera antes do 1° tiro
    ROW_AUTOFIRE_FRAMES,   // Shoot Delay   — intervalo entre tiros
    ROW_OVERSHOOTING,
    ROW_FOV_H,
    ROW_FOV_V,
    ROW_SENSITIVITY,
    ROW_DPI,
    ROW_SMOOTHING,
    ROW_DEADZONE,
    ROW_MAX_SPEED,
    ROW_COUNT  // 11
};

struct Row {
    const char* label;
    const char* unit;
    float min, max, step;
    bool  is_int;
    int   num_opts; // 0=continuous, >0=enum cycling
};

static const Row kRows[ROW_COUNT] = {
    { "Confidence",    "",       0.10f, 1.00f, 0.05f, false, 0 },
    { "Priority",      "",       0.00f, 2.00f, 1.00f, true,  3 },
    { "Box Overlay",   "",       0.00f, 1.00f, 1.00f, true,  2 },
    { "ROI Size",      "px",     0.00f, 5.00f, 1.00f, true,  kROICount },
    { "Autofire",      "",       0.00f, 1.00f, 1.00f, true,  2 },
    { "AutoFire Delay","s",      1.00f,50.00f, 1.00f, true,  0 },
    { "Shoot Delay",   "s",      1.00f,50.00f, 1.00f, true,  0 },
    { "Overshooting",  "",      10.0f,200.0f, 10.0f, true,  0 },
    { "FOV Horizontal","deg",   50.0f,150.0f,  1.0f, false, 0 },
    { "FOV Vertical",  "deg",   30.0f,120.0f,  1.0f, false, 0 },
    { "Sensitivity",   "",       0.10f, 10.0f, 0.10f, false, 0 },
    { "DPI",           "",     100.0f,32000.f,100.0f, true,  0 },
    { "Smoothing",     "",       0.00f, 0.99f, 0.05f, false, 0 },
    { "Deadzone",      "deg",    0.00f, 20.0f, 0.50f, false, 0 },
    { "Max Speed",     "px/s",  50.0f,5000.f, 50.0f,  true,  0 },
};

// ── Console helpers ───────────────────────────────────────────────────────────
static int CfgConsoleW() {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi))
        return csbi.srWindow.Right - csbi.srWindow.Left + 1;
    return 80;
}

static void CfgHideCursor() {
    CONSOLE_CURSOR_INFO ci = { 1, FALSE };
    SetConsoleCursorInfo(GetStdHandle(STD_OUTPUT_HANDLE), &ci);
}

// ── Value display ─────────────────────────────────────────────────────────────
static const char* kPriorityLabels[] = { "Body", "Head", "Body+Head" };

static std::string GetValStr(int row, const IrisConfig& c) {
    switch (row) {
    case ROW_PRIORITY:   return kPriorityLabels[std::clamp(c.priority, 0, 2)];
    case ROW_SHOW_BOXES: return c.show_boxes ? "On" : "Off";
    case ROW_ROI:        return kROINames[std::clamp(c.roi_idx, 0, kROICount-1)];
    case ROW_AUTOFIRE:   return c.autofire ? "On" : "Off";
    case ROW_SMOOTHING:
        if (c.smoothing == 0.0f) return "Off";
        { char b[24]; snprintf(b,sizeof(b),"%.2f", c.smoothing); return b; }
    case ROW_DEADZONE:
        if (c.deadzone == 0.0f) return "Off";
        { char b[24]; snprintf(b,sizeof(b),"%.2f deg", c.deadzone); return b; }
    default: break;
    }
    const Row& r = kRows[row];
    float val = 0;
    switch (row) {
    case ROW_CONFIDENCE:  val = c.confidence;  break;
    case ROW_AUTOFIRE_INITIAL: {
        char b[16]; snprintf(b, sizeof(b), "%.1fs", c.autofire_initial * 0.1f); return b;
    }
    case ROW_AUTOFIRE_FRAMES: {
        char b[16]; snprintf(b, sizeof(b), "%.1fs", c.autofire_delay * 0.1f); return b;
    }
    case ROW_OVERSHOOTING: val = (float)c.overshooting; break;
    case ROW_FOV_H:       val = c.fov_h;       break;
    case ROW_FOV_V:       val = c.fov_v;       break;
    case ROW_SENSITIVITY: val = c.sensitivity; break;
    case ROW_DPI:         val = (float)c.dpi;  break;
    case ROW_MAX_SPEED:   val = c.max_speed;   break;
    default: break;
    }
    char buf[32];
    if (r.is_int)
        r.unit[0] ? snprintf(buf,sizeof(buf),"%d %s",(int)val,r.unit)
                  : snprintf(buf,sizeof(buf),"%d",(int)val);
    else
        r.unit[0] ? snprintf(buf,sizeof(buf),"%.2f %s",val,r.unit)
                  : snprintf(buf,sizeof(buf),"%.2f",val);
    return buf;
}

// ── Adjust ────────────────────────────────────────────────────────────────────
static void AdjustRow(IrisConfig& c, int row, int dir) {
    const Row& r = kRows[row];
    if (r.num_opts > 0) {
        if (row == ROW_PRIORITY)
            c.priority = ((c.priority + dir) % 3 + 3) % 3;
        else if (row == ROW_SHOW_BOXES)
            c.show_boxes = !c.show_boxes;
        else if (row == ROW_AUTOFIRE)
            c.autofire = !c.autofire;
        else if (row == ROW_ROI)
            c.roi_idx = ((c.roi_idx + dir) % kROICount + kROICount) % kROICount;
        return;
    }
    float step = r.step * dir;
    switch (row) {
    case ROW_CONFIDENCE:  c.confidence  = std::clamp(c.confidence  + step, r.min, r.max); break;
    case ROW_AUTOFIRE_INITIAL: c.autofire_initial = std::clamp(c.autofire_initial + (int)(r.step*dir), 1, 50); break;
    case ROW_AUTOFIRE_FRAMES:  c.autofire_delay   = std::clamp(c.autofire_delay   + (int)(r.step*dir), 1, 50); break;
    case ROW_OVERSHOOTING: c.overshooting = std::clamp(c.overshooting + (int)(r.step*dir), 10, 200); break;
    case ROW_FOV_H:       c.fov_h       = std::clamp(c.fov_h       + step, r.min, r.max); break;
    case ROW_FOV_V:       c.fov_v       = std::clamp(c.fov_v       + step, r.min, r.max); break;
    case ROW_SENSITIVITY: c.sensitivity = std::clamp(c.sensitivity + step, r.min, r.max); break;
    case ROW_DPI:         c.dpi         = (int)std::clamp((float)c.dpi+step, r.min, r.max); break;
    case ROW_SMOOTHING:   c.smoothing   = std::clamp(c.smoothing   + step, r.min, r.max); break;
    case ROW_DEADZONE:    c.deadzone    = std::clamp(c.deadzone    + step, r.min, r.max); break;
    case ROW_MAX_SPEED:   c.max_speed   = std::clamp(c.max_speed   + step, r.min, r.max); break;
    }
}

// ── Row rendering ─────────────────────────────────────────────────────────────
// Uses ASCII < > (guaranteed single-width) to avoid Unicode ambiguous-width bugs.
// Inner layout: "  <label><dots> < <val> >"
// Fixed chars  = 2(indent) + 1( )+1(<)+1( ) + 1( )+1(>) = 7  →  dots = inner_w - label - val - 7
static void PrintRow(int row, const IrisConfig& c, bool sel, int inner_w) {
    std::string val  = GetValStr(row, c);
    const char* lbl  = kRows[row].label;
    int llen = (int)strlen(lbl);
    int vlen = (int)val.size();
    int dots = inner_w - llen - vlen - 7;
    if (dots < 1) dots = 1;

    printf("  " CC_DIM "\xe2\x94\x82" CC_RST); // │

    if (sel) printf("  " CC_ACCENT CC_BOLD "%s" CC_RST, lbl);
    else     printf("  " CC_WHITE "%s" CC_RST, lbl);

    printf(CC_DIM);
    for (int i = 0; i < dots; i++) putchar('.');
    printf(CC_RST);

    if (sel)
        printf(" " CC_ACCENT CC_BOLD "<" CC_RST
               " " CC_OK    CC_BOLD "%s" CC_RST
               " " CC_ACCENT CC_BOLD ">" CC_RST,
               val.c_str());
    else
        printf(" " CC_DIM "<" CC_RST
               " " CC_BORDER "%s" CC_RST
               " " CC_DIM ">" CC_RST,
               val.c_str());

    printf(CC_DIM "\xe2\x94\x82" CC_RST "\n"); // │
}

// ── Section / blank rows ──────────────────────────────────────────────────────
static void PrintSection(const char* title, int inner_w) {
    int pad = inner_w - (int)strlen(title) - 4;
    if (pad < 0) pad = 0;
    printf("  " CC_DIM "\xe2\x94\x82" CC_RST "  " CC_INFO "%s" CC_RST, title);
    for (int i = 0; i < pad; i++) putchar(' ');
    printf("  " CC_DIM "\xe2\x94\x82" CC_RST "\n");
    // separator
    printf("  " CC_DIM "\xe2\x94\x82  ");
    for (int i = 0; i < inner_w - 4; i++) printf("\xe2\x94\x80");
    printf("  \xe2\x94\x82" CC_RST "\n");
}

static void PrintBlankRow(int inner_w) {
    printf("  " CC_DIM "\xe2\x94\x82");
    for (int i = 0; i < inner_w; i++) putchar(' ');
    printf("\xe2\x94\x82" CC_RST "\n");
}

// ── Full render — must print exactly CONFIG_LINES lines ───────────────────────
static void RenderConfig(const IrisConfig& c, int sel, int inner_w,
                         bool first_render, bool aiming) {
    if (!first_render)
        printf("\033[%dA", CONFIG_LINES);

    // 1  top border
    printf("  " CC_DIM "\xe2\x94\x8c" CC_BORDER " Configuration " CC_DIM);
    int td = inner_w - 15; if (td < 0) td = 0;
    for (int i = 0; i < td; i++) printf("\xe2\x94\x80");
    printf("\xe2\x94\x90" CC_RST "\n");

    PrintBlankRow(inner_w);                                              // 2

    PrintSection("DETECTION", inner_w);                                  // 3,4
    PrintBlankRow(inner_w);                                              // 5
    PrintRow(ROW_CONFIDENCE, c, sel == ROW_CONFIDENCE, inner_w);         // 6
    PrintRow(ROW_PRIORITY,   c, sel == ROW_PRIORITY,   inner_w);         // 7
    PrintRow(ROW_SHOW_BOXES, c, sel == ROW_SHOW_BOXES, inner_w);         // 8
    PrintRow(ROW_ROI,        c, sel == ROW_ROI,        inner_w);         // 9

    PrintBlankRow(inner_w);                                              // 10

    PrintSection("AIMING", inner_w);                                     // 11,12
    PrintBlankRow(inner_w);                                              // 13
    PrintRow(ROW_AUTOFIRE,         c, sel == ROW_AUTOFIRE,         inner_w); // 14
    PrintRow(ROW_AUTOFIRE_INITIAL, c, sel == ROW_AUTOFIRE_INITIAL, inner_w); // 15
    PrintRow(ROW_AUTOFIRE_FRAMES,  c, sel == ROW_AUTOFIRE_FRAMES,  inner_w); // 16
    PrintRow(ROW_OVERSHOOTING, c, sel == ROW_OVERSHOOTING, inner_w);     // 17
    PrintRow(ROW_FOV_H,        c, sel == ROW_FOV_H,        inner_w);     // 18
    PrintRow(ROW_FOV_V,        c, sel == ROW_FOV_V,        inner_w);     // 19
    PrintRow(ROW_SENSITIVITY,  c, sel == ROW_SENSITIVITY,  inner_w);     // 20
    PrintRow(ROW_DPI,          c, sel == ROW_DPI,          inner_w);     // 21
    PrintRow(ROW_SMOOTHING,    c, sel == ROW_SMOOTHING,    inner_w);     // 22
    PrintRow(ROW_DEADZONE,     c, sel == ROW_DEADZONE,     inner_w);     // 23
    PrintRow(ROW_MAX_SPEED,    c, sel == ROW_MAX_SPEED,    inner_w);     // 24

    PrintBlankRow(inner_w);                                              // 25

    // 26  hint row — inline F1 toggle state
    // Visible content between borders: "  " + 33 visible chars + hint_pad + "  "
    // Count: 2+2+6+2+6+2+2+3+3+1+6 = 35 → hint_pad = inner_w - 35 - 2 = inner_w - 37
    const char* aim_tag  = aiming ? CC_OK  "ON " CC_RST : CC_ERR "OFF" CC_RST;
    int         hint_pad = inner_w - 37;
    if (hint_pad < 0) hint_pad = 0;
    printf("  " CC_DIM "\xe2\x94\x82" CC_RST
           "  " CC_DIM "\xe2\x86\x91\xe2\x86\x93" CC_RST " Nav  "
           CC_DIM "\xe2\x86\x90\xe2\x86\x92" CC_RST " Adj  "
           CC_INFO "F1" CC_RST " [%s]  "
           CC_INFO "R" CC_RST " Reset",
           aim_tag);
    for (int i = 0; i < hint_pad; i++) putchar(' ');
    printf("  " CC_DIM "\xe2\x94\x82" CC_RST "\n");

    PrintBlankRow(inner_w);                                              // 27

    // 28  bottom border
    printf("  " CC_DIM "\xe2\x94\x94");
    for (int i = 0; i < inner_w; i++) printf("\xe2\x94\x80");
    printf("\xe2\x94\x98" CC_RST "\n");

    fflush(stdout);
}
