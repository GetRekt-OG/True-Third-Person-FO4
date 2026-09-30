#pragma once
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <string>
#include <string_view>
#include <vector>

// Startup trace in %TEMP%. Deliberately independent of spdlog and the game so it
// works before F4SE::Init. Each line is flushed to the OS, so a game crash loses
// nothing. It isn't forced to disk: that took a millisecond or more per line and
// showed up as a hitch when a line was written during play.
namespace TTPDiagnostic {
    // Enough for any real %TEMP%; kept small because Trace runs inside game hooks.
    inline constexpr std::size_t kPathSize = 2048;

    inline bool LogPath(wchar_t (&path)[kPathSize], const wchar_t *name) noexcept {
        std::size_t required = 0;
        return _wgetenv_s(&required, path, kPathSize, L"TEMP") == 0 && required > 1 &&
               required <= kPathSize - 64 && wcscat_s(path, name) == 0;
    }

    inline void Trace(std::string_view text) noexcept {
        wchar_t path[kPathSize]{};
        if (!LogPath(path, L"\\TrueThirdPerson-startup.log"))
            return;
        FILE *file = nullptr;
        if (_wfopen_s(&file, path, L"ab") != 0 || !file)
            return;
        std::fwrite(text.data(), 1, text.size(), file);
        std::fwrite("\r\n", 1, 2, file);
        std::fclose(file);
    }

    // Several lines with one file open.
    inline void TraceLines(const std::vector<std::string> &lines) noexcept {
        wchar_t path[kPathSize]{};
        if (lines.empty() || !LogPath(path, L"\\TrueThirdPerson-startup.log"))
            return;
        FILE *file = nullptr;
        if (_wfopen_s(&file, path, L"ab") != 0 || !file)
            return;
        for (const auto &line : lines) {
            std::fwrite(line.data(), 1, line.size(), file);
            std::fwrite("\r\n", 1, 2, file);
        }
        std::fclose(file);
    }

    // First line of a game session. A log over 1 MB is moved to
    // TrueThirdPerson-startup.old.log first, so it doesn't grow forever.
    inline void BeginSession(std::string_view header) noexcept {
        wchar_t path[kPathSize]{}, old[kPathSize]{};
        if (LogPath(path, L"\\TrueThirdPerson-startup.log") &&
            LogPath(old, L"\\TrueThirdPerson-startup.old.log")) {
            FILE *file = nullptr;
            long size = 0;
            if (_wfopen_s(&file, path, L"rb") == 0 && file) {
                if (std::fseek(file, 0, SEEK_END) == 0)
                    size = std::ftell(file);
                std::fclose(file);
            }
            if (size > 1024 * 1024) {
                _wremove(old);
                _wrename(path, old);
            }
        }
        Trace(header);
    }
}
