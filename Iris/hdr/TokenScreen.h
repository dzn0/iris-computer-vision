#pragma once

#include <string>
#include <iostream>
#include <conio.h>
#include <windows.h>

// ── Palette ───────────────────────────────────────────────────────────────────
#define C_RST     "\033[0m"
#define C_BG_ROW  "\033[48;5;17m"        // dark navy bg accent
#define C_BORDER  "\033[38;5;183m"        // medium purple
#define C_BORDER2 "\033[38;5;57m"        // deeper purple (inner accents)
#define C_ACCENT  "\033[38;5;213m"       // hot pink-purple (highlights)
#define C_TITLE   "\033[38;5;183m"       // lavender
#define C_WHITE   "\033[38;5;255m"       // bright white
#define C_GRAY    "\033[38;5;245m"       // mid-gray
#define C_DIM     "\033[38;5;60m"        // dim purple
#define C_OK      "\033[38;5;141m"       // light purple
#define C_ERR     "\033[38;5;197m"       // red-pink
#define C_INFO    "\033[38;5;111m"       // periwinkle
#define C_BOLD    "\033[1m"
#define C_RESET_BOLD "\033[22m"

#define LOG_OK(m)   std::cout << C_OK   << "  [+] " << C_WHITE << m << C_RST << "\n"
#define LOG_ERR(m)  std::cout << C_ERR  << "  [-] " << C_WHITE << m << C_RST << "\n"
#define LOG_INFO(m) std::cout << C_INFO << "  [*] " << C_WHITE << m << C_RST << "\n"

// ── Helpers ───────────────────────────────────────────────────────────────────
static int ConsoleWidth() {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi))
        return csbi.srWindow.Right - csbi.srWindow.Left + 1;
    return 80;
}

static std::string Rep(const std::string& s, int n) {
    std::string r; r.reserve(s.size() * n);
    for (int i = 0; i < n; i++) r += s;
    return r;
}

// Visible length (strips ANSI escape codes)
static int VisLen(const std::string& s) {
    int v = 0; bool esc = false;
    for (char c : s) {
        if (c == '\033') esc = true;
        else if (esc && c == 'm') esc = false;
        else if (!esc) v++;
    }
    return v;
}

static std::string PadR(const std::string& s, int w) {
    int sp = w - VisLen(s);
    return s + std::string(sp > 0 ? sp : 0, ' ');
}

static std::string CenterStr(const std::string& s, int w) {
    int len = VisLen(s);
    if (len >= w) return s;
    int l = (w - len) / 2;
    return std::string(l, ' ') + s;
}

// ── Box drawing ───────────────────────────────────────────────────────────────
static const char* TL = "\xe2\x95\xad"; // ╭
static const char* TR = "\xe2\x95\xae"; // ╮
static const char* BL = "\xe2\x95\xb0"; // ╰
static const char* BR = "\xe2\x95\xaf"; // ╯
static const char* HZ = "\xe2\x94\x80"; // ─
static const char* VT = "\xe2\x94\x82"; // │
static const char* ML = "\xe2\x94\x9c"; // ├
static const char* MR = "\xe2\x94\xa4"; // ┤


//╭──── Iris v1.0.1 ────────────────────────────────────────────────────────╮
//│                                    │                                    │                                         
//│                                    │                                    │
//│                                    │                                    │
//│                                    │                                    │
//│                                    │                                    │
//│                                    │                                    │
//╰─────────────────────────────────────────────────────────────────────────╯

//╭─────── Iris v1.0 ─────────────────────────────────────────────────────────╮
//│                              │                                            │
//│    ██╗██████╗ ██╗███████╗    │ Iris is a computer vision aim assist tool  │
//│    ██║██╔══██╗██║██╔════╝    │ ────────────────────────────────────────── │
//│    ██║██████╔╝██║███████╗    │                                            │
//│    ██║██╔══██╗██║╚════██║    │ Start getting your token at discord, then  │
//│    ██║██║  ██║██║███████║    │ paste it here so you can aceess the tool   │
//│    ╚═╝╚═╝  ╚═╝╚═╝╚══════╝    │                                            │
//│         dc: @d.zn.           │                                            │
//╰───────────────────────────────────────────────────────────────────────────╯

//╭───────────────────────────────────────────────────────────────────────────╮
//│Token>                                                                     │
//╰───────────────────────────────────────────────────────────────────────────╯

// ── Logo ──────────────────────────────────────────────────────────────────────
static void PrintBanner(int W) {
    std::cout << "\n";

    // Banner with IRIS logo and info
    const char* banner[10] = {
        "╭─────── Iris v1.0 ───────────────────────────────────────────────────────────────────────╮",
        "│                                            │                                            │",
        "│           ██╗██████╗ ██╗███████╗           │ Iris is a computer vision aim assist tool  │",
        "│           ██║██╔══██╗██║██╔════╝           │ ────────────────────────────────────────── │",
        "│           ██║██████╔╝██║███████╗           │                                            │",
        "│           ██║██╔══██╗██║╚════██║           │ Start getting your token at discord, then  │",
        "│           ██║██║  ██║██║███████║           │ paste it here so you can aceess the tool.  │",
        "│           ╚═╝╚═╝  ╚═╝╚═╝╚══════╝           │                                            │",
        "│                 dc: @d.zn.                 │                                            │",
        "╰─────────────────────────────────────────────────────────────────────────────────────────╯",
    };

    // UTF-8 sequence length
    auto seqLen = [](unsigned char b) -> int {
        if (b >= 0xF0) return 4;
        if (b >= 0xE0) return 3;
        if (b >= 0xC0) return 2;
        return 1;
    };

    // Is this position a box-drawing border char?
    auto isBorderSeq = [](const std::string& s, size_t j) -> bool {
        std::string seq = s.substr(j, 3);
        return seq == "\xe2\x94\x82" || seq == "\xe2\x94\x80" ||
               seq == "\xe2\x95\xad" || seq == "\xe2\x95\xae" ||
               seq == "\xe2\x95\xb0" || seq == "\xe2\x95\xaf" ||
               seq == "\xe2\x94\x9c" || seq == "\xe2\x94\xa4";
    };

    const char* C_LIGHT = "\033[38;5;250m";
    std::string VT3 = "\xe2\x94\x82";

    for (int i = 0; i < 10; i++) {
        std::string line = banner[i];

        if (i == 0) {
            // Top border: v1.0 in dim gray, rest in C_ACCENT
            size_t pos = line.find("v1.0");
            std::cout << C_ACCENT << line.substr(0, pos)
                      << C_LIGHT  << "v1.0"
                      << C_ACCENT << line.substr(pos + 4)
                      << C_RST << "\n";

        } else if (i == 9) {
            // Bottom border: all C_ACCENT
            std::cout << C_ACCENT << line << C_RST << "\n";

        } else if (i == 5 || i == 6) {
            // Instruction lines: find 3 │ chars, color segments
            size_t p1 = line.find(VT3);
            size_t p2 = (p1 != std::string::npos) ? line.find(VT3, p1 + 3) : std::string::npos;
            size_t p3 = (p2 != std::string::npos) ? line.find(VT3, p2 + 3) : std::string::npos;
            if (p1 != std::string::npos && p2 != std::string::npos && p3 != std::string::npos) {
                std::cout << C_ACCENT << VT3
                          << C_BORDER << line.substr(p1 + 3, p2 - p1 - 3)
                          << C_ACCENT << VT3
                          << C_LIGHT  << line.substr(p2 + 3, p3 - p2 - 3)
                          << C_ACCENT << VT3
                          << C_RST    << "\n";
            } else {
                std::cout << C_ACCENT << line << C_RST << "\n";
            }

        } else {
            // All other content rows: borders in C_ACCENT, text in C_BORDER
            std::string out = C_ACCENT;
            const char* cur = C_ACCENT;
            size_t dcPos = line.find("dc: @d.zn.");

            for (size_t j = 0; j < line.size(); ) {
                int sl = seqLen((unsigned char)line[j]);
                if (dcPos != std::string::npos && j == dcPos) {
                    if (cur != C_DIM) { out += C_DIM; cur = C_DIM; }
                    if (cur != C_LIGHT) { out += C_LIGHT; cur = C_LIGHT; }
                    out += "dc: @d.zn.";
                    j += 10; continue;
                }
                bool border = (j + 2 < line.size()) && isBorderSeq(line, j);
                const char* want = border ? C_ACCENT : C_BORDER;
                if (cur != want) { out += want; cur = want; }
                out += line.substr(j, border ? 3 : sl);
                j += border ? 3 : sl;
            }
            std::cout << out << C_RST << "\n";
        }
    }

    std::cout << "\n";
}

// ── Token input screen ────────────────────────────────────────────────────────
static bool s_token_first_render = true;

// Banner = 1 blank + 10 banner lines + 1 blank = 12
// Input box = 3 lines (top border, input, bottom border)
// Total = 15 lines redrawn on retry
static const int TOKEN_SCREEN_LINES = 15;

static std::string GetTokenInput() {
    // Enable VT processing
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD outMode = 0;
    GetConsoleMode(hOut, &outMode);
    SetConsoleMode(hOut, outMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);

    int W = ConsoleWidth();

    if (s_token_first_render) {
        s_token_first_render = false;
    } else {
        printf("\033[2J\033[H");  // Clear screen and move cursor to top
    }
    PrintBanner(W);

    // ── Token input box ───────────────────────────────────────────────
    std::cout << C_ACCENT << "╭─────────────────────────────────────────────────────────────────────────────────────────╮" << C_RST << "\n";
    std::cout << C_ACCENT << "│" << C_RST << "Token> " << std::flush;

    int inputStart = 7;  // "Token> " = 7 chars
    int maxInputLen = 64;  // Content width

    // Print bottom border immediately (will be at line below input)
    std::cout << std::string(maxInputLen - inputStart, ' ') << C_ACCENT << "                         │" << C_RST << "\n";
    std::cout << C_ACCENT << "╰─────────────────────────────────────────────────────────────────────────────────────────╯" << C_RST << "\n";

    // Move cursor back up to input line
    std::cout << "\033[2A\r\033[7C" << std::flush;  // Up 2 lines, carriage return, move right 7 chars ("Token> ")

    // ── Masked input ───────────────────────────────────────────────────
    HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
    DWORD oldMode; GetConsoleMode(hStdin, &oldMode);
    SetConsoleMode(hStdin, oldMode & ~ENABLE_ECHO_INPUT);

    std::string token;

    auto AppendChar = [&](char c) {
        if ((int)token.size() < maxInputLen - inputStart) {
            token += c;
            std::cout << C_ACCENT << "●" << C_RST << std::flush;
        }
    };
    char ch;
    while ((ch = (char)_getch()) != '\r') {
        if (ch == '\x16') {
            // Ctrl+V — paste from clipboard
            if (OpenClipboard(NULL)) {
                HANDLE hData = GetClipboardData(CF_TEXT);
                if (hData) {
                    const char* pText = (const char*)GlobalLock(hData);
                    if (pText) {
                        for (const char* p = pText; *p && *p != '\r' && *p != '\n'; ++p)
                            if (*p >= 32 && *p < 127) AppendChar(*p);
                        GlobalUnlock(hData);
                    }
                }
                CloseClipboard();
            }
        } else if ((ch == '\b' || ch == 127) && !token.empty()) {
            token.pop_back();
            std::cout << "\b \b" << std::flush;
        } else if (ch >= 32 && ch < 127) {
            AppendChar(ch);
        }
    }
    SetConsoleMode(hStdin, oldMode);

    std::cout << "\n\n";

    return token;
}

// Forward declaration - implemented in SupabaseValidator.h
extern bool ValidateTokenWithSupabase(const std::string& token);

// ── Validate online (Supabase) ────────────────────────────────────────────────
static bool ValidateTokenOnline(const std::string& token) {
    std::cout << C_INFO << "  [*] " << C_WHITE << "Validating token..." << C_RST << "\n";
    std::cout.flush();

    bool valid = ValidateTokenWithSupabase(token);

    if (valid) {
        std::cout << C_OK << "  [+] " << C_WHITE << "Token valid!" << C_RST << "\n";
    } else {
        std::cout << C_ERR << "  [-] " << C_WHITE << "Invalid Token" << C_RST << "\n";
    }
    std::cout.flush();

    Sleep(800);
    return valid;
}

// ── Full screen-1 flow ────────────────────────────────────────────────────────
static bool TokenValidationScreen() {
    while (true) {
        std::string token = GetTokenInput();

        if (token.empty()) {
            LOG_ERR("Token not provided.");
            Sleep(2000);
            continue;
        }

        if (!ValidateTokenOnline(token)) {
            Sleep(2000);
            continue;
        }

        return true;
    }
}
