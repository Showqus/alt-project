#include "inject.h"

#include <aclapi.h>
#include <sddl.h>
#include <tlhelp32.h>

#include <cwchar>

namespace launcher {
namespace {

std::wstring ErrorText(DWORD code) {
    wchar_t* buffer = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, code, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::wstring text = L"код " + std::to_wstring(code);
    if (buffer) {
        text += L": ";
        text += buffer;
        LocalFree(buffer);
        while (!text.empty() && (text.back() == L'\n' || text.back() == L'\r')) text.pop_back();
    }
    return text;
}

std::wstring FileName(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

}  // namespace

DWORD FindProcess(const std::wstring& exeName) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    DWORD pid = 0;
    for (BOOL ok = Process32FirstW(snapshot, &entry); ok; ok = Process32NextW(snapshot, &entry)) {
        if (_wcsicmp(entry.szExeFile, exeName.c_str()) == 0) {
            pid = entry.th32ProcessID;
            break;
        }
    }
    CloseHandle(snapshot);
    return pid;
}

double ProcessAgeSeconds(DWORD pid) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return 0.0;
    FILETIME created, exited, kernel, user, now;
    double age = 0.0;
    if (GetProcessTimes(process, &created, &exited, &kernel, &user)) {
        GetSystemTimeAsFileTime(&now);
        ULARGE_INTEGER a, b;
        a.LowPart = created.dwLowDateTime;
        a.HighPart = created.dwHighDateTime;
        b.LowPart = now.dwLowDateTime;
        b.HighPart = now.dwHighDateTime;
        if (b.QuadPart > a.QuadPart) age = static_cast<double>(b.QuadPart - a.QuadPart) / 1e7;
    }
    CloseHandle(process);
    return age;
}

std::wstring LoadedModDll(DWORD pid) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snapshot == INVALID_HANDLE_VALUE) return L"";
    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    std::wstring found;
    for (BOOL ok = Module32FirstW(snapshot, &entry); ok; ok = Module32NextW(snapshot, &entry)) {
        if (_wcsnicmp(entry.szModule, L"BedrockQoL", 10) == 0) {
            found = entry.szModule;
            break;
        }
    }
    CloseHandle(snapshot);
    return found;
}

bool GrantAppContainerAccess(const std::wstring& path) {
    PSID sid = nullptr;
    if (!ConvertStringSidToSidW(L"S-1-15-2-1", &sid)) return false;

    PACL oldDacl = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    DWORD result = GetNamedSecurityInfoW(path.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr,
                                         &oldDacl, nullptr, &descriptor);
    if (result == ERROR_SUCCESS) {
        EXPLICIT_ACCESSW access{};
        access.grfAccessPermissions = GENERIC_READ | GENERIC_EXECUTE;
        access.grfAccessMode = GRANT_ACCESS;
        access.grfInheritance = NO_INHERITANCE;
        access.Trustee.TrusteeForm = TRUSTEE_IS_SID;
        access.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
        access.Trustee.ptstrName = static_cast<LPWSTR>(sid);

        PACL newDacl = nullptr;
        result = SetEntriesInAclW(1, &access, oldDacl, &newDacl);
        if (result == ERROR_SUCCESS) {
            result = SetNamedSecurityInfoW(const_cast<LPWSTR>(path.c_str()), SE_FILE_OBJECT,
                                           DACL_SECURITY_INFORMATION, nullptr, nullptr, newDacl, nullptr);
            LocalFree(newDacl);
        }
        LocalFree(descriptor);
    }
    LocalFree(sid);
    SetLastError(result);
    return result == ERROR_SUCCESS;
}

bool InjectDll(DWORD pid, const std::wstring& dllPath, std::wstring& error) {
    HANDLE process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION |
                                     PROCESS_VM_WRITE | PROCESS_VM_READ,
                                 FALSE, pid);
    if (!process) {
        error = L"не удалось открыть процесс игры (" + ErrorText(GetLastError()) + L")";
        return false;
    }

    const SIZE_T bytes = (dllPath.size() + 1) * sizeof(wchar_t);
    void* remote = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    bool ok = false;
    if (remote && WriteProcessMemory(process, remote, dllPath.c_str(), bytes, nullptr)) {
        auto loadLibrary = reinterpret_cast<LPTHREAD_START_ROUTINE>(
            reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW")));
        HANDLE thread = CreateRemoteThread(process, nullptr, 0, loadLibrary, remote, 0, nullptr);
        if (thread) {
            WaitForSingleObject(thread, 15000);
            DWORD exitCode = 0;
            GetExitCodeThread(thread, &exitCode);
            CloseHandle(thread);
            // exitCode holds the low 32 bits of the HMODULE; 0 means LoadLibraryW failed.
            ok = exitCode != 0 || _wcsicmp(LoadedModDll(pid).c_str(), FileName(dllPath).c_str()) == 0;
            if (!ok) error = L"LoadLibraryW в игре вернул ошибку (нет прав на файл или DLL повреждена)";
        } else {
            error = L"CreateRemoteThread не удался (" + ErrorText(GetLastError()) + L")";
        }
    } else {
        error = L"не удалось записать путь к DLL в память игры (" + ErrorText(GetLastError()) + L")";
    }

    if (remote) VirtualFreeEx(process, remote, 0, MEM_RELEASE);
    CloseHandle(process);
    return ok;
}

}  // namespace launcher
