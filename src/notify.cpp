#include "notify.h"

#include <windows.h>

#include <cstdio>

#include "log.h"

namespace notify {
namespace {

std::wstring g_path;
SRWLOCK g_lock = SRWLOCK_INIT;

}  // namespace

void Init(const std::wstring& dataDirectory) {
    const std::wstring control = dataDirectory + L"\\control";
    CreateDirectoryW(control.c_str(), nullptr);
    g_path = control + L"\\notifications.txt";
    // Start empty; the launcher notices the file shrinking and resets its read position.
    if (FILE* f = _wfopen(g_path.c_str(), L"wb")) fclose(f);
}

void Send(const std::string& message) {
    // One notification per line; embedded newlines are escaped as "\n".
    std::string line;
    for (char c : message) {
        if (c == '\n') {
            line += "\\n";
        } else if (c != '\r') {
            line += c;
        }
    }

    logx::Info("[notify] %s", message.c_str());
    if (g_path.empty()) return;

    AcquireSRWLockExclusive(&g_lock);
    if (FILE* f = _wfopen(g_path.c_str(), L"ab")) {
        line += '\n';
        fwrite(line.data(), 1, line.size(), f);
        fclose(f);
    }
    ReleaseSRWLockExclusive(&g_lock);
}

}  // namespace notify
