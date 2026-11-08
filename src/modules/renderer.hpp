#pragma once
/**
 * @file renderer.hpp
 * @brief Русскоязычный 3D-клиент с системным шрифтом кириллицы.
 * @details Модуль только рисует невладеющие DTO; не читает SQLite и не изменяет мир.
 *          Прямоугольники и кубы формируются программно, Blender не нужен.
 */
#include "api/contracts.hpp"
#include "raylib.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace rpg3d {
/** @brief Обёртка над графикой и шрифтом с явным жизненным циклом. */
class Renderer final {
public:
    /**
     * @brief Загрузить установленный системный TTF с кириллицей.
     * @return false, если системный шрифт не обнаружен.
     * @note Вызывать после InitWindow в главном графическом потоке.
     */
    bool initialize() {
        static constexpr const char* paths[] = {
            "C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/arial.ttf",
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
            "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf"
        };
        std::array<int, 96+320+2> codepoints{};
        int count=0;
        for (int ch=32;ch<128;++ch) codepoints[count++]=ch;
        for (int ch=0x0400;ch<=0x053F;++ch) codepoints[count++]=ch;
        codepoints[count++]=0x2116; // №
        codepoints[count++]=0x20bd; // ₽
        for (const char* path : paths) {
            if (!FileExists(path)) continue;
            font_=LoadFontEx(path,28,codepoints.data(),count);
            if (font_.texture.id!=0) { loaded_=true;return true; }
        }
        return false;
    }

    /** @brief Освободить GPU-текстуру шрифта до CloseWindow. */
    void shutdown() noexcept {
        if (loaded_) UnloadFont(font_);
        loaded_=false;
    }

    /** @brief Нарисовать геометрию внутри BeginMode3D. */
    static void scene(SceneView scene, const Player& player) noexcept {
        const float mapSize = scene.halfExtent * 2.0f;
        DrawCube({0.0f, -0.46f, 0.0f}, mapSize, 0.9f, mapSize,
                 Color{106, 143, 100, 255});
        DrawCubeWires({0.0f, -0.46f, 0.0f}, mapSize, 0.9f, mapSize,
                      Color{69, 99, 78, 255});

        // Две непрерывные дороги вместо 46 одинаковых кубов в каждом кадре.
        DrawCube({0.0f, 0.001f, 0.0f}, 23.0f, 0.018f, 2.0f,
                 Color{168, 152, 121, 255});
        DrawCube({0.0f, 0.002f, 0.0f}, 2.0f, 0.018f, 23.0f,
                 Color{168, 152, 121, 255});
        DrawGrid(static_cast<int>(mapSize), 1.0f);

        for (const Obstacle& obstacle : scene.obstacles) draw_obstacle(obstacle);
        for (const Npc& npc : scene.npcs) {
            draw_humanoid(npc.position, 0.0f, npc_color(npc.palette), 0.0f, false);
        }
        draw_humanoid(player.position, player.jumpHeight,
                      Color{69, 133, 201, 255}, player.yaw, true);
    }

    /** @brief Вывести названия профессий NPC над их кубическими фигурами. */
    void labels(SceneView scene,Camera3D camera,Vec2 playerPosition) const noexcept {
        for (const Npc& npc:scene.npcs) {
            if (distance_squared(npc.position,playerPosition)>100.0f) continue;
            const Vector2 atScreen=GetWorldToScreen(at(npc.position,2.25f),camera);
            const int textWidth=width(npc.profession,18);
            DrawRectangle(static_cast<int>(atScreen.x)-textWidth/2-8,
                static_cast<int>(atScreen.y)-5,textWidth+16,27,Color{20,29,39,210});
            text(npc.profession,static_cast<int>(atScreen.x)-textWidth/2,
                static_cast<int>(atScreen.y),18,RAYWHITE);
        }
    }

    /**
     * @brief Нарисовать русскоязычный HUD, инвентарь, журнал и диалог.
     * @param scene Геометрия, содержащая NPC.
     * @param player Текущая позиция персонажа.
     * @param hud Невладеющие ссылки на подготовленные Application снимки.
     */
    void hud(SceneView scene,const Player& player,const HudState& hud) const noexcept {
        const int w=GetScreenWidth(),h=GetScreenHeight();
        DrawRectangle(14,14,460,92,Color{22,33,43,218});
        text("RPG 3D | Кубическая деревня v0.2",25,25,22,RAYWHITE);
        text(TextFormat("X: %.1f  Z: %.1f | События: %lld",player.position.x,player.position.z,
            static_cast<long long>(hud.discoveries)),25,58,18,Color{184,212,220,255});
        text("WASD — движение  |  E — действие  |  I — инвентарь  |  J — журнал",
            18,h-33,17,RAYWHITE);
        text("T — продать  |  C — обмен валют  |  P — молитва  |  R — опыт  |  F1 — помощь",
            18,h-59,17,RAYWHITE);

        if (hud.nearby.type!=Interaction::Type::None && hud.activeDialogue<0 &&
            !hud.showInventory && !hud.showJournal) {
            const char* target="Ресурс";
            if (hud.nearby.type==Interaction::Type::Npc &&
                hud.nearby.index<scene.npcs.count) target=scene.npcs[hud.nearby.index].profession;
            const int labelWidth=width(target,20);
            DrawRectangle(w/2-labelWidth/2-85,h-140,labelWidth+170,47,Color{20,29,39,220});
            text("[E]",w/2-labelWidth/2-68,h-128,20,YELLOW);
            text(target,w/2-labelWidth/2-18,h-128,20,RAYWHITE);
            if (hud.nearby.type==Interaction::Type::Resource && hud.resourceRemaining>=0)
                text(TextFormat("Остаток: %lld",static_cast<long long>(hud.resourceRemaining)),
                     w/2-90,h-174,18,RAYWHITE);
        }
        if (hud.activeDialogue>=0 && static_cast<std::size_t>(hud.activeDialogue)<scene.npcs.count) {
            const Npc& npc=scene.npcs[static_cast<std::size_t>(hud.activeDialogue)];
            DrawRectangle(w/2-390,h-250,780,139,Color{15,26,36,237});
            DrawRectangleLines(w/2-390,h-250,780,139,Color{116,158,164,255});
            text(npc.name,w/2-369,h-237,24,Color{243,215,147,255});
            text(npc.dialogueLine1,w/2-368,h-202,19,RAYWHITE);
            if (hud.activeDialogue==1)
                text("[T] Продать руду (10 зол.)  [C] Обменять 5 зол. на 20 серебра",
                     w/2-369,h-169,17,YELLOW);
            text("[E] Закрыть",w/2+234,h-144,16,LIGHTGRAY);
        }
        if (hud.showInventory && hud.inventory) {
            DrawRectangle(w/2-265,h/2-210,530,410,Color{15,26,36,246});
            DrawRectangleLines(w/2-265,h/2-210,530,410,Color{116,158,164,255});
            text("ЛИЧНЫЙ ИНВЕНТАРЬ",w/2-240,h/2-188,25,RAYWHITE);
            const InventorySnapshot& inv=*hud.inventory;
            text(TextFormat("Руда: %lld",static_cast<long long>(inv.ore)),w/2-230,h/2-138,21,RAYWHITE);
            text(TextFormat("Древесина: %lld",static_cast<long long>(inv.wood)),w/2-230,h/2-103,21,RAYWHITE);
            text(TextFormat("Кристаллы: %lld",static_cast<long long>(inv.gem)),w/2-230,h/2-68,21,RAYWHITE);
            text(TextFormat("Золото: %lld",static_cast<long long>(inv.gold)),w/2-230,h/2-20,21,YELLOW);
            text(TextFormat("Серебро: %lld",static_cast<long long>(inv.silver)),w/2-230,h/2+15,21,RAYWHITE);
            text(TextFormat("Уникальных предметов: %d",static_cast<int>(inv.uniqueItems.size())),
                 w/2-230,h/2+62,20,Color{193,215,245,255});
            if (!inv.uniqueItems.empty())
                text(TextFormat("ID предмета: %lld",static_cast<long long>(inv.uniqueItems.front())),
                    w/2-230,h/2+97,18,RAYWHITE);
            text("[I] Закрыть",w/2-230,h/2+158,17,LIGHTGRAY);
        }
        if (hud.showJournal) {
            DrawRectangle(w/2-385,h/2-230,770,460,Color{15,26,36,247});
            DrawRectangleLines(w/2-385,h/2-230,770,460,Color{116,158,164,255});
            text("ЖУРНАЛ СОБЫТИЙ (SHA-256)",w/2-361,h/2-208,24,RAYWHITE);
            for (std::size_t i=0;i<hud.eventCount && i<9;++i) {
                const EventRecord& e=hud.events[i];
                text(TextFormat("№%lld",static_cast<long long>(e.sequence)),
                    w/2-360,h/2-161+static_cast<int>(i)*35,17,YELLOW);
                text(event_name(e.type),w/2-289,h/2-161+static_cast<int>(i)*35,17,RAYWHITE);
                text(TextFormat("%.12s",e.hash.c_str()),w/2+212,
                     h/2-161+static_cast<int>(i)*35,15,Color{170,195,207,255});
            }
            text("[J] Закрыть  |  Полные записи доступны в SQLite",w/2-360,h/2+194,17,LIGHTGRAY);
        }
        if (hud.showHelp) {
            DrawRectangle(w/2-417,h/2-225,834,440,Color{17,25,35,248});
            DrawRectangleLines(w/2-417,h/2-225,834,440,Color{123,164,176,255});
            text("СПРАВКА ПО ИГРЕ",w/2-385,h/2-198,27,RAYWHITE);
            const char* lines[]={
                "W A S D — ходьба, Shift — бег, Space — прыжок",
                "Мышь — камера, Tab — освободить курсор",
                "E — добыть ресурс или поговорить с NPC",
                "I — приватный инвентарь, J — доступные события",
                "У алхимика: T — продажа руды, C — обмен валют",
                "У алхимика: P — молитва раз в игровой день",
                "У алхимика: R — редкий эксперимент (шанс 1 / 1 000 000)",
                "F1 — закрыть справку, Esc — выйти"
            };
            int pos=h/2-150;
            for (const char* line:lines) {text(line,w/2-382,pos,18,RAYWHITE);pos+=42;}
        }
        if (hud.cursorReleased)
            text("Курсор свободен. Нажми Tab, чтобы вернуться",18,118,18,YELLOW);
        if (hud.pendingTrade)
            text("NPC исполняет торговый контракт...",18,150,18,YELLOW);
        if (hud.notice && hud.notice[0]) {
            DrawRectangle(20,190,std::min(1030,w-40),44,Color{19,31,42,232});
            text(hud.notice,30,200,18,RAYWHITE);
        }
        DrawFPS(w-100,16);
    }

private:
    Font font_{}; ///< GPU-шрифт; владение только у Renderer.
    bool loaded_=false;

    /** @brief Русский текст через заранее загруженный TTF. */
    void text(const char* s,int x,int y,int size,Color color) const noexcept {
        DrawTextEx(font_,s,Vector2{static_cast<float>(x),static_cast<float>(y)},
                   static_cast<float>(size),1.0f,color);
    }
    int width(const char* s,int size) const noexcept {
        return static_cast<int>(MeasureTextEx(font_,s,static_cast<float>(size),1.0f).x);
    }
    static const char* event_name(const std::string& kind) noexcept {
        if (kind=="genesis") return "Создан мир";
        if (kind=="harvest") return "Добыча ресурсов";
        if (kind=="regenerate") return "Природа восстановилась";
        if (kind=="offer_open") return "Открыта сделка";
        if (kind=="offer_done") return "Сделка исполнена";
        if (kind=="offer_cancel") return "Сделка отменена";
        if (kind=="meditate") return "Молитва";
        if (kind=="jump") return "Прыжок";
        if (kind=="node_init") return "Месторождение создано";
        if (kind=="synthesis_failed") return "Неудачный синтез";
        if (kind=="discovery") return "Редкое открытие";
        return "Событие";
    }
    /** @brief Перевести плоскую позицию в 3D-координаты raylib. */
    static Vector3 at(Vec2 p, float y) noexcept { return {p.x, y, p.z}; }

    /** @brief Затемнить цвет, не меняя альфа-канал. */
    static Color shade(Color source, float factor) noexcept {
        const auto channel = [factor](unsigned char c) -> unsigned char {
            return static_cast<unsigned char>(std::clamp(c * factor, 0.0f, 255.0f));
        };
        return {channel(source.r), channel(source.g), channel(source.b), source.a};
    }

    /** @brief Преобразовать тип объекта в цвет для кубической визуализации. */
    static Color color(ObjectKind kind) noexcept {
        switch (kind) {
            case ObjectKind::Forge:     return {146, 88, 72, 255};
            case ObjectKind::Alchemy:   return {87, 99, 146, 255};
            case ObjectKind::Carpentry: return {177, 132, 74, 255};
            case ObjectKind::House:     return {178, 153, 111, 255};
            case ObjectKind::Tree:      return {110, 76, 49, 255};
            case ObjectKind::Rock:      return {117, 125, 126, 255};
            case ObjectKind::Fence:     return {121, 85, 58, 255};
            case ObjectKind::Ore:       return {91, 108, 139, 255};
            case ObjectKind::WoodPile:  return {140, 98, 59, 255};
        }
        return GRAY;
    }

    /** @brief Нарисовать один элемент статической геометрии. */
    static void draw_obstacle(const Obstacle& o) noexcept {
        const Color base = color(o.kind);
        const Vector3 center = at(o.position, o.height / 2.0f);
        const float width = o.halfSize.x * 2.0f;
        const float depth = o.halfSize.z * 2.0f;
        if (o.kind == ObjectKind::Tree) {
            DrawCube(center, width, o.height, depth, base);
            DrawCube(at(o.position, o.height + 0.4f), 2.0f, 1.5f, 2.0f,
                     Color{54, 116, 71, 255});
            DrawCube(at(o.position, o.height + 1.15f), 1.4f, 0.7f, 1.4f,
                     Color{72, 145, 78, 255});
            return;
        }
        DrawCube(center, width, o.height, depth, base);
        DrawCubeWires(center, width, o.height, depth, shade(base, 0.55f));

        if (o.kind == ObjectKind::Forge || o.kind == ObjectKind::Alchemy ||
            o.kind == ObjectKind::Carpentry || o.kind == ObjectKind::House) {
            DrawCube(at(o.position, o.height + 0.23f), width + 0.45f, 0.45f,
                     depth + 0.45f, shade(base, 0.65f));
            DrawCube({o.position.x - width * 0.3f, 1.6f,
                      o.position.z - o.halfSize.z - 0.03f},
                     0.66f, 0.65f, 0.09f, Color{173, 222, 225, 255});
            DrawCube({o.position.x + width * 0.25f, 0.8f,
                      o.position.z - o.halfSize.z - 0.04f},
                     0.9f, 1.6f, 0.1f, shade(base, 0.45f));
        }
        if (o.kind == ObjectKind::Forge) {
            DrawCube({o.position.x + 1.0f, o.height + 1.05f, o.position.z},
                     0.7f, 1.8f, 0.7f, Color{93, 86, 79, 255});
        }
        if (o.kind == ObjectKind::Ore) {
            DrawCube(at(o.position, o.height + 0.04f),
                     width * 0.55f, 0.08f, depth * 0.55f,
                     Color{98, 190, 209, 255});
        }
        if (o.kind == ObjectKind::WoodPile) {
            for (int i = -1; i <= 1; ++i) {
                DrawCube({o.position.x + i * 0.42f, o.height + 0.16f,
                          o.position.z}, 0.36f, 0.31f, 1.1f,
                         Color{166, 112, 65, 255});
            }
        }
    }

    /** @brief Получить цвет одежды NPC без разыменования динамической таблицы. */
    static Color npc_color(std::uint8_t index) noexcept {
        static constexpr Color palette[4] = {
            {169, 92, 71, 255}, {109, 105, 180, 255},
            {200, 139, 69, 255}, {91, 156, 94, 255}
        };
        return palette[index % 4];
    }

    /** @brief Нарисовать персонажа из шести кубов и маркера направления. */
    static void draw_humanoid(Vec2 position, float baseY, Color clothes,
                              float yaw, bool player) noexcept {
        const Color skin{231, 188, 151, 255};
        DrawCube({position.x, baseY + 0.94f, position.z}, 0.74f, 0.9f, 0.43f,
                 clothes);
        DrawCube({position.x, baseY + 1.67f, position.z}, 0.57f, 0.55f, 0.52f,
                 skin);
        DrawCube({position.x - 0.48f, baseY + 0.92f, position.z},
                 0.23f, 0.79f, 0.36f, clothes);
        DrawCube({position.x + 0.48f, baseY + 0.92f, position.z},
                 0.23f, 0.79f, 0.36f, clothes);
        DrawCube({position.x - 0.19f, baseY + 0.27f, position.z},
                 0.28f, 0.55f, 0.35f, shade(clothes, 0.65f));
        DrawCube({position.x + 0.19f, baseY + 0.27f, position.z},
                 0.28f, 0.55f, 0.35f, shade(clothes, 0.65f));
        if (player) {
            DrawCube({position.x + std::sin(yaw) * 0.29f, baseY + 1.68f,
                      position.z + std::cos(yaw) * 0.29f},
                     0.18f, 0.12f, 0.16f, Color{29, 50, 71, 255});
        }
    }
};

} // namespace rpg3d
