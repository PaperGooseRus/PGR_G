/**
 * @file main.cpp
 * @brief Единственная точка соединения независимых модулей: класс Application.
 * @details Графика и ввод не знают о SQLite. Хранилище не знает о raylib.
 *          Все команды передаются только через Application — подготовка к
 *          выделенному потоку симуляции и авторитетному серверу.
 */
#include "modules/world.hpp"
#include "modules/collision.hpp"
#include "modules/movement.hpp"
#include "modules/interaction.hpp"
#include "modules/camera.hpp"
#include "modules/renderer.hpp"
#include "modules/trade_rules.hpp"
#include "modules/rare_discovery.hpp"
#include "modules/ledger_store.hpp"
#include "raylib.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace rpg3d {
/**
 * @brief Составной корень программы, владеющий всеми её подсистемами.
 * @details Только здесь связываются графика, физика, мир, торговля и журнал.
 *          Коллекции игровых DTO меняются по событию, не в каждом кадре.
 */
class Application final {
public:
    /**
     * @brief Инициализировать игровую экономику и запустить CLI или raylib.
     * @param argc Число аргументов.
     * @param argv Команды: --verify, --inventory, --audit либо без параметров.
     * @return Код завершения. Проверки без графики не требуют окна.
     */
    int run(int argc,char** argv) {
        std::filesystem::create_directories("save");
        database_ = std::make_unique<LedgerStore>("save/game.sqlite");
        register_nature(world_.view());
        refresh_inventory();
        pendingOffer_=database_->pending_offer(kPlayer,kMerchant);
        if (pendingOffer_!=0) tradeSeconds_=0.2f; // Возобновление NPC после перезапуска.
        if (argc>1) return command_line(argv[1]);

        SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
        InitWindow(1280,720,"Кубическая деревня | RPG v0.2");
        SetWindowMinSize(960,540);
        SetTargetFPS(60);
        if (!renderer_.initialize()) {
            std::fprintf(stderr,"Не найден шрифт для кириллицы (Windows: Segoe UI/Arial).\n");
            CloseWindow();
            return 2;
        }
        DisableCursor();
        const SceneView scene=world_.view();
        lastSecond_=static_cast<std::int64_t>(std::time(nullptr));
        while (!WindowShouldClose()) {
            const float dt=std::clamp(GetFrameTime(),0.0f,Movement::kMaxDelta);
            update(scene,dt);
            draw(scene);
        }
        EnableCursor();
        renderer_.shutdown();
        CloseWindow();
        return 0;
    }

private:
    /** @brief Привязать дерево/руду/кристаллы к устойчивым индексам карты. */
    void register_nature(SceneView scene) {
        for (std::size_t i=0;i<scene.obstacles.count;++i) {
            const ObjectKind kind=scene.obstacles[i].kind;
            const std::int64_t nodeId=static_cast<std::int64_t>(i)+1;
            if (kind==ObjectKind::Tree || kind==ObjectKind::WoodPile)
                database_->register_node(nodeId,"wood",12,30);
            if (kind==ObjectKind::Ore)
                database_->register_node(nodeId,"ore",10,25);
            if (kind==ObjectKind::Rock)
                database_->register_node(nodeId,"gem",3,120);
        }
    }

    /** @brief Режимы разработки без окна; удобно для тестов и аудита. */
    int command_line(const std::string& option) {
        if (option=="--verify") {
            const VerifyResult vr=database_->verify();
            std::printf("Проверка журнала: %s; записей: %lld\n",vr.valid?"УСПЕХ":"ПОВРЕЖДЁН",
                static_cast<long long>(vr.checked));
            return vr.valid?0:1;
        }
        if (option=="--inventory") {
            std::printf("Личный инвентарь игрока:\nРуда=%lld, древесина=%lld, кристаллы=%lld\n",
                static_cast<long long>(inventory_.ore),static_cast<long long>(inventory_.wood),
                static_cast<long long>(inventory_.gem));
            std::printf("Золото=%lld, серебро=%lld, уникальные=%zu\n",
                static_cast<long long>(inventory_.gold),static_cast<long long>(inventory_.silver),
                inventory_.uniqueItems.size());
            return 0;
        }
        if (option=="--audit") {
            const auto records=database_->events_for(kPlayer,20);
            for (const EventRecord& e:records)
                std::printf("№%lld | %s | %s | SHA=%s\n",
                    static_cast<long long>(e.sequence),e.type.c_str(),e.payload.c_str(),e.hash.c_str());
            return 0;
        }
        std::printf("Режимы: --verify, --inventory, --audit; без параметров — 3D-игра.\n");
        return 1;
    }

    /** @brief Отложенная проверка состояния; не вызывается на каждом кадре. */
    void refresh_inventory() {
        const EconomyResult result=database_->inventory(kPlayer,kPlayer,inventory_);
        if (!result) throw std::runtime_error(result.message);
        discoveries_=database_->discovery_count();
        if (showJournal_) visibleEvents_=database_->events_for(kPlayer,9);
    }

    /** @brief Вывести локализованное сообщение на несколько секунд. */
    void inform(const EconomyResult& result) {
        notice_=result.message;
        noticeDuration_=4.0f;
        refresh_inventory();
    }

    /** @brief Добыча физического источника с проверкой его ID. */
    void take_resource(SceneView scene,std::size_t index) {
        if (index>=scene.obstacles.count) return;
        const std::int64_t id=static_cast<std::int64_t>(index)+1;
        const EconomyResult result=database_->harvest(kPlayer,id);
        inform(result);
        nearStock_=database_->node(id).remaining;
    }

    /** @brief Создать резерв по выбранному предложению, NPC подтвердит его чуть позже. */
    void offer_to_npc(const Asset& give,const Asset& take) {
        if (pendingOffer_!=0) {
            notice_="Сначала дождитесь завершения предыдущей сделки";
            noticeDuration_=3.0f;
            return;
        }
        if (!TradeRules::valid_offer(kPlayer,kMerchant,give,take)) {
            inform({EconomyCode::Invalid,"Неверные условия торгового контракта"});
            return;
        }
        const auto result=database_->create_offer(kPlayer,kMerchant,give,take);
        inform(result);
        if (result) {
            pendingOffer_=result.id;
            tradeSeconds_=0.9f;
        }
    }

    /** @brief Попытка редкого синтеза строго с проверкой правил ядром. */
    void attempt_rare(SceneView scene) {
        RareContext context=database_->ritual_context(kPlayer);
        context.atLaboratory=nearby_.type==Interaction::Type::Npc && nearby_.index==1 &&
            distance_squared(player_.position,scene.npcs[1].position)<6.25f;
        const float tau=6.28318530718f;
        float yaw=std::fmod(player_.yaw,tau);
        if (yaw<0.0f) yaw+=tau;
        context.direction=static_cast<unsigned>(std::floor((yaw+0.7853981634f)/1.5707963268f))%4U;
        const bool allowed=RareDiscovery::eligible(context,database_->rare_rule());
        if (!allowed) {
            inform({EconomyCode::ConditionsNotMet,"Не выполнена загадочная последовательность действий"});
            return;
        }
        inform(database_->synthesize1024(kPlayer,RareDiscovery::secure_roll(),true));
    }

    /** @brief Шаг ввода/физики/экономики. Изменяемое состояние принадлежит Application. */
    void update(SceneView scene,float dt) {
        noticeDuration_=std::max(0.0f,noticeDuration_-dt);
        if (IsKeyPressed(KEY_TAB)) {
            cursorReleased_=!cursorReleased_;
            if (cursorReleased_) EnableCursor(); else DisableCursor();
        }
        if (IsKeyPressed(KEY_F1)) {
            showHelp_=!showHelp_;
            if (showHelp_) {showInventory_=false;showJournal_=false;}
        }
        if (IsKeyPressed(KEY_I)) {
            showInventory_=!showInventory_;
            if (showInventory_) {showHelp_=false;showJournal_=false;}
        }
        if (IsKeyPressed(KEY_J)) {
            showJournal_=!showJournal_;
            if (showJournal_) {
                showHelp_=false;showInventory_=false;
                visibleEvents_=database_->events_for(kPlayer,9);
            }
        }

        const std::int64_t now=static_cast<std::int64_t>(std::time(nullptr));
        if (now>lastSecond_) {
            lastSecond_=now;
            if (database_->regenerate(now)>0 && nearby_.type==Interaction::Type::Resource)
                nearStock_=database_->node(static_cast<std::int64_t>(nearby_.index)+1).remaining;
        }
        if (pendingOffer_!=0) {
            tradeSeconds_-=dt;
            if (tradeSeconds_<=0.0f) {
                TradeOffer offer{};
                const auto info=database_->offer(kMerchant,pendingOffer_,offer);
                EconomyResult result{};
                if (!info || !TradeRules::npc_accepts(offer)) {
                    result=database_->cancel_offer(kPlayer,pendingOffer_);
                    if (result) result.message="NPC отклонил условия. Резерв возвращён";
                } else {
                    result=database_->accept_offer(kMerchant,pendingOffer_);
                    if (!result) {
                        database_->cancel_offer(kPlayer,pendingOffer_);
                        result.message += "; резерв возвращён";
                    }
                }
                pendingOffer_=0;
                inform(result);
            }
        }
        const bool controlled=!cursorReleased_ && !showHelp_ &&
            !showInventory_ && !showJournal_ && IsWindowFocused();
        if (controlled) {
            const Vector2 mouse=GetMouseDelta();
            player_.yaw+=mouse.x*0.0035f;
            cameraPitch_=std::clamp(cameraPitch_+mouse.y*0.0025f,0.13f,1.14f);
            cameraDistance_=std::clamp(cameraDistance_-GetMouseWheelMove(),3.0f,9.0f);
            const bool jump=IsKeyPressed(KEY_SPACE);
            const bool validJump=jump && player_.jumpHeight==0.0f && player_.verticalSpeed==0.0f;
            const MovementInput input{
                static_cast<float>(IsKeyDown(KEY_W))-static_cast<float>(IsKeyDown(KEY_S)),
                static_cast<float>(IsKeyDown(KEY_D))-static_cast<float>(IsKeyDown(KEY_A)),
                IsKeyDown(KEY_LEFT_SHIFT)||IsKeyDown(KEY_RIGHT_SHIFT),jump
            };
            const Vec2 displacement=Movement::step(player_,input,dt);
            player_.position=Collision::slide(scene,player_.position,displacement);
            if (validJump) database_->register_jump(kPlayer);
        }
        const Interaction lastNearby=nearby_;
        nearby_=InteractionSystem::closest(scene,player_.position);
        if (nearby_.type==Interaction::Type::Resource &&
            (lastNearby.type!=nearby_.type || lastNearby.index!=nearby_.index))
            nearStock_=database_->node(static_cast<std::int64_t>(nearby_.index)+1).remaining;
        if (nearby_.type!=Interaction::Type::Resource) nearStock_=-1;
        if (activeDialogue_>=0) {
            const auto index=static_cast<std::size_t>(activeDialogue_);
            if (index>=scene.npcs.count ||
                distance_squared(player_.position,scene.npcs[index].position)>12.25f)
                activeDialogue_=-1;
        }
        if (!controlled) return;
        if (IsKeyPressed(KEY_E)) {
            if (activeDialogue_>=0) activeDialogue_=-1;
            else if (nearby_.type==Interaction::Type::Npc && nearby_.index<scene.npcs.count)
                activeDialogue_=static_cast<int>(nearby_.index);
            else if (nearby_.type==Interaction::Type::Resource)
                take_resource(scene,nearby_.index);
        }
        const bool nextToAlchemist=nearby_.type==Interaction::Type::Npc && nearby_.index==1;
        if (nextToAlchemist) {
            if (IsKeyPressed(KEY_T))
                offer_to_npc(Asset{AssetKind::Resource,"ore",1,0},
                             Asset{AssetKind::Currency,"gold",10,0});
            if (IsKeyPressed(KEY_C))
                offer_to_npc(Asset{AssetKind::Currency,"gold",5,0},
                             Asset{AssetKind::Currency,"silver",20,0});
            if (IsKeyPressed(KEY_P)) inform(database_->meditate(kPlayer,now));
            if (IsKeyPressed(KEY_R)) attempt_rare(scene);
        }
    }

    /** @brief Создать непередающий владение снимок для отрисовки; без копий. */
    void draw(SceneView scene) const {
        const Camera3D camera=CameraSystem::make(scene,player_,cameraPitch_,cameraDistance_);
        BeginDrawing();
        ClearBackground(Color{138,190,215,255});
        BeginMode3D(camera);
        Renderer::scene(scene,player_);
        EndMode3D();
        renderer_.labels(scene,camera,player_.position);
        HudState hud{};
        hud.nearby=nearby_;
        hud.activeDialogue=activeDialogue_;
        hud.showHelp=showHelp_;
        hud.cursorReleased=cursorReleased_;
        hud.showInventory=showInventory_;
        hud.showJournal=showJournal_;
        hud.notice=noticeDuration_>0.0f?notice_.c_str():nullptr;
        hud.inventory=&inventory_;
        hud.events=visibleEvents_.data();
        hud.eventCount=visibleEvents_.size();
        hud.resourceRemaining=nearStock_;
        hud.discoveries=discoveries_;
        hud.pendingTrade=pendingOffer_!=0;
        renderer_.hud(scene,player_,hud);
        EndDrawing();
    }

    static constexpr ActorId kPlayer=1;
    static constexpr ActorId kMerchant=2;
    World world_{};
    Player player_{};
    Renderer renderer_{};
    std::unique_ptr<LedgerStore> database_{};
    InventorySnapshot inventory_{};
    std::vector<EventRecord> visibleEvents_{};
    Interaction nearby_{};
    std::string notice_{};
    std::int64_t discoveries_=0;
    std::int64_t nearStock_=-1;
    std::int64_t lastSecond_=0;
    std::int64_t pendingOffer_=0;
    float tradeSeconds_=0.0f;
    float noticeDuration_=0.0f;
    float cameraPitch_=0.36f;
    float cameraDistance_=5.9f;
    int activeDialogue_=-1;
    bool cursorReleased_=false;
    bool showHelp_=false;
    bool showInventory_=false;
    bool showJournal_=false;
};
} // namespace rpg3d

/** @brief Единственная точка входа. */
int main(int argc,char** argv) {
    try {
        rpg3d::Application game;
        return game.run(argc,argv);
    } catch (const std::exception& e) {
        std::fprintf(stderr,"Ошибка запуска RPG: %s\n",e.what());
        return 1;
    }
}
