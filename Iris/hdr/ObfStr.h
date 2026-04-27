#pragma once
#include <cstdint>

// ── Compile-time XOR string obfuscation (C++17) ───────────────────────────────
// Usage:  OBFSTR("NtQueryInformationProcess")   → const char*
//         OBFWSTR(L"ntdll.dll")                 → const wchar_t*
//
// The plaintext string never appears in .rdata / .data sections.
// Key mixes __TIME__ so it differs per build.
// The temporary object lives on the stack for the duration of the expression.

namespace obf {

static constexpr uint8_t KEY =
    static_cast<uint8_t>((__TIME__[0] ^ __TIME__[3] ^ __TIME__[6]) * 0x6Bu + 0x3Du);

// ── char ─────────────────────────────────────────────────────────────────────
template<size_t N>
struct ObfStr {
    char buf[N];

    constexpr ObfStr(const char(&s)[N]) : buf{} {
        for (size_t i = 0; i < N; i++)
            buf[i] = static_cast<char>(static_cast<uint8_t>(s[i]) ^ static_cast<uint8_t>(KEY + i));
    }

    // Decrypt in-place at runtime and return pointer.
    // Safe to use as a function argument — lifetime covers the full expression.
    const char* str() const {
        char* p = const_cast<char*>(buf);
        for (size_t i = 0; i < N - 1; i++)
            p[i] = static_cast<char>(static_cast<uint8_t>(p[i]) ^ static_cast<uint8_t>(KEY + i));
        p[N - 1] = '\0';
        return buf;
    }
};

// ── wchar_t ───────────────────────────────────────────────────────────────────
template<size_t N>
struct ObfWStr {
    wchar_t buf[N];

    constexpr ObfWStr(const wchar_t(&s)[N]) : buf{} {
        for (size_t i = 0; i < N; i++)
            buf[i] = static_cast<wchar_t>(static_cast<uint16_t>(s[i]) ^ static_cast<uint8_t>(KEY + i));
    }

    const wchar_t* str() const {
        wchar_t* p = const_cast<wchar_t*>(buf);
        for (size_t i = 0; i < N - 1; i++)
            p[i] = static_cast<wchar_t>(static_cast<uint16_t>(p[i]) ^ static_cast<uint8_t>(KEY + i));
        p[N - 1] = L'\0';
        return buf;
    }
};

} // namespace obf

#define OBFSTR(s)  (obf::ObfStr<sizeof(s)>{s}.str())
#define OBFWSTR(s) (obf::ObfWStr<sizeof(s)/sizeof(wchar_t)>{s}.str())
