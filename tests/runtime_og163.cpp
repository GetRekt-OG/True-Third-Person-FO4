#include "REL/RuntimeOG163.hpp"
#include "../src/NativeHookLayout.hpp"
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

// Pass text.bin from the user's loaded 1.10.163.0 capture, not the packed EXE.
int main(int argc, char **argv) {
    if (argc != 2)
        return 2;
    std::ifstream input(argv[1], std::ios::binary);
    const std::vector<unsigned char> text((std::istreambuf_iterator<char>(input)), {});
    assert(text.size() == 46182287);
    auto read = [&](std::uint32_t rva, std::size_t n) {
        assert(rva >= 0x1000 && rva - 0x1000 + n <= text.size());
        return text.data() + rva - 0x1000;
    };
    auto resolve = [](std::uint64_t id) {
        const auto *entry = TTPRuntime::OG163::Find(id);
        assert(entry);
        return entry->rva;
    };
    for (const auto &entry : TTPRuntime::OG163::entries)
        assert(std::memcmp(read(entry.rva, entry.prefix.size()), entry.prefix.data(),
                           entry.prefix.size()) == 0);
    assert(!TTPRuntime::OG163::Find(UINT64_MAX));
    const auto predicate = resolve(2230295);
    auto call = [&](std::uint32_t rva) {
        const auto *code = read(rva, 17);
        std::int32_t delta;
        std::memcpy(&delta, code + 1, 4);
        assert(code[0] == 0xE8 && rva + 5 + delta == predicate);
        return code;
    };
    constexpr unsigned char pivot[]{0xFF, 0x90, 0xA8, 0x08, 0, 0, 0x84, 0xC0};
    for (const auto &site : TrueThirdPerson::NativeHookLayout::PowerCalls(true)) {
        const auto *code = call(resolve(site.function) + static_cast<std::uint32_t>(site.offset));
        assert(std::memcmp(code + 5, site.following.data(), 2) == 0);
    }
    for (const auto &site : TrueThirdPerson::NativeHookLayout::PowerPivots(true))
        assert(
            std::memcmp(read(resolve(site.function) + static_cast<std::uint32_t>(site.offset), 8),
                        pivot, 8) == 0);
    constexpr unsigned char tail[]{0x84, 0xC0, 0x74, 5,    0x0F, 0x57,
                                   0xC0, 0xEB, 0x0C, 0x48, 0x8B, 3};
    for (const auto &site : TrueThirdPerson::NativeHookLayout::Compass(true)) {
        const auto function = resolve(site.function);
        const auto *code = call(function + static_cast<std::uint32_t>(site.callOffset));
        assert(std::memcmp(code + 5, tail, sizeof(tail)) == 0);
        code = read(function + static_cast<std::uint32_t>(site.pivotOffset), 10);
        assert(std::memcmp(code, pivot, 8) == 0 && code[8] == 0x74 && code[9] == site.pivotBranch);
    }
    std::cout << "OG163: supplemental prefixes and native patch sites verified\n";
}
