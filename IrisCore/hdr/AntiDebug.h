#pragma once
#include <windows.h>
#include <intrin.h>
#include <ntstatus.h>
#include <cstdint>
#include "ObfStr.h"

#ifndef NT_SUCCESS
#define NT_SUCCESS(s) (((NTSTATUS)(s)) >= 0)
#endif
typedef LONG NTSTATUS;

// ── Obfuscated exit — harder to NOP than a direct call ───────────────────────
#define AD_DIE() do {                                           \
    void(WINAPI*_fn)(UINT) = &ExitProcess;                     \
    _fn(0);                                                     \
} while(0)

// ─────────────────────────────────────────────────────────────────────────────
// CRC32 & .text integrity state
// ─────────────────────────────────────────────────────────────────────────────
static constexpr uint32_t AD_CRC_KEY = 0xA3F1C2D7u;
static volatile uint32_t  g_ad_crc   = 0;

static uint32_t AD_Crc32(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++)
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1)));
    }
    return ~crc;
}

static bool AD_GetTextSection(const uint8_t*& base, size_t& size) {
    HMODULE hMod = GetModuleHandleW(NULL);
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(hMod);
    auto nt  = reinterpret_cast<const IMAGE_NT_HEADERS*>(
                    reinterpret_cast<const uint8_t*>(hMod) + dos->e_lfanew);
    auto sec = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++) {
        if (sec->Characteristics & IMAGE_SCN_CNT_CODE) {
            base = reinterpret_cast<const uint8_t*>(hMod) + sec->VirtualAddress;
            size = sec->Misc.VirtualSize;
            return true;
        }
    }
    return false;
}

// ── Helper: resolve ntdll export by obfuscated name ──────────────────────────
static FARPROC AD_GetNtProc(const char* name) {
    HMODULE ntdll = GetModuleHandleW(OBFWSTR(L"ntdll.dll"));
    if (!ntdll) return nullptr;
    return GetProcAddress(ntdll, name);
}

// ── 1. IsDebuggerPresent ──────────────────────────────────────────────────────
static bool AD_IsDebuggerPresent() {
    return IsDebuggerPresent() != FALSE;
}

// ── 2. CheckRemoteDebuggerPresent ────────────────────────────────────────────
static bool AD_RemoteDebugger() {
    BOOL present = FALSE;
    CheckRemoteDebuggerPresent(GetCurrentProcess(), &present);
    return present != FALSE;
}

// ── 3. NtQueryInformationProcess — ProcessDebugPort ──────────────────────────
static bool AD_NtDebugPort() {
    using FnNtQIP = NTSTATUS(NTAPI*)(HANDLE, UINT, PVOID, ULONG, PULONG);
    auto NtQIP = reinterpret_cast<FnNtQIP>(
        AD_GetNtProc(OBFSTR("NtQueryInformationProcess")));
    if (!NtQIP) return false;
    HANDLE port = nullptr;
    NTSTATUS st = NtQIP(GetCurrentProcess(), 7, &port, sizeof(port), nullptr);
    return NT_SUCCESS(st) && port != nullptr;
}

// ── 4. NtQueryInformationProcess — ProcessDebugFlags ─────────────────────────
static bool AD_NtDebugFlags() {
    using FnNtQIP = NTSTATUS(NTAPI*)(HANDLE, UINT, PVOID, ULONG, PULONG);
    auto NtQIP = reinterpret_cast<FnNtQIP>(
        AD_GetNtProc(OBFSTR("NtQueryInformationProcess")));
    if (!NtQIP) return false;
    DWORD flags = 1;
    NTSTATUS st = NtQIP(GetCurrentProcess(), 0x1F, &flags, sizeof(flags), nullptr);
    return NT_SUCCESS(st) && flags == 0;
}

// ── 5. RDTSC timing check ─────────────────────────────────────────────────────
static bool AD_TimingCheck() {
    unsigned __int64 t1 = __rdtsc();
    volatile int x = 0;
    for (int i = 0; i < 100; i++) x += i;
    unsigned __int64 t2 = __rdtsc();
    return (t2 - t1) > 2000000ULL;
}

// ── 6. Heap flags (NtGlobalFlag in PEB) ──────────────────────────────────────
static bool AD_HeapFlags() {
#ifdef _WIN64
    BYTE* peb = (BYTE*)__readgsqword(0x60);
    DWORD ntGlobalFlag = *reinterpret_cast<DWORD*>(peb + 0xBC);
#else
    BYTE* peb = (BYTE*)__readfsdword(0x30);
    DWORD ntGlobalFlag = *reinterpret_cast<DWORD*>(peb + 0x68);
#endif
    return (ntGlobalFlag & 0x70) != 0;
}

// ── 7. Parent process SeDebugPrivilege check ──────────────────────────────────
static bool AD_SuspiciousParent() {
    using FnNtQIP = NTSTATUS(NTAPI*)(HANDLE, UINT, PVOID, ULONG, PULONG);
    auto NtQIP = reinterpret_cast<FnNtQIP>(
        AD_GetNtProc(OBFSTR("NtQueryInformationProcess")));
    if (!NtQIP) return false;

    struct PBI { ULONG_PTR res; PVOID peb; ULONG_PTR affinity; ULONG_PTR priority; ULONG_PTR pid; ULONG_PTR ppid; };
    PBI pbi = {};
    if (!NT_SUCCESS(NtQIP(GetCurrentProcess(), 0, &pbi, sizeof(pbi), nullptr)))
        return false;

    HANDLE hParent = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
                                  FALSE, (DWORD)pbi.ppid);
    if (!hParent) return false;

    HANDLE hToken = nullptr;
    bool suspicious = false;
    if (OpenProcessToken(hParent, TOKEN_QUERY, &hToken)) {
        DWORD len = 0;
        GetTokenInformation(hToken, TokenPrivileges, nullptr, 0, &len);
        if (len > 0) {
            auto buf = (TOKEN_PRIVILEGES*)_malloca(len);
            if (buf && GetTokenInformation(hToken, TokenPrivileges, buf, len, &len)) {
                for (DWORD i = 0; i < buf->PrivilegeCount; i++) {
                    LUID seDebug;
                    if (LookupPrivilegeValueW(nullptr, OBFWSTR(L"SeDebugPrivilege"), &seDebug)) {
                        if (buf->Privileges[i].Luid.LowPart == seDebug.LowPart &&
                            buf->Privileges[i].Luid.HighPart == seDebug.HighPart &&
                            (buf->Privileges[i].Attributes & SE_PRIVILEGE_ENABLED)) {
                            suspicious = true;
                            break;
                        }
                    }
                }
            }
            if (buf) _freea(buf);
        }
        CloseHandle(hToken);
    }
    CloseHandle(hParent);
    return suspicious;
}

// ── 8. Anti-attach — hide thread from debugger ───────────────────────────────
static void AD_HideThread(HANDLE hThread = NULL) {
    using FnNtSIT = NTSTATUS(NTAPI*)(HANDLE, UINT, PVOID, ULONG);
    auto NtSIT = reinterpret_cast<FnNtSIT>(
        AD_GetNtProc(OBFSTR("NtSetInformationThread")));
    if (!NtSIT) return;
    NtSIT(hThread ? hThread : GetCurrentThread(), 0x11, NULL, 0);
}

// ── 9. Integrity check — CRC32 of .text section ──────────────────────────────
static void AD_IntegrityInit() {
    const uint8_t* base; size_t sz;
    if (!AD_GetTextSection(base, sz)) return;
    g_ad_crc = AD_Crc32(base, sz) ^ AD_CRC_KEY;
}

static bool AD_IntegrityCheck() {
    if (g_ad_crc == 0) return false;
    const uint8_t* base; size_t sz;
    if (!AD_GetTextSection(base, sz)) return false;
    return AD_Crc32(base, sz) != (g_ad_crc ^ AD_CRC_KEY);
}

// ── Initialize — call once, very first ───────────────────────────────────────
static void AntiDebugInit() {
    AD_HideThread();
    AD_IntegrityInit();
}

// ── Run all checks ────────────────────────────────────────────────────────────
static void AntiDebugCheck() {
    if (AD_IsDebuggerPresent())  AD_DIE();
    if (AD_RemoteDebugger())     AD_DIE();
    if (AD_NtDebugPort())        AD_DIE();
    if (AD_NtDebugFlags())       AD_DIE();
    if (AD_HeapFlags())          AD_DIE();
    if (AD_TimingCheck())        AD_DIE();
    if (AD_IntegrityCheck())     AD_DIE();
#ifndef _DEBUG
    if (AD_SuspiciousParent())   AD_DIE();
#endif
}
