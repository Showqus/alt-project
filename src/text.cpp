#include "text.h"

#include <windows.h>

namespace text {

std::wstring Widen(const std::string& utf8) {
    if (utf8.empty()) return {};
    const int len = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), &out[0], len);
    return out;
}

std::string Narrow(const std::wstring& utf16) {
    if (utf16.empty()) return {};
    const int len = WideCharToMultiByte(CP_UTF8, 0, utf16.data(), static_cast<int>(utf16.size()), nullptr, 0,
                                        nullptr, nullptr);
    std::string out(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, utf16.data(), static_cast<int>(utf16.size()), &out[0], len, nullptr, nullptr);
    return out;
}

std::string Trim(const std::string& s, const char* chars) {
    const size_t start = s.find_first_not_of(chars);
    if (start == std::string::npos) return {};
    const size_t end = s.find_last_not_of(chars);
    return s.substr(start, end - start + 1);
}

std::string Lower(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

std::vector<std::string> Split(const std::string& s) {
    std::vector<std::string> parts;
    size_t pos = 0;
    while (pos < s.size()) {
        const size_t start = s.find_first_not_of(" \t", pos);
        if (start == std::string::npos) break;
        const size_t end = s.find_first_of(" \t", start);
        parts.push_back(s.substr(start, end == std::string::npos ? std::string::npos : end - start));
        pos = end == std::string::npos ? s.size() : end;
    }
    return parts;
}

bool StartsWith(const std::string& s, const std::string& prefix) {
    return !prefix.empty() && s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

}  // namespace text
