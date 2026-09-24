#pragma once

#include <string>

namespace launcher {

// manifest.txt published next to the DLL in the GitHub release:
//   version=57-1a2b3c4
//   file=BedrockQoL.dll
//   sha256=<hex>
struct Manifest {
    std::string version;
    std::string file;
    std::string sha256;
};

bool HttpGet(const std::wstring& url, std::string& body, std::wstring& error);
bool ParseManifest(const std::string& text, Manifest& out);
std::string Sha256Hex(const std::string& data);

// "https://host/a/b/manifest.txt" + "BedrockQoL.dll" -> "https://host/a/b/BedrockQoL.dll".
std::wstring ResolveUrl(const std::wstring& base, const std::wstring& relative);

}  // namespace launcher
