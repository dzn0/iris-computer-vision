#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include <cstdio>

#include "AntiDebug.h"
#include "config.h"
#include "SupabaseValidator.h"
#include "TokenScreen.h"
#include "SecurityCheck.h"
#include "Downloader.h"

#define C_RST  "\033[0m"
#define C_ERR  "\033[38;5;197m"
#define C_OK   "\033[38;5;141m"

static void EnableAnsiConsole() {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    SetConsoleCP(65001);
    SetConsoleOutputCP(65001);
    if (GetConsoleMode(h, &mode))
        SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING | ENABLE_PROCESSED_OUTPUT);
    _setmode(_fileno(stdout), _O_BINARY);
}

int main()
{
    // ── [1] Anti-debug — very first thing, before any output ─────────────
    AntiDebugInit();   // hide thread + take .text CRC baseline
    AntiDebugCheck();

    EnableAnsiConsole();

    // ── [2] Token validation ──────────────────────────────────────────────
    if (!TokenValidationScreen()) {
        std::cout << C_ERR << "\n  Closing..." << C_RST << "\n";
        Sleep(2000);
        return 1;
    }

    // ── [3] Anti-debug again — after token screen (patch window) ─────────
    AntiDebugCheck();

    // ── [4] System requirements check ────────────────────────────────────
    if (!SecurityCheckScreen()) {
        std::cout << C_ERR << "\n  Aborted." << C_RST << "\n";
        Sleep(2000);
        return 1;
    }

    // ── [5] Anti-debug one more time — before launching IrisCore ─────────
    AntiDebugCheck();

    // ── [6] Download IrisCore (skip if found alongside Iris.exe — DEV ONLY) ──
    wchar_t exeDir[MAX_PATH], localPath[MAX_PATH];
    GetModuleFileNameW(NULL, exeDir, MAX_PATH);
    // strip filename to get directory
    wchar_t* last = wcsrchr(exeDir, L'\\');
    if (last) *(last + 1) = L'\0';
    swprintf_s(localPath, L"%sIrisCore.exe", exeDir);

    const wchar_t* launchPath = IRISCORE_TEMP_PATH;
    if (GetFileAttributesW(localPath) != INVALID_FILE_ATTRIBUTES) {
        std::cout << C_OK << "\n  [DEV] " << C_RST << "Local IrisCore.exe found — skipping download.\n";
        launchPath = localPath;
    } else {
        std::cout << C_INFO << "\n  [*] " << C_WHITE << "Downloading IrisCore..." << C_RST << "\n\n";
        if (!DownloadIrisCore()) {
            Sleep(3000);
            return 1;
        }
    }

    // ── [7] Launch IrisCore ───────────────────────────────────────────────
    std::cout << C_OK << "\n  Launching..." << C_RST << "\n\n";
    Sleep(300);

    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    // Inherit console — IrisCore output goes to this same window, no second console
    if (!CreateProcessW(launchPath, NULL, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        std::cout << C_ERR << "  [-] Failed to launch IrisCore.exe." << C_RST << "\n";
        Sleep(3000);
        return 1;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return 0;
}
