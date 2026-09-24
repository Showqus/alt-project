#pragma once

#include <windows.h>

// Writes fatal exceptions (access violation, illegal instruction...) that happen inside the DLL, or
// on a thread while it runs mod code marked with crashlog::Scope, to BedrockQoL.log with the module
// and offset. The game still crashes, but the log says where.
namespace crashlog {

void Install(HMODULE self);
void Uninstall();

// What the current thread is doing, e.g. "drawing the menu (Direct3D 12)". Nests.
class Scope {
public:
    explicit Scope(const char* what);
    ~Scope();
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

private:
    const char* previous_;
};

}  // namespace crashlog
