#include "REL/RuntimeDatabaseKnown.hpp"
#include <array>
#include <iostream>

// Database coverage only: resolving a function does not validate its ABI or
// the instruction offsets used by the plugin's hooks.
int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: runtime_coverage f4rd-runtime.bin\n";
        return 2;
    }
    constexpr std::array versions{
        std::array<std::uint16_t, 4>{1, 10, 163, 0},
        std::array<std::uint16_t, 4>{1, 10, 980, 0},
        std::array<std::uint16_t, 4>{1, 10, 984, 0},
        std::array<std::uint16_t, 4>{1, 11, 221, 0},
        std::array<std::uint16_t, 4>{1, 11, 240, 0}};
    constexpr std::array<std::uint64_t, 4> ids{
        2230295, 2248327, 2248338, 2219437};
    try {
        for (const auto& version : versions) {
            TTPRuntime::KnownDatabase db;
            db.Load(argv[1], version);
            std::cout << version[0] << '.' << version[1] << '.' << version[2]
                      << ": " << db.Size() << " known IDs\n";
            for (const auto id : ids) {
                std::cout << "  " << id << ": ";
                try {
                    const auto address = db.Resolve(id);
                    std::cout << "0x" << std::hex << address << std::dec << '\n';
                } catch (const std::exception&) {
                    std::cout << "MISSING\n";
                }
            }
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
