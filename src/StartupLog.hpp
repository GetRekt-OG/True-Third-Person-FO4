#pragma once
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <io.h>
#include <string_view>

// Diagnostic-only, independent of spdlog, game singletons and Address Library.
// Open/flush/commit/close each record so process termination loses no buffered lines.
namespace TTPDiagnostic {
    inline void Trace(std::string_view text) noexcept {
        wchar_t path[32768]{};
        std::size_t required = 0;
        if (_wgetenv_s(&required, path, 32768, L"TEMP") != 0 || required <= 1 || required > 32600)
            return;
        if (wcscat_s(path, L"\\TrueThirdPerson-startup.log") != 0)
            return;
        FILE *file = nullptr;
        if (_wfopen_s(&file, path, L"ab") != 0 || !file)
            return;
        std::fwrite(text.data(), 1, text.size(), file);
        std::fwrite("\r\n", 1, 2, file);
        std::fflush(file);
        _commit(_fileno(file));
        std::fclose(file);
    }
    inline void Trace(std::wstring_view text) noexcept {
        char buffer[2048]{};
        const auto count = text.size() < 2047 ? text.size() : 2047;
        for (std::size_t i = 0; i < count; ++i)
            buffer[i] = text[i] < 128 ? static_cast<char>(text[i]) : '?';
        Trace(std::string_view(buffer, count));
    }
} // namespace TTPDiagnostic
