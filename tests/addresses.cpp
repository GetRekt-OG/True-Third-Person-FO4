#include "../external/CommonLibF4/include/REL/BuiltinAddresses.hpp"
#include "../src/NativeHookLayout.hpp"
#include <cassert>
#include <cstdio>
#include <limits>

using namespace TTPRuntime;

// Vtable IDs used by EmplaceVtable constructors in the bundled CommonLib headers
// and sources. Header-defined constructors count too (UIMessage lives in one).
constexpr std::uint32_t kConstructorVtables[]{
    23904,   29840,   34112,   47280,   51540,   75750,   82634,   89447,   92488,   93985,
    103211,  117692,  129968,  137209,  146310,  168197,  179124,  193699,  215463,  220992,
    234076,  234807,  237942,  247338,  265264,  275958,  277807,  279203,  289899,  303327,
    307277,  319870,  369508,  407287,  413530,  434636,  451426,  466048,  467529,  510367,
    552870,  586697,  593426,  606842,  621542,  624641,  634682,  680791,  694269,  712117,
    725760,  738758,  775704,  794407,  797443,  806843,  808121,  823617,  826145,  834766,
    836343,  842040,  846981,  859152,  871785,  883625,  898195,  905839,  930437,  938404,
    939371,  940261,  952475,  969821,  994213,  998850,  999262,  1031341, 1033467, 1043322,
    1045422, 1045680, 1046100, 1048271, 1072833, 1086897, 1092305, 1092699, 1108148, 1116978,
    1119447, 1124700, 1138903, 1139281, 1142633, 1153354, 1166197, 1185650, 1186049, 1195453,
    1214487, 1244219, 1291641, 1297891, 1316205, 1324799, 1325250, 1325994, 1339012, 1339698,
    1360746, 1361956, 1364940, 1413068, 1414337, 1416190, 1475587, 1480578, 1488491, 1499319,
    1510330, 1526621, 1539601};

constexpr Version kSupported[]{
    {1, 10, 163, 0}, {1, 10, 980, 0}, {1, 10, 984, 0},
    {1, 11, 191, 0}, {1, 11, 221, 0}, {1, 11, 240, 0}};

int main() {
    using TrueThirdPerson::NativeHookLayout::Compass;
    using TrueThirdPerson::NativeHookLayout::PowerCalls;
    using TrueThirdPerson::NativeHookLayout::PowerPivots;

    for (const auto v : kSupported) {
        const auto entries = Select(v);
        assert(!entries.empty());
        std::uint32_t previous = 0;
        for (const auto entry : entries) {
            assert(entry.id > previous && entry.rva != 0); // sorted, unique, non-zero
            assert(Find(entries, entry.id) == entry.rva);
            previous = entry.id;
        }
        assert(Find(entries, 0) == 0);
        assert(Find(entries, std::numeric_limits<std::uintptr_t>::max()) == 0);

        const bool og = v[2] == 163;
        for (const auto site : PowerCalls(og))
            assert(Find(entries, site.function));
        for (const auto site : PowerPivots(og))
            assert(Find(entries, site.function));
        for (const auto site : Compass(og))
            assert(Find(entries, site.function));
        assert(Find(entries, 2230295)); // holstered-camera predicate
        for (const auto id : kConstructorVtables)
            assert(Find(entries, id));
    }

    // UIMessage vtable. 0.2.4.52 shipped without it and crashed when the marker opened.
    assert(Find(Select({1, 10, 163, 0}), 1124700) == 0x2EAEEA0);
    assert(Find(Select({1, 10, 980, 0}), 1124700) == 0x2508B00);
    assert(Find(Select({1, 10, 984, 0}), 1124700) == 0x2508B30);
    assert(Find(Select({1, 11, 191, 0}), 1124700) == 0x2716140);
    assert(Find(Select({1, 11, 221, 0}), 1124700) == 0x2716150);
    assert(Find(Select({1, 11, 240, 0}), 1124700) == 0x271E260);

    assert(Find(Select({1, 10, 163, 0}), 2230295) == 0xDB0240);
    assert(Find(Select({1, 10, 984, 0}), 2230295) == 0xC17340);
    assert(Find(Select({1, 11, 191, 0}), 2230295) == 0xC9CDF0);
    assert(Find(Select({1, 11, 221, 0}), 2230295) == 0xC9CF80);
    assert(Find(Select({1, 11, 240, 0}), 2230295) == 0xC9D310);

    assert(Select({1, 10, 162, 0}).empty());
    assert(Select({1, 11, 190, 0}).empty());
    assert(Select({1, 11, 241, 0}).empty());
    assert(Select({1, 11, 240, 1}).empty());
    std::puts("addresses: ok");
}
