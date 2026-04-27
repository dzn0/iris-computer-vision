#pragma once

#include <string>
#include <windows.h>
#include <winhttp.h>
#include <iostream>
#include "config.h"

#pragma comment(lib, "winhttp.lib")

// Color defines (same as TokenScreen.h to avoid circular include)
#define C_RST     "\033[0m"
#define C_OK      "\033[38;5;141m"
#define C_ERR     "\033[38;5;197m"
#define C_INFO    "\033[38;5;111m"

// Simple JSON key extractor
static std::string ExtractJsonValue(const std::string& json, const std::string& key) {
    std::string searchKey = "\"" + key + "\":";
    size_t pos = json.find(searchKey);
    if (pos == std::string::npos) return "";

    pos += searchKey.length();
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\n' || json[pos] == '\t')) pos++;

    if (pos >= json.length()) return "";

    if (json[pos] == '"') {
        pos++;
        std::string value;
        while (pos < json.length() && json[pos] != '"') {
            value += json[pos];
            pos++;
        }
        return value;
    }

    return "";
}

// Validate token via Supabase REST API
static bool ValidateTokenWithSupabase(const std::string& token) {
    HINTERNET hSession = NULL;
    HINTERNET hConnect = NULL;
    HINTERNET hRequest = NULL;

    // Create session
    hSession = WinHttpOpen(L"Iris/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    if (!hSession) return false;

    // Connect to host
    hConnect = WinHttpConnect(hSession, SUPABASE_HOST, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return false;
    }

    std::wstring tokenWide(token.begin(), token.end());

    // Build ISO 8601 UTC timestamp for expires_at filter
    SYSTEMTIME st{};
    GetSystemTime(&st);
    wchar_t nowIso[32]{};
    swprintf_s(nowIso, L"%04d-%02d-%02dT%02d:%02d:%02dZ",
               st.wYear, st.wMonth, st.wDay,
               st.wHour, st.wMinute, st.wSecond);

    // Supabase returns [] if: token not found, expired, or inactive
    std::wstring path = L"/rest/v1/tokens?token=eq." + tokenWide
                      + L"&active=eq.true"
                      + L"&expires_at=gte." + nowIso;

    // Create request
    hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(), NULL, NULL, NULL, WINHTTP_FLAG_SECURE);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    // Add headers: apikey for authentication
    std::wstring apiKeyHeader = L"apikey: ";
    apiKeyHeader += SUPABASE_ANON_KEY;
    WinHttpAddRequestHeaders(hRequest, apiKeyHeader.c_str(), (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);
    WinHttpAddRequestHeaders(hRequest, L"Content-Type: application/json", (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);

    // Send request
    if (!WinHttpSendRequest(hRequest, NULL, 0, NULL, 0, 0, 0)) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    // Wait for response
    if (!WinHttpReceiveResponse(hRequest, NULL)) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    // Read response
    std::string response;
    DWORD dwSize = 0;
    bool result = false;

    while (WinHttpQueryDataAvailable(hRequest, &dwSize)) {
        if (dwSize == 0) break;

        char buffer[4096];
        DWORD dwRead = 0;
        if (!WinHttpReadData(hRequest, buffer, min(dwSize, (DWORD)(sizeof(buffer) - 1)), &dwRead)) break;

        response.append(buffer, dwRead);
        dwSize -= dwRead;
    }

    // Check if token is in response
    // Supabase returns empty array [] if no match, or [{...}] if found
    if (response.find("[]") != std::string::npos) {
        // Token not found - this is expected for invalid tokens
        result = false;
    } else if (response.find("[{") != std::string::npos) {
        // Token found and valid
        result = true;
    } else {
        // Empty response or connection issue
        result = false;
    }

    // Cleanup
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    return result;
}
