#pragma once

#include <string>
#include <vector>

// Small string helpers (UTF-8 <-> UTF-16, trimming, splitting).
namespace text {

std::wstring Widen(const std::string& utf8);
std::string Narrow(const std::wstring& utf16);
std::string Trim(const std::string& s, const char* chars);
std::string Lower(std::string s);  // ASCII only
std::vector<std::string> Split(const std::string& s);  // on whitespace
bool StartsWith(const std::string& s, const std::string& prefix);

}  // namespace text
