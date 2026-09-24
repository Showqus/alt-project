// Loads BedrockQoL.dll into Minecraft Bedrock (Minecraft.Windows.exe).
//
// Usage: BedrockQoLInjector.exe [path\to\BedrockQoL.dll] [--no-pause]
// Without a path, BedrockQoL.dll next to the injector is used.

#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#include <shellapi.h>
#include <tlhelp32.h>

#include <cwchar>
#include <string>

namespace {

const wchar_t kProcessName[] = L"Minecraft.Windows.exe";
const wchar_t kDefaultDll[] = L"BedrockQoL.dll";

void Print(const std::wstring& text) {
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD written = 0;
    const std::wstring line = text + L"\r\n";
    if (!WriteConsoleW(out, line.c_str(), static_cast<DWORD>(line.size()), &written, nullptr)) {
        // Redirected output: write UTF-8.
        const int len = WideCharToMultiByte(CP_UTF8, 0, line.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string utf8(static_cast<size_t>(len > 0 ? len - 1 : 0), '\0');
        if (len > 1) WideCharToMultiByte(CP_UTF8, 0, line.c_str(), -1, &utf8[0], len, nullptr, nullptr);
        WriteFile(out, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    }
}

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

std::wstring ExeDirectory() {
    wchar_t path[MAX_PATH];
    const DWORD len = GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring dir(path, len);
    const size_t slash = dir.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : dir.substr(0, slash);
}

std::wstring FileName(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

// UWP apps run in an AppContainer and can only load files that "ALL APPLICATION PACKAGES"
// (S-1-15-2-1) is allowed to read and execute.
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

DWORD FindProcess(const wchar_t* name) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    DWORD pid = 0;
    for (BOOL ok = Process32FirstW(snapshot, &entry); ok; ok = Process32NextW(snapshot, &entry)) {
        if (_wcsicmp(entry.szExeFile, name) == 0) {
            pid = entry.th32ProcessID;
            break;
        }
    }
    CloseHandle(snapshot);
    return pid;
}

bool IsModuleLoaded(DWORD pid, const std::wstring& moduleName) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snapshot == INVALID_HANDLE_VALUE) return false;
    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    bool found = false;
    for (BOOL ok = Module32FirstW(snapshot, &entry); ok; ok = Module32NextW(snapshot, &entry)) {
        if (_wcsicmp(entry.szModule, moduleName.c_str()) == 0) {
            found = true;
            break;
        }
    }
    CloseHandle(snapshot);
    return found;
}

bool Inject(DWORD pid, const std::wstring& dllPath) {
    HANDLE process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION |
                                     PROCESS_VM_WRITE | PROCESS_VM_READ,
                                 FALSE, pid);
    if (!process) {
        Print(L"Не удалось открыть процесс игры (" + ErrorText(GetLastError()) + L")");
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
            // exitCode holds the low 32 bits of the HMODULE, 0 means LoadLibraryW failed.
            ok = exitCode != 0 || IsModuleLoaded(pid, FileName(dllPath));
            if (!ok) Print(L"LoadLibraryW внутри игры вернул ошибку (нет прав на файл или DLL повреждена).");
        } else {
            Print(L"CreateRemoteThread не удался (" + ErrorText(GetLastError()) + L")");
        }
    } else {
        Print(L"Не удалось записать путь к DLL в память игры (" + ErrorText(GetLastError()) + L")");
    }

    if (remote) VirtualFreeEx(process, remote, 0, MEM_RELEASE);
    CloseHandle(process);
    return ok;
}

}  // namespace

int main() {
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);

    std::wstring dllArg;
    bool pause = true;
    for (int i = 1; i < argc; ++i) {
        if (_wcsicmp(argv[i], L"--no-pause") == 0) {
            pause = false;
        } else {
            dllArg = argv[i];
        }
    }
    if (argv) LocalFree(argv);

    if (dllArg.empty()) dllArg = ExeDirectory() + L"\\" + kDefaultDll;

    wchar_t fullPath[MAX_PATH];
    if (!GetFullPathNameW(dllArg.c_str(), MAX_PATH, fullPath, nullptr)) {
        Print(L"Неверный путь к DLL: " + dllArg);
        return 1;
    }
    const std::wstring dllPath = fullPath;

    int exitCode = 1;
    if (GetFileAttributesW(dllPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        Print(L"Файл не найден: " + dllPath);
    } else {
        if (!GrantAppContainerAccess(dllPath)) {
            Print(L"Предупреждение: не удалось выдать права ALL APPLICATION PACKAGES (" +
                  ErrorText(GetLastError()) + L"). Инжект может не сработать.");
        }

        DWORD pid = FindProcess(kProcessName);
        if (!pid) Print(L"Жду запуска Minecraft (Minecraft.Windows.exe)... Закройте окно для отмены.");
        while (!pid) {
            Sleep(1000);
            pid = FindProcess(kProcessName);
        }

        if (IsModuleLoaded(pid, FileName(dllPath))) {
            Print(L"DLL уже загружена в игру. Чтобы перезагрузить, нажмите в игре END и запустите инжектор снова.");
            exitCode = 0;
        } else if (Inject(pid, dllPath)) {
            Print(L"Готово! BedrockQoL загружен (PID " + std::to_wstring(pid) + L").");
            Print(L"AutoSprint: F8 вкл/выкл, Zoom: удерживайте C, колесо мыши - сила зума, END - выгрузить.");
            exitCode = 0;
        }
    }

    if (pause) {
        Print(L"\nНажмите Enter для выхода...");
        HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
        FlushConsoleInputBuffer(in);
        wchar_t buffer[8];
        DWORD read = 0;
        ReadConsoleW(in, buffer, 1, &read, nullptr);
    }
    return exitCode;
}
