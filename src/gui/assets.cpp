#include "assets.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "../config.h"
#include "../log.h"
#include "../text.h"
#include "imgui_internal.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_TGA
#define STBI_ONLY_GIF
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wsign-compare"
#elif defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include "stb_image.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

// Built-in font, embedded by cmake/embed.cmake.
extern const unsigned char kFontRoboto[];
extern const unsigned int kFontRoboto_size;

namespace gui::assets {
namespace {

constexpr float kFontReferenceSize = 18.0f;
constexpr int kMaxImageSide = 4096;

// Render-thread state.
ImFont* g_builtinFont = nullptr;
ImFont* g_font = nullptr;
std::string g_fontName = "\x01";  // never a real name: forces the first SetFont to apply
std::string g_fontError;

ImTextureData* g_background = nullptr;
std::string g_backgroundName = "\x01";
std::string g_backgroundError;
std::vector<ImTextureData*> g_retired;  // waiting for the renderer to release them

bool IsAbsolute(const std::string& path) {
    return path.size() > 1 && (path[1] == ':' || (path[0] == '\\' && path[1] == '\\'));
}

bool Exists(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring WindowsFonts() {
    wchar_t windows[MAX_PATH] = L"C:\\Windows";
    GetWindowsDirectoryW(windows, MAX_PATH);
    return std::wstring(windows) + L"\\Fonts";
}

// A file name from the menu / config.ini: full path, or a name in BedrockQoL\<folder> (then the
// data folder itself, then C:\Windows\Fonts for fonts).
std::wstring Resolve(const std::string& name, const wchar_t* folder, bool systemFonts) {
    const std::wstring wide = text::Widen(name);
    if (IsAbsolute(name)) return wide;
    for (const std::wstring& dir : {Folder(folder), config::Directory()}) {
        const std::wstring path = dir + L"\\" + wide;
        if (Exists(path)) return path;
    }
    if (systemFonts) {
        const std::wstring path = WindowsFonts() + L"\\" + wide;
        if (Exists(path)) return path;
    }
    return L"";
}

bool ReadAll(const std::wstring& path, std::vector<unsigned char>& out) {
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return false;
    unsigned char buffer[65536];
    size_t n;
    out.clear();
    while ((n = fread(buffer, 1, sizeof(buffer), f)) > 0) out.insert(out.end(), buffer, buffer + n);
    fclose(f);
    return !out.empty();
}

void Retire(ImTextureData* tex) {
    if (!tex) return;
    tex->WantDestroyNextFrame = true;
    g_retired.push_back(tex);
}

void Delete(ImTextureData* tex) {
    ImGui::UnregisterUserTexture(tex);
    tex->DestroyPixels();
    IM_DELETE(tex);
}

// Box filter by an integer factor, for pictures larger than kMaxImageSide.
std::vector<unsigned char> Downscale(const unsigned char* pixels, int& w, int& h) {
    const int factor = (std::max(w, h) + kMaxImageSide - 1) / kMaxImageSide;
    const int nw = std::max(1, w / factor);
    const int nh = std::max(1, h / factor);
    std::vector<unsigned char> out(static_cast<size_t>(nw) * nh * 4);
    for (int y = 0; y < nh; ++y) {
        for (int x = 0; x < nw; ++x) {
            unsigned sum[4] = {};
            for (int dy = 0; dy < factor; ++dy) {
                const unsigned char* row = pixels + (static_cast<size_t>(y * factor + dy) * w + x * factor) * 4;
                for (int dx = 0; dx < factor; ++dx) {
                    for (int c = 0; c < 4; ++c) sum[c] += row[dx * 4 + c];
                }
            }
            for (int c = 0; c < 4; ++c) {
                out[(static_cast<size_t>(y) * nw + x) * 4 + c] = static_cast<unsigned char>(sum[c] / (factor * factor));
            }
        }
    }
    w = nw;
    h = nh;
    return out;
}

}  // namespace

std::wstring Folder(const wchar_t* name) {
    const std::wstring dir = config::Directory() + L"\\" + name;
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

std::vector<std::string> List(const wchar_t* folder, const char* extensions) {
    std::vector<std::string> out;
    const std::wstring dir = folder[0] && folder[1] == L':' ? std::wstring(folder) : Folder(folder);
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileW((dir + L"\\*").c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) return out;
    const std::string exts = text::Lower(extensions);
    do {
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        const std::string name = text::Narrow(data.cFileName);
        const size_t dot = name.find_last_of('.');
        if (dot == std::string::npos) continue;
        const std::string ext = text::Lower(name.substr(dot));
        size_t pos = 0;
        bool match = false;
        while (pos <= exts.size() && !match) {
            const size_t end = exts.find(';', pos);
            match = exts.substr(pos, end == std::string::npos ? std::string::npos : end - pos) == ext;
            if (end == std::string::npos) break;
            pos = end + 1;
        }
        if (match) out.push_back(name);
    } while (FindNextFileW(find, &data));
    FindClose(find);
    std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) {
        return text::Lower(a) < text::Lower(b);
    });
    return out;
}

const std::vector<std::string>& SystemFonts() {
    static const std::vector<std::string> fonts = List(WindowsFonts().c_str(), ".ttf;.otf;.ttc");
    return fonts;
}

void Init() {
    ImGuiIO& io = ImGui::GetIO();
    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = false;  // static data inside the DLL
    std::snprintf(cfg.Name, sizeof(cfg.Name), "Roboto Medium (built-in)");
    g_builtinFont = io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(kFontRoboto),
                                                   static_cast<int>(kFontRoboto_size), kFontReferenceSize, &cfg);
    if (!g_builtinFont) {
        logx::Error("Menu: the built-in font could not be loaded, using ImGui's default font");
        g_builtinFont = io.Fonts->AddFontDefault();
    }
    io.FontDefault = g_builtinFont;
    g_font = g_builtinFont;
    g_fontName = "";
    g_fontError.clear();
}

void SetFont(const std::string& name) {
    if (name == g_fontName) return;
    g_fontName = name;
    g_fontError.clear();

    ImGuiIO& io = ImGui::GetIO();
    if (g_font && g_font != g_builtinFont) {
        io.FontDefault = g_builtinFont;
        io.Fonts->RemoveFont(g_font);
    }
    g_font = g_builtinFont;
    io.FontDefault = g_builtinFont;
    if (name.empty()) return;

    const std::wstring path = Resolve(name, L"fonts", true);
    std::vector<unsigned char> data;
    if (path.empty() || !ReadAll(path, data)) {
        g_fontError = "файл шрифта не найден: " + name;
        logx::Warn("Menu: font '%s' not found (looked in BedrockQoL\\fonts and C:\\Windows\\Fonts)", name.c_str());
        return;
    }

    void* owned = IM_ALLOC(data.size());
    std::memcpy(owned, data.data(), data.size());
    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = true;
    std::snprintf(cfg.Name, sizeof(cfg.Name), "%s", name.c_str());
    ImFont* font = io.Fonts->AddFontFromMemoryTTF(owned, static_cast<int>(data.size()), kFontReferenceSize, &cfg);
    if (!font) {
        g_fontError = "не удалось прочитать шрифт: " + name;
        logx::Warn("Menu: '%s' is not a usable TTF/OTF font", name.c_str());
        return;
    }
    // Characters the chosen font lacks (e.g. Cyrillic in a Latin-only font) come from the built-in one.
    ImFontConfig fallback;
    fallback.MergeMode = true;
    fallback.FontDataOwnedByAtlas = false;
    io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(kFontRoboto), static_cast<int>(kFontRoboto_size),
                                   kFontReferenceSize, &fallback);
    g_font = font;
    io.FontDefault = font;
    logx::Info("Menu: font %s loaded", text::Narrow(path).c_str());
}

const std::string& FontError() { return g_fontError; }

void SetBackground(const std::string& name) {
    if (name == g_backgroundName) return;
    g_backgroundName = name;
    g_backgroundError.clear();
    Retire(g_background);
    g_background = nullptr;
    if (name.empty()) return;

    const std::wstring path = Resolve(name, L"images", false);
    std::vector<unsigned char> bytes;
    if (path.empty() || !ReadAll(path, bytes)) {
        g_backgroundError = "картинка не найдена: " + name;
        logx::Warn("Menu: background '%s' not found (put it into BedrockQoL\\images)", name.c_str());
        return;
    }
    int w = 0, h = 0, channels = 0;
    unsigned char* pixels = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &channels, 4);
    if (!pixels) {
        g_backgroundError = std::string("не удалось открыть картинку: ") + stbi_failure_reason();
        logx::Warn("Menu: background '%s' could not be decoded (%s)", name.c_str(), stbi_failure_reason());
        return;
    }
    std::vector<unsigned char> scaled;
    const unsigned char* source = pixels;
    if (w > kMaxImageSide || h > kMaxImageSide) {
        scaled = Downscale(pixels, w, h);
        source = scaled.data();
    }

    auto* tex = IM_NEW(ImTextureData)();
    tex->Create(ImTextureFormat_RGBA32, w, h);
    std::memcpy(tex->Pixels, source, static_cast<size_t>(w) * h * 4);
    tex->UseColors = true;
    ImGui::RegisterUserTexture(tex);
    stbi_image_free(pixels);
    g_background = tex;
    logx::Info("Menu: background %s loaded (%dx%d)", text::Narrow(path).c_str(), w, h);
}

const std::string& BackgroundError() { return g_backgroundError; }

bool Background(ImTextureRef& texture, ImVec2& size) {
    if (!g_background) return false;
    texture = g_background->GetTexRef();
    size = ImVec2(static_cast<float>(g_background->Width), static_cast<float>(g_background->Height));
    return true;
}

void Update() {
    for (size_t i = 0; i < g_retired.size();) {
        if (g_retired[i]->Status == ImTextureStatus_Destroyed) {
            Delete(g_retired[i]);
            g_retired.erase(g_retired.begin() + static_cast<std::ptrdiff_t>(i));
        } else {
            ++i;
        }
    }
}

void Shutdown() {
    // The renderer backend has already released the GPU copies.
    for (ImTextureData* tex : g_retired) Delete(tex);
    g_retired.clear();
    if (g_background) Delete(g_background);
    g_background = nullptr;
    g_backgroundName = "\x01";
    g_backgroundError.clear();
    g_builtinFont = nullptr;
    g_font = nullptr;
    g_fontName = "\x01";
    g_fontError.clear();
}

}  // namespace gui::assets
