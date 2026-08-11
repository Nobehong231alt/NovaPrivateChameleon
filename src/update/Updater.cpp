#include "Updater.h"
#include <windows.h>
#include <shellapi.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <string>
#include <vector>
#include <cctype>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "bcrypt.lib")

namespace NovaPrivateChameleon {

static const wchar_t* UPDATE_HOST  = L"raw.githubusercontent.com";
static const wchar_t* VER_PATH     = L"/Nobehong231alt/NovaPrivateChameleon/master/version.txt";
static const wchar_t* EXE_PATH_URL = L"/Nobehong231alt/NovaPrivateChameleon/master/NovaPrivateChameleon.exe";
static const wchar_t* HASH_PATH    = L"/Nobehong231alt/NovaPrivateChameleon/master/NovaPrivateChameleon.exe.sha256";

static std::vector<BYTE> HttpsGet(const wchar_t* path) {
    std::vector<BYTE> out;
    HINTERNET ses = WinHttpOpen(L"NPC-Updater/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses) return out;
    HINTERNET con = WinHttpConnect(ses, UPDATE_HOST, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!con) { WinHttpCloseHandle(ses); return out; }
    HINTERNET req = WinHttpOpenRequest(con, L"GET", path, nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!req) { WinHttpCloseHandle(con); WinHttpCloseHandle(ses); return out; }

    if (WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, nullptr, 0, 0, 0) &&
        WinHttpReceiveResponse(req, nullptr)) {
        DWORD avail = 0;
        while (WinHttpQueryDataAvailable(req, &avail) && avail > 0) {
            size_t base = out.size();
            out.resize(base + avail);
            DWORD got = 0;
            WinHttpReadData(req, out.data() + base, avail, &got);
            if (got < avail) out.resize(base + got);
        }
    }
    WinHttpCloseHandle(req);
    WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);
    return out;
}

static std::string Sha256Hex(const std::vector<BYTE>& data) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) return {};
    DWORD hlen = 0, cb = 0;
    BCryptGetProperty(alg, BCRYPT_HASH_LENGTH, (PUCHAR)&hlen, sizeof(hlen), &cb, 0);
    BCRYPT_HASH_HANDLE h = nullptr;
    std::string hex;
    if (BCryptCreateHash(alg, &h, nullptr, 0, nullptr, 0, 0) == 0) {
        BCryptHashData(h, (PUCHAR)data.data(), (ULONG)data.size(), 0);
        std::vector<BYTE> dig(hlen);
        if (BCryptFinishHash(h, dig.data(), hlen, 0) == 0) {
            static const char HEX[] = "0123456789abcdef";
            hex.resize(hlen * 2);
            for (DWORD i = 0; i < hlen; ++i) {
                hex[i * 2]     = HEX[dig[i] >> 4];
                hex[i * 2 + 1] = HEX[dig[i] & 0xF];
            }
        }
        BCryptDestroyHash(h);
    }
    BCryptCloseAlgorithmProvider(alg, 0);
    return hex;
}

static std::string Trim(std::string s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' '))
        s.pop_back();
    return s;
}

static std::string CurrentExePath() {
    char buf[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    return buf;
}

static std::string TempPath(const std::string& name) {
    char tmp[MAX_PATH] = {};
    GetTempPathA(MAX_PATH, tmp);
    return std::string(tmp) + name;
}

bool CheckAndApplyUpdate() {
    try {
        // 1. Fetch version number
        auto verBytes = HttpsGet(VER_PATH);
        if (verBytes.empty()) return false;
        std::string verStr = Trim(std::string(verBytes.begin(), verBytes.end()));
        int serverVer = 0;
        try { serverVer = std::stoi(verStr); } catch (...) { return false; }
        if (serverVer <= NPC_VERSION) return false;

        // 2. Fetch expected SHA256
        auto hashBytes = HttpsGet(HASH_PATH);
        if (hashBytes.empty()) return false;
        std::string expectedHash = Trim(std::string(hashBytes.begin(), hashBytes.end()));
        for (auto& c : expectedHash) c = (char)tolower((unsigned char)c);
        if (expectedHash.size() != 64) return false;

        // 3. Download new exe
        auto exeBytes = HttpsGet(EXE_PATH_URL);
        if (exeBytes.size() < 65536) return false;

        // 4. Verify hash
        if (Sha256Hex(exeBytes) != expectedHash) return false;

        // 5. Write new exe to temp
        std::string newExe = TempPath("NovaPrivateChameleon_new.exe");
        std::string oldExe = TempPath("NovaPrivateChameleon_old.exe");
        std::string batPath = TempPath("nova_npc_update.bat");
        std::string curExe  = CurrentExePath();

        FILE* f = fopen(newExe.c_str(), "wb");
        if (!f) return false;
        fwrite(exeBytes.data(), 1, exeBytes.size(), f);
        fclose(f);

        // 6. Write self-replace batch (mirrors spoofer pattern)
        std::string bat =
            "@echo off\r\n"
            "timeout /t 2 /nobreak >nul\r\n"
            "move /y \"" + curExe + "\" \"" + oldExe + "\" >nul 2>&1\r\n"
            "copy /y \"" + newExe + "\" \"" + curExe + "\" >nul 2>&1\r\n"
            "del \"" + newExe + "\" >nul 2>&1\r\n"
            "start \"\" \"" + curExe + "\"\r\n"
            "del \"%~f0\"\r\n";

        f = fopen(batPath.c_str(), "w");
        if (!f) return false;
        fputs(bat.c_str(), f);
        fclose(f);

        // 7. Launch bat hidden and signal caller to exit
        ShellExecuteA(nullptr, "open", batPath.c_str(), nullptr, nullptr, SW_HIDE);
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace NovaPrivateChameleon
