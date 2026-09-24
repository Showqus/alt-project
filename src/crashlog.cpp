#include "crashlog.h"

#include <atomic>
#include <cstdio>
#include <string>

#include "log.h"

namespace crashlog {
namespace {

HMODULE g_self = nullptr;
uintptr_t g_codeBegin = 0;  // the DLL's executable sections
uintptr_t g_codeEnd = 0;
PVOID g_handler = nullptr;
std::atomic<int> g_reports{0};
thread_local const char* t_scope = nullptr;

bool Fatal(DWORD code) {
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION:
        case EXCEPTION_ILLEGAL_INSTRUCTION:
        case EXCEPTION_PRIV_INSTRUCTION:
        case EXCEPTION_STACK_OVERFLOW:
        case EXCEPTION_INT_DIVIDE_BY_ZERO:
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
        case EXCEPTION_IN_PAGE_ERROR:
        case EXCEPTION_DATATYPE_MISALIGNMENT: return true;
        default: return false;
    }
}

// "BedrockQoL.dll+0x1234" for an address inside a loaded module.
std::string Where(const void* address) {
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            static_cast<LPCWSTR>(address), &module) ||
        !module) {
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "0x%p", address);
        return buffer;
    }
    char path[MAX_PATH] = {};
    GetModuleFileNameA(module, path, MAX_PATH);
    const char* name = path;
    for (const char* p = path; *p; ++p) {
        if (*p == '\\' || *p == '/') name = p + 1;
    }
    char buffer[MAX_PATH + 32];
    snprintf(buffer, sizeof(buffer), "%s+0x%llX", name,
             static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(address) - reinterpret_cast<uintptr_t>(module)));
    return buffer;
}

bool InSelfCode(const void* address) {
    const auto a = reinterpret_cast<uintptr_t>(address);
    return a >= g_codeBegin && a < g_codeEnd;
}

bool InSelf(const void* address) {
    HMODULE module = nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              static_cast<LPCWSTR>(address), &module) &&
           module == g_self;
}

LONG CALLBACK Handler(EXCEPTION_POINTERS* info) {
    const EXCEPTION_RECORD* record = info->ExceptionRecord;
    if (!Fatal(record->ExceptionCode)) return EXCEPTION_CONTINUE_SEARCH;
    const bool ours = InSelf(record->ExceptionAddress) || t_scope != nullptr;
    if (!ours || g_reports.fetch_add(1) >= 5) return EXCEPTION_CONTINUE_SEARCH;

    std::string detail;
    if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2) {
        const ULONG_PTR kind = record->ExceptionInformation[0];
        char buffer[96];
        snprintf(buffer, sizeof(buffer), ", %s address 0x%llX",
                 kind == 0 ? "reading" : kind == 1 ? "writing" : "executing",
                 static_cast<unsigned long long>(record->ExceptionInformation[1]));
        detail = buffer;
    }
    logx::Error("CRASH: exception 0x%08lX at %s%s, while %s", static_cast<unsigned long>(record->ExceptionCode),
                Where(record->ExceptionAddress).c_str(), detail.c_str(), t_scope ? t_scope : "running mod code");

    // Return addresses of mod code still on the stack (a rough call stack for the offsets above).
#if defined(_M_X64) || defined(__x86_64__)
    const auto* stack = reinterpret_cast<const uintptr_t*>(info->ContextRecord->Rsp);
    MEMORY_BASIC_INFORMATION mbi{};
    size_t count = 0;
    if (VirtualQuery(stack, &mbi, sizeof(mbi)) && mbi.State == MEM_COMMIT) {
        const uintptr_t end = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
        count = (end - reinterpret_cast<uintptr_t>(stack)) / sizeof(uintptr_t);
        if (count > 1024) count = 1024;
    }
    std::string trail;
    int found = 0;
    for (size_t i = 0; i < count && found < 12; ++i) {
        const void* value = reinterpret_cast<const void*>(stack[i]);
        if (InSelfCode(value)) {
            trail += " " + Where(value);
            ++found;
        }
    }
    if (!trail.empty()) logx::Error("CRASH: mod code on the stack:%s", trail.c_str());
#endif
    return EXCEPTION_CONTINUE_SEARCH;
}

}  // namespace

void Install(HMODULE self) {
    g_self = self;
    const auto base = reinterpret_cast<uintptr_t>(self);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        if (!(section->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        const uintptr_t begin = base + section->VirtualAddress;
        const uintptr_t end = begin + section->Misc.VirtualSize;
        if (!g_codeBegin || begin < g_codeBegin) g_codeBegin = begin;
        if (end > g_codeEnd) g_codeEnd = end;
    }
    if (!g_handler) g_handler = AddVectoredExceptionHandler(1, Handler);
}

void Uninstall() {
    if (g_handler) RemoveVectoredExceptionHandler(g_handler);
    g_handler = nullptr;
}

Scope::Scope(const char* what) : previous_(t_scope) { t_scope = what; }
Scope::~Scope() { t_scope = previous_; }

}  // namespace crashlog
