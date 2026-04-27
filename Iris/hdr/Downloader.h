#pragma once

#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <wincrypt.h>
#include <string>
#include <vector>
#include <iostream>
#include "config.h"

#define C_RST    "\033[0m"
#define C_OK     "\033[38;5;141m"
#define C_ERR    "\033[38;5;197m"
#define C_INFO   "\033[38;5;111m"
#define C_WHITE  "\033[38;5;255m"
#define C_DIM    "\033[38;5;60m"
#define C_ACCENT "\033[38;5;213m"
#define C_BORDER "\033[38;5;183m"

// ── Console width ─────────────────────────────────────────────────────────────
static int GetConsoleW() {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi))
        return csbi.srWindow.Right - csbi.srWindow.Left + 1;
    return 80;
}

// ── Progress bar ──────────────────────────────────────────────────────────────
// right-side info: " 100%  141.0/141 MB  @ 48.00 MB/s   " ≈ 38 visible chars
// left margin: "  │ " = 4 chars
// bar fills remaining space
static void PrintProgress(LONGLONG received, LONGLONG total, double speedMBs) {
    int consW = GetConsoleW() - 1; // -1 so we never trigger a wrap
    // right text (no ANSI): " 100%  141.0/141 MB  @ 93.98 MB/s" = 34 chars
    const int RIGHT_VIS = 34;
    const int LEFT_VIS  = 4; // "  │ "
    int barW = consW - LEFT_VIS - RIGHT_VIS;
    if (barW < 4) barW = 4;

    int pct    = total > 0 ? (int)(received * 100 / total) : 0;
    int filled = total > 0 ? (int)((double)pct / 100.0 * barW) : barW;

    std::string bar;
    bar += C_ACCENT;
    for (int i = 0; i < filled; i++)     bar += "\xe2\x96\x88"; // █
    bar += C_DIM;
    for (int i = filled; i < barW; i++)  bar += "\xe2\x96\x91"; // ░
    bar += C_RST;

    char right[128];
    if (total > 0)
        snprintf(right, sizeof(right),
            " " C_WHITE "%3d%%" C_RST
            "  " C_BORDER "%.1f" C_DIM "/" C_BORDER "%.0f MB" C_RST
            "  " C_DIM "@ " C_OK "%.2f MB/s" C_RST,
            pct, received / 1048576.0, total / 1048576.0, speedMBs);
    else
        snprintf(right, sizeof(right),
            " " C_ACCENT "↓" C_RST
            " " C_WHITE "%.1f MB" C_RST
            "  " C_DIM "@ " C_BORDER "%.2f MB/s" C_RST,
            received / 1048576.0, speedMBs);

    std::cout << "\r  " C_DIM "│" C_RST " " << bar << right << std::flush;
}

// box border sized to console width
static std::string BoxLine(const std::string& label = "") {
    int w   = GetConsoleW() - 4; // "  ╭" + "╮" = 4
    int lbl = (int)label.size();
    int rem = w - lbl - 2;      // "─ " + label + " ─..."
    if (rem < 0) rem = 0;
    std::string s = "  " C_DIM "╭─" C_BORDER + label + C_DIM + " ";
    for (int i = 0; i < rem; i++) s += "─";
    s += "╮" C_RST;
    return s;
}
static std::string BoxBottom() {
    int w = GetConsoleW() - 4; // "  ╰" (3) + dashes + "╯" (1) = consW
    std::string s = "  " C_DIM "╰";
    for (int i = 0; i < w; i++) s += "─";
    s += "╯" C_RST;
    return s;
}

// ── Utilities ─────────────────────────────────────────────────────────────────
static std::string JsonStr(const std::string& json, const std::string& key) {
    std::string sk = "\"" + key + "\":\"";
    auto pos = json.find(sk);
    if (pos == std::string::npos) return {};
    pos += sk.size();
    auto end = json.find('"', pos);
    return end == std::string::npos ? "" : json.substr(pos, end - pos);
}

static std::vector<uint8_t> B64UrlDecode(const std::string& s) {
    std::string b64 = s;
    for (auto& c : b64) { if (c == '-') c = '+'; if (c == '_') c = '/'; }
    while (b64.size() % 4) b64 += '=';
    DWORD cb = 0;
    CryptStringToBinaryA(b64.c_str(), 0, CRYPT_STRING_BASE64, NULL, &cb, NULL, NULL);
    std::vector<uint8_t> out(cb);
    CryptStringToBinaryA(b64.c_str(), 0, CRYPT_STRING_BASE64, out.data(), &cb, NULL, NULL);
    out.resize(cb);
    return out;
}

// ── WinHTTP helpers ───────────────────────────────────────────────────────────
static std::string HttpGet(const wchar_t* host, const wchar_t* path,
                            const wchar_t* headers = nullptr) {
    HINTERNET hS = WinHttpOpen(L"Mozilla/5.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hS) return {};
    HINTERNET hC = WinHttpConnect(hS, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET hR = hC ? WinHttpOpenRequest(hC, L"GET", path, NULL, NULL, NULL,
                                            WINHTTP_FLAG_SECURE) : NULL;
    if (!hR) { if (hC) WinHttpCloseHandle(hC); WinHttpCloseHandle(hS); return {}; }
    if (headers) WinHttpAddRequestHeaders(hR, headers, (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);
    std::string resp;
    if (WinHttpSendRequest(hR, NULL, 0, NULL, 0, 0, 0) && WinHttpReceiveResponse(hR, NULL)) {
        char buf[8192]; DWORD rd = 0;
        while (WinHttpReadData(hR, buf, sizeof(buf), &rd) && rd > 0) resp.append(buf, rd);
    }
    WinHttpCloseHandle(hR); WinHttpCloseHandle(hC); WinHttpCloseHandle(hS);
    return resp;
}

static std::string HttpPost(const wchar_t* host, const wchar_t* path,
                             const std::string& body) {
    HINTERNET hS = WinHttpOpen(L"Mozilla/5.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hS) return {};
    HINTERNET hC = WinHttpConnect(hS, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET hR = hC ? WinHttpOpenRequest(hC, L"POST", path, NULL, NULL, NULL,
                                            WINHTTP_FLAG_SECURE) : NULL;
    if (!hR) { if (hC) WinHttpCloseHandle(hC); WinHttpCloseHandle(hS); return {}; }
    WinHttpAddRequestHeaders(hR, L"Content-Type: application/json",
                             (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);
    std::string resp;
    if (WinHttpSendRequest(hR, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                           (LPVOID)body.c_str(), (DWORD)body.size(), (DWORD)body.size(), 0) &&
        WinHttpReceiveResponse(hR, NULL)) {
        char buf[8192]; DWORD rd = 0;
        while (WinHttpReadData(hR, buf, sizeof(buf), &rd) && rd > 0) resp.append(buf, rd);
    }
    WinHttpCloseHandle(hR); WinHttpCloseHandle(hC); WinHttpCloseHandle(hS);
    return resp;
}

// ── Supabase config fetch ─────────────────────────────────────────────────────
static std::string FetchConfig(const char* key) {
    std::wstring path = std::wstring(L"/rest/v1/config?key=eq.")
                      + std::wstring(key, key + strlen(key))
                      + L"&select=value";
    std::wstring hdrs = std::wstring(L"apikey: ") + SUPABASE_ANON_KEY;
    return JsonStr(HttpGet(SUPABASE_HOST, path.c_str(), hdrs.c_str()), "value");
}

// ── Mega API: resolve file ID → direct download URL ───────────────────────────
static std::string MegaGetUrl(const std::string& fileId) {
    std::string body = "[{\"a\":\"g\",\"g\":1,\"p\":\"" + fileId + "\"}]";
    std::string resp = HttpPost(L"g.api.mega.co.nz", L"/cs?id=1", body);
    if (resp.empty() || resp[0] != '[') return {};
    return JsonStr(resp, "g");
}

// ── Mega key derivation ───────────────────────────────────────────────────────
struct MegaKeys { uint8_t aes[16]; uint8_t iv[8]; };

static MegaKeys MegaDeriveKeys(const std::vector<uint8_t>& k32) {
    MegaKeys mk = {};
    if (k32.size() < 32) return mk;
    for (int i = 0; i < 16; i++) mk.aes[i] = k32[i] ^ k32[i + 16];
    memcpy(mk.iv, k32.data() + 16, 8);
    return mk;
}

// ── AES-128-CTR decrypt (BCrypt ECB keystream, batch 64 KB) ──────────────────
static bool AesCtrDecrypt(BCRYPT_KEY_HANDLE hKey, const uint8_t* iv8,
                           uint64_t& blockOffset, uint8_t* data, size_t len) {
    const size_t BATCH = 4096;
    std::vector<uint8_t> ctrs(BATCH * 16), ks(BATCH * 16);
    size_t pos = 0;
    while (pos < len) {
        size_t rem   = (len - pos + 15) / 16;
        size_t count = rem < BATCH ? rem : BATCH;
        for (size_t b = 0; b < count; b++) {
            uint64_t blk = blockOffset + b;
            uint8_t* cb  = ctrs.data() + b * 16;
            memcpy(cb, iv8, 8);
            for (int j = 0; j < 8; j++) cb[8 + j] = (uint8_t)(blk >> (56 - j * 8));
        }
        ULONG cbResult = 0;
        if (!BCRYPT_SUCCESS(BCryptEncrypt(hKey, ctrs.data(), (ULONG)(count * 16),
                                          NULL, NULL, 0, ks.data(), (ULONG)(count * 16),
                                          &cbResult, 0))) return false;
        size_t xLen = count * 16;
        if (pos + xLen > len) xLen = len - pos;
        for (size_t i = 0; i < xLen; i++) data[pos + i] ^= ks[i];
        blockOffset += count;
        pos += xLen;
    }
    return true;
}

// ── Stream download + decrypt from Mega CDN URL ───────────────────────────────
static LONGLONG StreamMega(const std::string& urlA, const MegaKeys& keys, HANDLE hFile) {
    // Parse URL → host + path
    int wlen = MultiByteToWideChar(CP_UTF8, 0, urlA.c_str(), -1, NULL, 0);
    std::wstring wurl(wlen, 0);
    MultiByteToWideChar(CP_UTF8, 0, urlA.c_str(), -1, &wurl[0], wlen);
    size_t p = wurl.find(L"://");
    if (p == std::wstring::npos) return -1;
    std::wstring rest = wurl.substr(p + 3);
    size_t sl = rest.find(L'/');
    std::wstring host = sl == std::wstring::npos ? rest : rest.substr(0, sl);
    std::wstring path = sl == std::wstring::npos ? L"/" : rest.substr(sl);

    HINTERNET hS = WinHttpOpen(L"Mozilla/5.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hS) return -1;
    HINTERNET hC = WinHttpConnect(hS, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET hR = hC ? WinHttpOpenRequest(hC, L"GET", path.c_str(), NULL, NULL, NULL,
                                            WINHTTP_FLAG_SECURE) : NULL;
    if (!hR) { if (hC) WinHttpCloseHandle(hC); WinHttpCloseHandle(hS); return -1; }

    if (!WinHttpSendRequest(hR, NULL, 0, NULL, 0, 0, 0) ||
        !WinHttpReceiveResponse(hR, NULL)) {
        WinHttpCloseHandle(hR); WinHttpCloseHandle(hC); WinHttpCloseHandle(hS); return -1;
    }

    DWORD status = 0, statusSz = sizeof(status);
    WinHttpQueryHeaders(hR, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSz, WINHTTP_NO_HEADER_INDEX);
    if (status != 200) {
        std::cout << "\n" C_ERR "  [-] " C_WHITE "HTTP " << status << C_RST "\n";
        WinHttpCloseHandle(hR); WinHttpCloseHandle(hC); WinHttpCloseHandle(hS); return -1;
    }

    LONGLONG total = 0;
    wchar_t lenBuf[32] = {}; DWORD lenBufSz = sizeof(lenBuf);
    if (WinHttpQueryHeaders(hR, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX,
                            lenBuf, &lenBufSz, WINHTTP_NO_HEADER_INDEX))
        total = _wtoi64(lenBuf);

    // BCrypt AES-ECB for CTR keystream
    BCRYPT_ALG_HANDLE hAlg = NULL;
    BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, NULL, 0);
    BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_ECB,
                      sizeof(BCRYPT_CHAIN_MODE_ECB), 0);
    BCRYPT_KEY_HANDLE hKey = NULL;
    BCryptGenerateSymmetricKey(hAlg, &hKey, NULL, 0, (PUCHAR)keys.aes, 16, 0);

    const DWORD CHUNK = 1 << 20; // 1 MB
    std::vector<uint8_t> buf(CHUNK);
    DWORD bytesRead = 0;
    LONGLONG received = 0;
    LONGLONG lastTick = GetTickCount64(), lastBytes = 0;
    double speedMBs = 0.0;
    uint64_t blockOffset = 0;

    while (WinHttpReadData(hR, buf.data(), CHUNK, &bytesRead) && bytesRead > 0) {
        AesCtrDecrypt(hKey, keys.iv, blockOffset, buf.data(), bytesRead);
        DWORD written = 0;
        if (!WriteFile(hFile, buf.data(), bytesRead, &written, NULL) || written != bytesRead) {
            received = -1; break;
        }
        received += bytesRead;
        LONGLONG now = GetTickCount64(), dt = now - lastTick;
        if (dt >= 250) {
            speedMBs  = (double)(received - lastBytes) / (dt / 1000.0) / 1048576.0;
            lastTick  = now; lastBytes = received;
        }
        PrintProgress(received, total, speedMBs);
    }

    BCryptDestroyKey(hKey); BCryptCloseAlgorithmProvider(hAlg, 0);
    WinHttpCloseHandle(hR); WinHttpCloseHandle(hC); WinHttpCloseHandle(hS);
    return received;
}

// ── Zip extraction via PowerShell ─────────────────────────────────────────────
static bool ExtractZip(const wchar_t* zipPath, const wchar_t* destDir) {
    wchar_t cmd[MAX_PATH * 3];
    swprintf_s(cmd,
        L"powershell -NoProfile -NonInteractive -Command "
        L"\"Expand-Archive -LiteralPath '%s' -DestinationPath '%s' -Force\"",
        zipPath, destDir);
    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW,
                        NULL, NULL, &si, &pi)) return false;
    WaitForSingleObject(pi.hProcess, 120000);
    DWORD exit = 1;
    GetExitCodeProcess(pi.hProcess, &exit);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    return exit == 0;
}

// ── Main download entry ───────────────────────────────────────────────────────
static bool DownloadIrisCore() {
    // Fetch Mega credentials from Supabase config table
    std::cout << C_INFO "  [*] " C_WHITE "Checking for updates..." C_RST "\n";
    std::string megaId  = FetchConfig("iriscore_mega_id");
    std::string megaKey = FetchConfig("iriscore_mega_key");
    if (megaId.empty() || megaKey.empty()) {
        std::cout << C_ERR "  [-] " C_WHITE "Config not found on server" C_RST "\n";
        return false;
    }

    auto k32 = B64UrlDecode(megaKey);
    if (k32.size() < 32) {
        std::cout << C_ERR "  [-] " C_WHITE "Invalid Mega key" C_RST "\n";
        return false;
    }
    MegaKeys keys = MegaDeriveKeys(k32);

    std::cout << C_INFO "  [*] " C_WHITE "Fetching download link..." C_RST "\n";
    std::string dlUrl = MegaGetUrl(megaId);
    if (dlUrl.empty()) {
        std::cout << C_ERR "  [-] " C_WHITE "Failed to get Mega URL" C_RST "\n";
        return false;
    }

    // Always fresh — delete existing copy before download
    DeleteFileW(IRISCORE_TEMP_PATH);

    const wchar_t* tmpPath = L"C:\\Windows\\Temp\\iris_dl.tmp";
    HANDLE hFile = CreateFileW(tmpPath, GENERIC_WRITE, 0, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        std::cout << C_ERR "  [-] " C_WHITE "Failed to create temp file" C_RST "\n";
        return false;
    }

    std::cout << "\n" << BoxLine("IrisCore") << "\n";
    std::cout << "  " C_DIM "│" C_RST "\n";

    LONGLONG received = StreamMega(dlUrl, keys, hFile);

    std::cout << "\n  " C_DIM "│" C_RST "\n";
    std::cout << BoxBottom() << "\n";
    CloseHandle(hFile);

    if (received <= 0) {
        DeleteFileW(tmpPath);
        std::cout << C_ERR "  [-] " C_WHITE "Download failed" C_RST "\n";
        return false;
    }

    // Detect format: zip (PK) or exe (MZ)
    bool isZip = false;
    {
        HANDLE hC = CreateFileW(tmpPath, GENERIC_READ, FILE_SHARE_READ,
                                NULL, OPEN_EXISTING, 0, NULL);
        if (hC != INVALID_HANDLE_VALUE) {
            char magic[4] = {}; DWORD rd = 0;
            ReadFile(hC, magic, 4, &rd, NULL);
            CloseHandle(hC);
            isZip = (magic[0] == 'P' && magic[1] == 'K');
            if (!isZip && !(magic[0] == 'M' && magic[1] == 'Z')) {
                DeleteFileW(tmpPath);
                std::cout << C_ERR "  [-] " C_WHITE
                          "Corrupted file or wrong key" C_RST "\n";
                return false;
            }
        }
    }

    if (isZip) {
        std::cout << C_INFO "  [*] " C_WHITE "Extracting..." C_RST "\n";
        const wchar_t* zipPath = L"C:\\Windows\\Temp\\iris_dl.zip";
        MoveFileExW(tmpPath, zipPath, MOVEFILE_REPLACE_EXISTING);
        bool ok = ExtractZip(zipPath, L"C:\\Windows\\Temp\\");
        DeleteFileW(zipPath);
        if (!ok || GetFileAttributesW(IRISCORE_TEMP_PATH) == INVALID_FILE_ATTRIBUTES) {
            std::cout << C_ERR "  [-] " C_WHITE "Failed to extract zip" C_RST "\n";
            return false;
        }
    } else {
        MoveFileExW(tmpPath, IRISCORE_TEMP_PATH, MOVEFILE_REPLACE_EXISTING);
    }

    std::cout << C_OK "  [+] " C_WHITE "Download complete  "
              << C_DIM "(" C_BORDER << received / 1048576 << " MB" C_DIM ")" C_RST "\n";
    return true;
}
