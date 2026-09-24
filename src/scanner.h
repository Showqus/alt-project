#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace scanner {

struct Range {
    uintptr_t start = 0;
    size_t size = 0;
    bool Contains(uintptr_t address) const { return address >= start && address < start + size; }
};

// .text section of the game executable.
bool GetTextSection(Range& out);

// Finds the first match of an IDA-style pattern ("48 8B ? ?? 05") inside `range`.
// If the first token of the pattern is a literal E8 (call) or E9 (jmp), the rel32 operand
// is followed and the call target is returned instead of the match address.
// Returns 0 if the pattern is malformed or not found.
uintptr_t Find(const std::string& pattern, const Range& range);

}  // namespace scanner
