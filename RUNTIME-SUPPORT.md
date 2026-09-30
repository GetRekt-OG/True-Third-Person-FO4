# Game version support

TTP doesn't use Address Library at runtime. Each supported `Fallout4.exe` has its
own address table compiled into the DLL
(`external/CommonLibF4/include/REL/BuiltinAddresses.hpp`). At startup,
`AddressResolver` picks the table for the exact running version and refuses to
load on anything else. Before patching engine code, every call site is checked
against the expected bytes; if any check fails, that feature is left unpatched.

The tables hold only the addresses TTP and its bundled CommonLib code use, not a
full address database.

## Supported versions

| Family | Version | Patch sites checked | SHA-256 of the exe used |
| --- | --- | --- | --- |
| OG | 1.10.163.0 | Yes | `55f57947db9e05575122fae1088f0b0247442f11e566b56036caa0ac93329c36` |
| NG | 1.10.980.0 | Yes | `f4b921adce32c357b56f90b92bd9ecfef4f3572412d1a32a3d35c76930440ce8` |
| NG | 1.10.984.0 | Yes | `bcb8f9fe660ef4c33712b873fdc24e5ecbd6a77e629d6419f803c2c09c63eaf2` |
| AE | 1.11.191.0 | Yes | `81694b37816c8045855905a52c5fb13583c5803121fabb5024760892014e9bc6` |
| AE | 1.11.221.0 | Yes | `428f9996cc4248e26c0f62f9fdd3eaf0e5eb305834b67ee5996538e593218b61` |
| AE | 1.11.240.0 | Yes | `fdcef37ac1230af6d0b0050eb2142b139ef3a867b37b9211fb6edfcc646072f8` |

"Patch sites checked" means the seven power-armor predicate calls, four
power-armor pivots, and the compass call/pivot sites were read from that exe and
match what `NativeHooks.hpp` expects. A Steam or GOG build with different code
would need the same check.

## Adding a version

1. Get the Address Library `version-X-Y-Z-0.bin` for that version. The format is a
   `uint64` count followed by `(uint64 id, uint64 offset)` pairs.
2. Take the ID list from an existing table of the same family (AE tables share
   one ID set, NG tables another) and look each ID up in the `.bin`. Every ID
   must resolve, and every offset must fall inside the exe's `SizeOfImage`.
3. Add the table as `runtimeX_Y_Z[]` (sorted by ID) and a line in `Select()`.
4. Add the version to `kTargetRuntimes` in `src/Main.cpp` and to `kSupported`
   in `tests/addresses.cpp`.
5. Check the patch sites in `src/NativeHookLayout.hpp` against the new exe. If
   the byte checks fail, the offsets inside those functions have moved and
   need a new layout entry.
6. Run the tests, then the in-game checklist in [TESTING.md](TESTING.md).

1.11.191.0 was added this way: all 1,232 AE IDs resolved from
`version-1-11-191-0.bin`, all offsets fall inside the image, and all 15 patch
sites matched in the 1.11.191 executable.

## Address sources

Earlier tables were built partly from CommonLibF4RD address records used as
reference material during development (MIT notice in `THIRD-PARTY`). NG 1.10.980
was mapped from 1.10.984 by comparing function layout, instruction patterns,
RIP-relative references and imports.

Throw aim (grenades and mines) finds the game's physics velocity setter at run
time instead of from the tables: GrenadeProjectile's vtable slot 0xE7 calls the
base Projectile::AddInitialVelocity, which ends with the setter call. Each call
is recognised by the bytes that follow it; both patterns were checked on OG and
AE. If either is missing on another build, the log says "Throw aim: velocity
setter not found, off" and throws are left alone.

