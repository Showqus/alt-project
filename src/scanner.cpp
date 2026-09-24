#include "scanner.h"

#include <windows.h>

#include <cstdlib>
#include <cstring>
#include <vector>

namespace scanner {
namespace {

struct Pattern {
    std::vector<uint8_t> bytes;
    std::vector<bool> known;  // false = wildcard
};

bool Parse(const std::string& text, Pattern& out) {
    size_t pos = 0;
    while (pos < text.size()) {
        while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t')) ++pos;
        if (pos >= text.size()) break;
        const size_t stop = text.find_first_of(" \t", pos);
        const std::string token = text.substr(pos, stop == std::string::npos ? std::string::npos : stop - pos);
        pos = stop == std::string::npos ? text.size() : stop;

        if (token == "?" || token == "??") {
            out.bytes.push_back(0);
            out.known.push_back(false);
            continue;
        }
        if (token.size() != 2) return false;
        char* end = nullptr;
        const unsigned long value = std::strtoul(token.c_str(), &end, 16);
        if (!end || *end != '\0') return false;
        out.bytes.push_back(static_cast<uint8_t>(value));
        out.known.push_back(true);
    }
    return !out.bytes.empty();
}

}  // namespace

bool GetTextSection(Range& out) {
    auto* base = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
    if (!base) return false;

    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;

    IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        if (std::memcmp(section->Name, ".text", 5) == 0) {
            out.start = reinterpret_cast<uintptr_t>(base) + section->VirtualAddress;
            out.size = section->Misc.VirtualSize;
            return true;
        }
    }

    // No .text section name: fall back to the whole image.
    out.start = reinterpret_cast<uintptr_t>(base);
    out.size = nt->OptionalHeader.SizeOfImage;
    return true;
}

uintptr_t Find(const std::string& text, const Range& range) {
    Pattern p;
    if (!Parse(text, p) || range.size < p.bytes.size()) return 0;

    // Anchor the search on the first non-wildcard byte so memchr can do most of the work.
    size_t anchor = 0;
    while (anchor < p.bytes.size() && !p.known[anchor]) ++anchor;
    if (anchor == p.bytes.size()) return 0;

    const auto* begin = reinterpret_cast<const uint8_t*>(range.start);
    const size_t last = range.size - p.bytes.size();
    const uint8_t first = p.bytes[anchor];

    for (size_t i = 0; i <= last;) {
        const void* hit = std::memchr(begin + i + anchor, first, last - i + 1);
        if (!hit) break;
        i = static_cast<size_t>(static_cast<const uint8_t*>(hit) - begin) - anchor;

        bool match = true;
        for (size_t j = anchor + 1; j < p.bytes.size(); ++j) {
            if (p.known[j] && begin[i + j] != p.bytes[j]) {
                match = false;
                break;
            }
        }

        if (match) {
            const uintptr_t address = range.start + i;
            if (p.known[0] && (p.bytes[0] == 0xE8 || p.bytes[0] == 0xE9)) {
                int32_t rel = 0;
                std::memcpy(&rel, reinterpret_cast<const void*>(address + 1), sizeof(rel));
                return address + 5 + rel;
            }
            return address;
        }
        ++i;
    }
    return 0;
}

}  // namespace scanner
