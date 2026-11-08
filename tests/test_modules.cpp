/** @file test_modules.cpp @brief Проверки контрактов и чистой игровой логики. */
#include "api/contracts.hpp"
#include "modules/world.hpp"
#include "modules/collision.hpp"
#include "modules/movement.hpp"
#include "modules/interaction.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <iostream>
#include <type_traits>

using namespace rpg3d;

static bool near(float a, float b, float eps = 0.001f) {
    return std::fabs(a - b) <= eps;
}

static void test_contracts_and_world() {
    static_assert(std::is_trivially_copyable<SceneView>::value, "SceneView must be POD-like");
    static_assert(std::is_trivially_copyable<Player>::value, "Player must be POD-like");
    static_assert(sizeof(World) <= 8, "Immutable map must not copy geometry into World");
    World world;
    const SceneView scene = world.view();
    assert(scene.obstacles.count == 19);
    assert(scene.npcs.count == 4);
    assert(scene.halfExtent == 16.0f);
    assert(scene.obstacles.data != nullptr);
    assert(scene.npcs.data != nullptr);
}

static void test_collision() {
    const SceneView scene = World{}.view();
    assert(!Collision::blocked(scene, {0.0f, -1.0f}));
    assert(Collision::blocked(scene, {15.9f, 0.0f}));
    assert(Collision::blocked(scene, {-15.9f, 0.0f}));
    assert(Collision::blocked(scene, {7.5f, 5.2f}));
    assert(Collision::blocked(scene, scene.npcs[0].position));

    const Vec2 stopped = Collision::slide(scene, {4.9f, 5.2f}, {1.5f, 0.0f});
    assert(near(stopped.x, 4.9f));
    const Vec2 sliding = Collision::slide(scene, {4.9f, 5.2f}, {1.5f, -0.25f});
    assert(near(sliding.x, 4.9f));
    assert(near(sliding.z, 4.95f));

    // Независимость от World: передаём произвольную локальную сцену.
    const Obstacle single[]{
        {ObjectKind::Rock, {0.0f, 0.0f}, {1.0f, 1.0f}, 1.0f}
    };
    const SceneView testScene{{single, 1}, {nullptr, 0}, 10.0f};
    assert(Collision::blocked(testScene, {0.0f, 0.0f}));
    assert(!Collision::blocked(testScene, {2.0f, 2.0f}));
    assert(Collision::overlaps({1.0f, 0.0f}, 0.5f, {0.0f, 0.0f}, {1.0f, 1.0f}));
}

static void test_movement() {
    const SceneView scene = World{}.view();
    Player straight;
    const Vec2 forward = Movement::step(straight, {1.0f, 0.0f, false, false}, 0.02f);
    straight.position = Collision::slide(scene, straight.position, forward);
    Player diagonal;
    const Vec2 diag = Movement::step(diagonal, {1.0f, 1.0f, false, false}, 0.02f);
    diagonal.position = Collision::slide(scene, diagonal.position, diag);
    assert(near(std::sqrt(distance_squared(straight.position, {0.0f, -1.0f})),
                std::sqrt(distance_squared(diagonal.position, {0.0f, -1.0f}))));

    Player jumper;
    Movement::step(jumper, {0.0f, 0.0f, false, true}, 0.016f);
    assert(jumper.jumpHeight > 0.0f);
    for (int i = 0; i < 150; ++i) Movement::step(jumper, {}, 0.016f);
    assert(jumper.jumpHeight == 0.0f && jumper.verticalSpeed == 0.0f);

    Player actor;
    actor.position = {4.7f, 5.2f};
    actor.yaw = 1.5707963268f;
    for (int i = 0; i < 300; ++i) {
        actor.position = Collision::slide(scene, actor.position,
             Movement::step(actor, {1.0f, 0.0f, false, false}, 0.016f));
        assert(!Collision::blocked(scene, actor.position));
    }
    assert(actor.position.x < 5.3f);

    // Ограничение delta: нельзя одной задержкой игры телепортироваться через стену.
    const Vec2 clamped = Movement::step(jumper, {1, 0, false, false}, 10.0f);
    assert(near(clamped.z, 3.8f * 0.05f));
}

static void test_interactions() {
    const SceneView scene = World{}.view();
    const Interaction npc = InteractionSystem::closest(scene, {3.4f, 4.3f});
    assert(npc.type == Interaction::Type::Npc && npc.index == 0);
    assert(InteractionSystem::closest(scene, {0.0f, -1.0f}).type ==
           Interaction::Type::None);

    const Interaction ore = InteractionSystem::closest(scene, {2.0f, 11.1f});
    assert(ore.type == Interaction::Type::Resource);
    assert(scene.obstacles[ore.index].kind == ObjectKind::Ore);
    assert(ore.distanceSquared <= 2.5f * 2.5f);

    const SceneView empty{{nullptr, 0}, {nullptr, 0}, 5.0f};
    assert(InteractionSystem::closest(empty, {}).type == Interaction::Type::None);
}

int main() {
    test_contracts_and_world();
    test_collision();
    test_movement();
    test_interactions();
    std::cout << "Modular client tests: OK\n";
}
