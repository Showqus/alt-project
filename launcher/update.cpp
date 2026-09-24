#include "update.h"

#include <windows.h>
#include <bcrypt.h>
#include <winhttp.h>

#include <vector>

#include "common.h"

namespace launcher {
namespace {

// Not in older SDK headers.
#ifndef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
#define WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY 4
#endif

std::string Trim(const std::string& s) {
    const size_t start = s.find_first_not_of(" \t\r\n\xEF\xBB\xBF");
    if (start == std::string::npos) return {};
    const size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

}  // namespace

bool HttpGet(const std::wstring& url, std::string& body, std::wstring& error) {
    body.clear();

    URL_COMPONENTSW parts{};
    parts.dwStructSize = sizeof(parts);
    wchar_t host[256] = {};
    wchar_t path[2048] = {};
    parts.lpszHostName = host;
    parts.dwHostNameLength = 256;
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = 2048;
    wchar_t extra[1024] = {};
    parts.lpszExtraInfo = extra;
    parts.dwExtraInfoLength = 1024;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts)) {
        error = L"неверный URL: " + url;
        return false;
    }
    const std::wstring object = std::wstring(path) + extra;
    const bool https = parts.nScheme == INTERNET_SCHEME_HTTPS;

    HINTERNET session = WinHttpOpen(L"BedrockQoL-Launcher/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        session = WinHttpOpen(L"BedrockQoL-Launcher/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                              WINHTTP_NO_PROXY_BYPASS, 0);
    }
    if (!session) {
        error = L"WinHttpOpen: " + std::to_wstring(GetLastError());
        return false;
    }
    WinHttpSetTimeouts(session, 10000, 10000, 15000, 30000);

    bool ok = false;
    HINTERNET connection = WinHttpConnect(session, host, parts.nPort, 0);
    HINTERNET request = connection ? WinHttpOpenRequest(connection, L"GET", object.c_str(), nullptr,
                                                        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                        https ? WINHTTP_FLAG_SECURE : 0)
                                   : nullptr;
    if (request && WinHttpSendRequest(request, L"Cache-Control: no-cache\r\n", static_cast<DWORD>(-1L),
                                      WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(request, nullptr)) {
        DWORD status = 0;
        DWORD size = sizeof(status);
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
        if (status == 200) {
            std::vector<char> buffer(65536);
            DWORD read = 0;
            while (WinHttpReadData(request, buffer.data(), static_cast<DWORD>(buffer.size()), &read) && read > 0) {
                body.append(buffer.data(), read);
            }
            ok = true;
        } else {
            error = L"HTTP " + std::to_wstring(status);
        }
    } else {
        error = L"сетевая ошибка " + std::to_wstring(GetLastError());
    }

    if (request) WinHttpCloseHandle(request);
    if (connection) WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return ok;
}

bool ParseManifest(const std::string& text, Manifest& out) {
    out = Manifest{};
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        const std::string line = Trim(text.substr(pos, end - pos));
        pos = end + 1;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = Trim(line.substr(0, eq));
        const std::string value = Trim(line.substr(eq + 1));
        if (key == "version") out.version = value;
        if (key == "file") out.file = value;
        if (key == "sha256") out.sha256 = value;
    }
    // The file name ends up in a local path: no directories allowed.
    if (out.file.empty() || out.file.find_first_of("\\/:") != std::string::npos) return false;
    if (out.version.empty() || out.version.find_first_of("\\/:*?\"<>| ") != std::string::npos) return false;
    return out.sha256.size() == 64;
}

std::string Sha256Hex(const std::string& data) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    unsigned char digest[32] = {};
    std::string hex;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0) {
        if (BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) == 0) {
            if (BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char*>(data.data())),
                               static_cast<ULONG>(data.size()), 0) == 0 &&
                BCryptFinishHash(hash, digest, sizeof(digest), 0) == 0) {
                static const char* kHex = "0123456789abcdef";
                for (unsigned char b : digest) {
                    hex += kHex[b >> 4];
                    hex += kHex[b & 15];
                }
            }
            BCryptDestroyHash(hash);
        }
        BCryptCloseAlgorithmProvider(alg, 0);
    }
    return hex;
}

std::wstring ResolveUrl(const std::wstring& base, const std::wstring& relative) {
    if (relative.find(L"://") != std::wstring::npos) return relative;
    const size_t slash = base.find_last_of(L'/');
    return slash == std::wstring::npos ? relative : base.substr(0, slash + 1) + relative;
}

}  // namespace launcher
