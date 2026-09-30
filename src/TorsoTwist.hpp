#pragma once
#include "CameraSupport.hpp"
#include "RE/B/BSUtilities.hpp"
#include "RE/N/NiUpdateData.hpp"

// Hip-fire torso twist. While hip firing, the body keeps (close to) the direction
// you move and the spine turns the upper body, arms and gun the rest of the way
// to the camera, instead of the whole body snapping round. The spine bones are
// turned right after the game poses the skeleton each frame, so it works with
// any animation set. Twist 0 in the settings brings back the old full-body turn.
namespace TrueThirdPerson::TorsoTwist {
    // Radians the upper body is turned from the body. Written on the main thread,
    // read by the animation hook, which may run on an animation job.
    inline std::atomic<float> applied{0};
    // Radians the legs are turned back from the body (their yaw minus the
    // actor's). A throw from standing turns the actor to the camera; the legs are
    // turned back to where they stood and the spine turns the upper body forward
    // again, the same look as the hip-fire twist. See FaceThrow in Main.cpp.
    inline std::atomic<float> appliedLegs{0};
    inline float output = 0;
    inline float weight = 0;     // 0..1, eases the twist in and out
    inline float lastTarget = 0; // twist toward the camera last frame
    inline std::atomic_int reported{-1};
    // Once per game frame: the pose is rebuilt every frame, a second pass in the
    // same frame would turn the spine twice.
    inline std::atomic_uint frame{0}, appliedFrame{0};

    struct Bone {
        const char *name;
        float share;
    };
    inline constexpr Bone kBones[]{{"SPINE1", 0.30F}, {"SPINE2", 0.35F}, {"Chest", 0.35F}};
    inline std::array<RE::BSFixedString, std::size(kBones)> *names = nullptr;
    inline std::array<RE::BSFixedString, 2> *hipNames = nullptr; // COM, else Pelvis

    inline bool Enabled() {
        return g_config.twistDegrees > 0;
    }
    inline float Limit() {
        return g_config.twistDegrees * std::numbers::pi_v<float> / 180.0F;
    }

    // Main thread, end of the player update. `firing`: the hip-fire window is open.
    inline void Update(bool firing, float cameraYaw, float bodyYaw, float dt) {
        const bool aim = firing && Enabled();
        // The twist always closes the exact gap between body and camera, so the
        // chest and gun stay on the crosshair however fast the legs turn; only
        // its weight eases in (quick) and out (softer).
        if (std::isfinite(cameraYaw) && std::isfinite(bodyYaw))
            lastTarget = std::clamp(Logic::SignedYaw(cameraYaw - bodyYaw), -Limit(), Limit());
        weight = Twist::Approach(weight, aim ? 1.0F : 0.0F, aim ? 14.0F : 7.0F, dt);
        if (!aim && weight < 0.01F)
            weight = 0;
        output = weight * lastTarget;
        applied = output;
        ++frame;
    }
    inline void Reset() {
        output = weight = lastTarget = 0;
        applied = 0;
        appliedLegs = 0;
    }

    inline Twist::Mat3 ToMat(const RE::NiMatrix3 &m) {
        return {{{m.rows[0].x, m.rows[0].y, m.rows[0].z},
                 {m.rows[1].x, m.rows[1].y, m.rows[1].z},
                 {m.rows[2].x, m.rows[2].y, m.rows[2].z}}};
    }
    inline void Store(RE::NiMatrix3 &m, const Twist::Mat3 &a) {
        for (int i = 0; i < 3; ++i) {
            m.rows[i].x = a[i][0];
            m.rows[i].y = a[i][1];
            m.rows[i].z = a[i][2];
        }
    }

    // How the engine composes rotations. Column: world = parent * local (vectors
    // are columns). Row: world = local * parent (the stored matrices are the
    // transposes). Worked out from the skeleton itself; see Learn.
    inline std::atomic_int convention{0}; // 0 not checked yet (row assumed), 1 column, 2 row
    inline float columnError = 0, rowError = 0;
    inline int samples = 0;

    // World rotation of `node` from this frame's local rotations below the
    // skeleton root (whose world rotation is kept up to date with the actor).
    // The bones' own world transforms are only refreshed later in the frame.
    inline Twist::Mat3 ChainRotation(RE::NiAVObject *root, RE::NiAVObject *node, bool row) {
        RE::NiAVObject *chain[64]{};
        int count = 0;
        for (auto *n = node; n && n != root && count < 64; n = n->parent)
            chain[count++] = n;
        auto rotation = ToMat(root->world.rotation);
        for (int i = count - 1; i >= 0; --i) {
            const auto local = ToMat(chain[i]->local.rotation);
            rotation = row ? Twist::Multiply(local, rotation) : Twist::Multiply(rotation, local);
        }
        return rotation;
    }
    inline float Difference(const Twist::Mat3 &a, const Twist::Mat3 &b) {
        float sum = 0;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                sum += (a[i][j] - b[i][j]) * (a[i][j] - b[i][j]);
        return sum;
    }
    // Compares the engine's world rotation of a bone the twist never touches with
    // both ways of composing the chain. The right one matches (up to a frame of
    // motion); the wrong one is off by the lean of the hips and spine.
    inline void Learn(RE::NiAVObject *root, RE::NiAVObject *node) {
        if (convention != 0 || !node || node == root)
            return;
        const auto engine = ToMat(node->world.rotation);
        columnError += Difference(engine, ChainRotation(root, node, false));
        rowError += Difference(engine, ChainRotation(root, node, true));
        if (++samples < 30)
            return;
        convention = rowError < columnError * 0.5F ? 2 : 1;
        char line[160]{};
        std::snprintf(line, sizeof(line),
                      "Torso twist: rotation order %s (column error %.4f, row error %.4f)",
                      convention == 2 ? "row" : "column", columnError / samples, rowError / samples);
        TTPDiagnostic::Trace(line);
    }

    // New local rotation turning the node `yaw` about world up.
    inline Twist::Mat3 Turn(const Twist::Mat3 &parent, const Twist::Mat3 &local, float yaw,
                            bool row) {
        if (!row)
            return Twist::TurnLocal(parent, local, yaw);
        // Transposed composition: local' = local * parent * R^T * parent^T.
        return Twist::Multiply(
            Twist::Multiply(Twist::Multiply(local, parent), Twist::Transpose(Twist::YawRotation(yaw))),
            Twist::Transpose(parent));
    }

    // After the game has posed the player's third-person skeleton.
    inline void Apply(RE::PlayerCharacter *player) {
        const float twist = applied * g_config.twistSign, legs = appliedLegs;
        if (!names || !hipNames || !player || !std::isfinite(twist) || !std::isfinite(legs) ||
            (std::abs(twist) < 0.001F && std::abs(legs) < 0.001F))
            return;
        const unsigned current = frame;
        if (appliedFrame.exchange(current) == current)
            return;
        auto *root = player->Get3D(false);
        if (!root)
            return;
        RE::NiAVObject *nodes[std::size(kBones)]{};
        float total = 0;
        int found = 0;
        for (std::size_t i = 0; i < std::size(kBones); ++i) {
            nodes[i] = RE::BSUtilities::GetObjectByName(root, (*names)[i], true, false);
            if (!nodes[i] || !nodes[i]->parent)
                continue;
            total += kBones[i].share;
            ++found;
        }
        if (reported.exchange(found) != found) {
            char line[96]{};
            std::snprintf(line, sizeof(line), "Torso twist: %d of %zu spine bones found", found,
                          std::size(kBones));
            TTPDiagnostic::Trace(line);
        }
        if (!found || total <= 0)
            return;
        RE::NiAVObject *top = nullptr;
        for (auto *node : nodes)
            if (node && node->parent && !top)
                top = node;
        Learn(root, top->parent);
        // Row order is what the game uses; until the check has seen enough frames,
        // assume it rather than twisting about a tilted axis for the first shots.
        const bool row = convention != 1;
        // Legs first: the whole body below the spine turns back, then the spine
        // turns the upper body by the twist plus that amount.
        RE::NiAVObject *hips = nullptr;
        if (std::abs(legs) >= 0.001F) {
            for (const auto &name : *hipNames) {
                hips = RE::BSUtilities::GetObjectByName(root, name, true, false);
                if (hips && hips->parent)
                    break;
                hips = nullptr;
            }
            if (hips)
                Store(hips->local.rotation,
                      Turn(ChainRotation(root, hips->parent, row), ToMat(hips->local.rotation), legs, row));
        }
        const float yaw = hips ? twist - legs : twist;
        // Parent rotations from before any change; see Twist::TurnLocal.
        Twist::Mat3 parents[std::size(kBones)]{};
        for (std::size_t i = 0; i < std::size(kBones); ++i)
            if (nodes[i] && nodes[i]->parent)
                parents[i] = ChainRotation(root, nodes[i]->parent, row);
        for (std::size_t i = 0; i < std::size(kBones); ++i) {
            if (!nodes[i] || !nodes[i]->parent)
                continue;
            auto &rotation = nodes[i]->local.rotation;
            Store(rotation,
                  Turn(parents[i], ToMat(rotation), yaw * kBones[i].share / total, row));
        }
        // Bring the world transforms under the lowest turned bone up to date, in
        // case the game already computed them for this frame.
        RE::NiUpdateData data{};
        (hips ? hips : top)->UpdateDownwardPass(data, 0);
    }

    // Applied right after the game updates the player's animation (the pose is
    // written to the bones there). DoPostAnimationChannelUpdate ran before the
    // pose was written, so changes made there were overwritten.
    struct AnimationHook {
        static inline void (*original)(RE::PlayerCharacter *, float) = nullptr;
        static void Thunk(RE::PlayerCharacter *self, float delta) {
            original(self, delta);
            if (self == RE::PlayerCharacter::GetSingleton())
                Apply(self);
        }
        static bool Install() {
            if (original)
                return true;
            auto *player = RE::PlayerCharacter::GetSingleton();
            if (!player)
                return false;
            if (!names) {
                names = new std::array<RE::BSFixedString, std::size(kBones)>{
                    RE::BSFixedString(kBones[0].name), RE::BSFixedString(kBones[1].name),
                    RE::BSFixedString(kBones[2].name)};
            }
            if (!hipNames)
                hipNames = new std::array<RE::BSFixedString, 2>{RE::BSFixedString("COM"),
                                                                 RE::BSFixedString("Pelvis")};
            auto hooks = HookBatch::For(player);
            hooks.Add(0x9F, Thunk, original); // TESObjectREFR::UpdateAnimation
            return hooks.Commit();
        }
    };
}
