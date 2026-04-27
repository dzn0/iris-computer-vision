#pragma once
#include <windows.h>
#include <tlhelp32.h>
#include <string>
#include <iostream>
#include <conio.h>

// ── Palette (same as TokenScreen) ────────────────────────────────────────────
#ifndef C_RST
#define C_RST    "\033[0m"
#endif
#ifndef C_OK
#define C_OK     "\033[38;5;141m"
#endif
#ifndef C_ERR
#define C_ERR    "\033[38;5;197m"
#endif
#ifndef C_WARN
#define C_WARN   "\033[38;5;214m"
#endif
#ifndef C_INFO
#define C_INFO   "\033[38;5;111m"
#endif
#ifndef C_WHITE
#define C_WHITE  "\033[38;5;255m"
#endif
#ifndef C_GRAY
#define C_GRAY   "\033[38;5;245m"
#endif
#ifndef C_ACCENT
#define C_ACCENT "\033[38;5;213m"
#endif
#ifndef C_BOLD
#define C_BOLD   "\033[1m"
#endif

// ── Registry helpers ─────────────────────────────────────────────────────────
static DWORD RegReadDword(HKEY root, const wchar_t* path, const wchar_t* name, DWORD def = 0xFFFFFFFF) {
    HKEY hKey = NULL;
    if (RegOpenKeyExW(root, path, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return def;
    DWORD val = def, size = sizeof(DWORD), type = REG_DWORD;
    RegQueryValueExW(hKey, name, NULL, &type, (LPBYTE)&val, &size);
    RegCloseKey(hKey);
    return val;
}

// ── Check: Core Isolation / Memory Integrity (HVCI) ─────────────────────────
// Returns true if DISABLED (good for us)
static bool CheckCoreIsolationDisabled() {
    // Primary key
    DWORD v = RegReadDword(
        HKEY_LOCAL_MACHINE,
        L"SYSTEM\\CurrentControlSet\\Control\\DeviceGuard\\Scenarios\\HypervisorEnforcedCodeIntegrity",
        L"Enabled"
    );
    if (v == 0) return true;
    if (v == 1) return false;

    // Fallback key (older Windows builds)
    v = RegReadDword(
        HKEY_LOCAL_MACHINE,
        L"SYSTEM\\CurrentControlSet\\Control\\DeviceGuard",
        L"EnableVirtualizationBasedSecurity"
    );
    return (v == 0 || v == 0xFFFFFFFF);
}

// ── Check: Valorant not running ───────────────────────────────────────────────
// Returns true if Valorant is NOT running (good for us)
static bool CheckValorantClosed() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return true;
    PROCESSENTRY32W pe = { sizeof(pe) };
    bool found = false;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, L"VALORANT-Win64-Shipping.exe") == 0 ||
                _wcsicmp(pe.szExeFile, L"vgc.exe") == 0) {
                found = true; break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return !found;
}

// ── Check: Windows Defender Real-Time Protection ─────────────────────────────
// Returns true if DISABLED (good for us)
static bool CheckDefenderDisabled() {
    DWORD v = RegReadDword(
        HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows Defender\\Real-Time Protection",
        L"DisableRealtimeMonitoring"
    );
    return (v == 1);
}

// ── Box drawing constants (UTF-8) ─────────────────────────────────────────────
static const char* SC_TL = "\xe2\x95\xad"; // ╭
static const char* SC_TR = "\xe2\x95\xae"; // ╮
static const char* SC_BL = "\xe2\x95\xb0"; // ╰
static const char* SC_BR = "\xe2\x95\xaf"; // ╯
static const char* SC_HZ = "\xe2\x94\x80"; // ─
static const char* SC_VT = "\xe2\x94\x82"; // │

// Box inner visible width (excluding the two border chars)
// Must fit longest row: label(33) + pad(1) + badge(8) + sep(2) + failMsg(46) = 90 visible chars
// SC_W - 4 >= 90  →  SC_W >= 94  (using 95 for 1 char breathing room)
static const int SC_W = 95;

// Repeat a box-drawing char N times
static std::string ScRep(const char* s, int n) {
    std::string r;
    for (int i = 0; i < n; i++) r += s;
    return r;
}

// Visible length (strips ANSI codes)
static int ScVisLen(const std::string& s) {
    int v = 0; bool esc = false;
    for (char c : s) {
        if (c == '\033') { esc = true; continue; }
        if (esc) { if (c == 'm') esc = false; continue; }
        // Count only first byte of each UTF-8 sequence as 1 col
        if ((unsigned char)c < 0x80 || (unsigned char)c >= 0xC0) v++;
    }
    return v;
}

// Print one row inside the box: │  <content padded to SC_W-2>  │
static void ScRow(const std::string& content = "") {
    int vis = ScVisLen(content);
    int pad = (SC_W - 4) - vis;  // 4 = 2 leading spaces + 2 trailing spaces inside box
    std::cout << "  " << C_ACCENT << SC_VT << C_RST
              << "  " << content
              << std::string(pad > 0 ? pad : 0, ' ')
              << "  " << C_ACCENT << SC_VT << C_RST << "\n";
}

// Print a check row with label, status badge, and message — all inside the box
static void ScCheckRow(const char* label, bool ok, const char* okMsg, const char* failMsg) {
    // Build colored content string
    std::string content;
    content += C_WHITE;
    content += label;
    content += C_RST;

    // Pad label to 34 visible chars
    int labelPad = 34 - (int)strlen(label);
    if (labelPad > 0) content += std::string(labelPad, ' ');

    if (ok) {
        content += C_OK;
        content += "[  OK  ]";
        content += C_RST;
        content += "  ";
        content += C_GRAY;
        content += okMsg;
        content += C_RST;
    } else {
        content += C_ERR;
        content += "[ FAIL ]";
        content += C_RST;
        content += "  ";
        content += C_WARN;
        content += failMsg;
        content += C_RST;
    }

    ScRow(content);
}

// ── Full security check screen ────────────────────────────────────────────────
// Returns true if user confirms (Enter), false if aborts (Esc/q)
static bool SecurityCheckScreen() {
    // ── Static frame (drawn once) ─────────────────────────────────────────
    std::cout << "\n  " << C_ACCENT
              << SC_TL << ScRep(SC_HZ, 3) << " System Check " << ScRep(SC_HZ, SC_W - 3 - 14)
              << SC_TR << C_RST << "\n";
    ScRow();

    // ── Save cursor here — dynamic rows are redrawn from this point ───────
    std::cout << "\033[s" << std::flush;

    // ── Loop: redraw dynamic rows + wait for input ────────────────────────
    while (true) {
        bool coreOk      = CheckCoreIsolationDisabled();
        bool defenderOk  = CheckDefenderDisabled();
        bool valorantOk  = CheckValorantClosed();
        bool allOk       = coreOk && defenderOk && valorantOk;

        // Restore to saved position and overwrite dynamic rows
        std::cout << "\033[u";

        ScCheckRow("Core Isolation (Memory Integrity)", coreOk,
            "Disabled", "Still enabled  -  disable in Windows Security");

        ScCheckRow("Windows Defender (Real-Time)", defenderOk,
            "Disabled", "Still active   -  disable before continuing");

        ScCheckRow("Valorant / Vanguard", valorantOk,
            "Not running", "Close Valorant before launching Iris");

        ScRow();

        // Status / prompt row
        {
            std::string msg;
            if (allOk) {
                msg += C_OK; msg += "All checks passed."; msg += C_RST;
                msg += "  Press ";
                msg += C_WHITE; msg += C_BOLD; msg += "Enter"; msg += C_RST;
                msg += " to continue  or  ";
                msg += C_GRAY; msg += "Esc"; msg += C_RST; msg += " to abort.";
            } else {
                msg += C_WARN; msg += "Checks failed."; msg += C_RST;
                msg += "  Fix the issues above, then press ";
                msg += C_WHITE; msg += C_BOLD; msg += "Enter"; msg += C_RST;
                msg += " to retry  or  ";
                msg += C_GRAY; msg += "Esc"; msg += C_RST; msg += " to abort.";
            }
            ScRow(msg);
        }

        std::cout << "  " << C_ACCENT
                  << SC_BL << ScRep(SC_HZ, SC_W) << SC_BR
                  << C_RST << "\n\n" << std::flush;

        // Wait for input
        int ch = _getch();
        if (ch == 27 || ch == 'q' || ch == 'Q') return false;
        if (ch == '\r' || ch == '\n') {
            if (allOk) return true;
            // Not ready yet — loop back, restore cursor and re-check
        }
    }
}
