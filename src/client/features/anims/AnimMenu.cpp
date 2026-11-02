#include "features/anims/AnimMenu.h"

#include <windows.h>

#include <algorithm>
#include <string>
#include <vector>

#include "common/lib/log/Log.h"
#include "common/lib/util/Util.h"
#include "features/duel/Duel.h"
#include "pages/connect-menu/ConnectMenu.h"
#include "shared/config/Config.h"
#include "shared/game/Anim.h"
#include "shared/game/Game.h"
#include "shared/game/Script.h"
#include "shared/lib/core/Core.h"
#include "shared/ui/chat/Chat.h"
#include "shared/ui/input/Input.h"
#include "shared/ui/renderer/Renderer.h"
#include "shared/ui/widgets/Widgets.h"

namespace coop {

namespace {
using namespace game;
using game::script::Cmd;
using ui::w::kSaBlue, ui::w::kSaSlate, ui::w::kSaMuted, ui::w::kSaRed, ui::w::kSaLine, ui::w::kSaGreen;

struct AnimDef {
    const char* nameRu;
    const char* nameEn;
    const char* block;
    const char* anim;
    bool loop;
    int category;  // 0: dances, 1: stances, 2: actions, 3: gestures, 4: sports
};

const AnimDef kAnims[] = {
    // ---- 0: Dances (Танцы) ----
    {"Танец 1", "Dance 1", "DANCING", "dance_loop", true, 0},
    {"Танец 2", "Dance 2", "DANCING", "dan_down_a", true, 0},
    {"Танец 3", "Dance 3", "DANCING", "dan_right_a", true, 0},
    {"Танец 4", "Dance 4", "DANCING", "dan_loop_a", true, 0},
    {"G-Funk танец", "G-Funk Dance", "GFUNK", "dance_loop", true, 0},
    {"Running Man", "Running Man", "RUNNINGMAN", "dance_loop", true, 0},
    {"WOP танец", "WOP Dance", "WOP", "dance_loop", true, 0},
    {"Стриптиз 1", "Strip 1", "STRIP", "strip_a", true, 0},
    {"Стриптиз 2", "Strip 2", "STRIP", "strip_b", true, 0},
    {"Стриптиз 3", "Strip 3", "STRIP", "strip_c", true, 0},
    {"Стриптиз 4", "Strip 4", "STRIP", "strip_d", true, 0},
    {"Стриптиз 5", "Strip 5", "STRIP", "strip_e", true, 0},

    // ---- 1: Stances / Poses (Стойки и позы) ----
    {"Руки за спину", "Hands Behind Back", "COP_AMBIENT", "coplook_loop", true, 1},
    {"Скрестить руки", "Cross Arms", "DEALER", "dealer_idle", true, 1},
    {"Прислониться к стене", "Lean on Wall", "MISC", "plyrsit_loop", true, 1},
    {"Сидеть на земле", "Sit on Ground", "BEACH", "bather", true, 1},
    {"Лежать на спине", "Lie on Back", "BEACH", "lay_bac_loop", true, 1},
    {"Загорать на боку", "Sunbathe", "BEACH", "sitnwait_loop_w", true, 1},
    {"Руки вверх", "Hands Up", "PED", "handsup", true, 1},
    {"Стойка у стены", "Lean on Wall", "GANGS", "leanIDLE", true, 1},
    {"Опереться на бар", "Lean on Bar", "BAR", "barman_idle", true, 1},
    {"Охранять", "Guard Stance", "GRAVEYARD", "mrnD_loop", true, 1},
    {"Поза размышления", "Thinking Pose", "COP_AMBIENT", "copbrowse_loop", true, 1},

    // ---- 2: Actions (Действия) ----
    {"Курить", "Smoke Cigarette", "SMOKING", "M_smkstnd_loop", true, 2},
    {"Пить напиток", "Drink Beverage", "BAR", "dnk_stnd_l", true, 2},
    {"Пить из бокала", "Drink Wine", "BAR", "dnk_stnd_m", true, 2},
    {"Есть бургер", "Eat Burger", "FOOD", "eat_burger", true, 2},
    {"Есть пиццу", "Eat Pizza", "FOOD", "eat_pizza", true, 2},
    {"Пьяная походка", "Drunk Walk", "PED", "walk_drunk", true, 2},
    {"Разговор по телефону", "Phone Call", "PED", "phone_talk", true, 2},
    {"Откачать (СЛР)", "CPR", "MEDIC", "cpr", true, 2},
    {"Поцелуй", "Kiss", "KISSING", "grlfrd_kiss_01", false, 2},
    {"Заложить бомбу", "Plant Bomb", "BOMBER", "bom_plant_loop", true, 2},
    {"Копаться в карманах", "Search Pockets", "DEALER", "shop_pay", true, 2},

    // ---- 3: Gestures (Жесты) ----
    {"Помахать рукой", "Wave Hand", "PED", "endchat_01", false, 3},
    {"Бандитский жест 1", "Gang Sign 1", "GHANDS", "gsign1", false, 3},
    {"Бандитский жест 2", "Gang Sign 2", "GHANDS", "gsign2", false, 3},
    {"Бандитский жест 3", "Gang Sign 3", "GHANDS", "gsign3", false, 3},
    {"Бандитский жест 4", "Gang Sign 4", "GHANDS", "gsign4", false, 3},
    {"Бандитский жест 5", "Gang Sign 5", "GHANDS", "gsign5", false, 3},
    {"Смех", "Laugh", "RAPPING", "laugh_01", false, 3},
    {"Рукопожатие", "Handshake", "GANGS", "hndshkfa", false, 3},
    {"Пощечина", "Slap", "MISC", "bitchslap", false, 3},
    {"Крик ярости", "Riot Shout", "RIOT", "riot_shout", true, 3},
    {"Аплодисменты", "Clap", "OTB", "wtchr_shout", true, 3},
    {"Почесать затылок", "Scratch Head", "MISC", "scratchballs_01", false, 3},

    // ---- 4: Sports (Спорт) ----
    {"Тай-чи", "Tai Chi", "PARK", "tai_chi_loop", true, 4},
    {"Бег на дорожке", "Treadmill Run", "GYMNASIUM", "gym_tread_walk", true, 4},
    {"Качать бицепс", "Dumbbells", "FREEWEIGHTS", "gym_free_loop", true, 4},
    {"Жим штанги", "Bench Press", "BENCHPRESS", "gym_bp_celebrate", true, 4},
    {"Бой с тенью", "Shadow Boxing", "GYMNASIUM", "gym_shadowbox", true, 4},
    {"Разминка", "Workout Stretch", "GYMNASIUM", "gym_tread_celebrate", false, 4},
};

constexpr size_t kAnimsCount = sizeof(kAnims) / sizeof(kAnims[0]);

bool g_visible = false;
int g_category = -1;  // -1: All, 0..4: Specific category
int g_page = 0;
constexpr int kPageSize = 10;
int g_playingIdx = -1;
bool g_playing = false;

void EnsureBlock(const char* block) {
    if (!block || !_stricmp(block, "PED")) return;
    int idx = Call<0x4D3990, int>(block);  // CAnimManager::GetAnimationBlockIndex
    if (idx >= 0) {
        if (!anim::BlockLoaded(idx)) {
            RequestModel(anim::kIfpBase + idx, 2 | 0x10 /* GAME_REQUIRED | PRIORITY_REQUEST */);
            LoadAllRequestedModels(false);
        }
    } else {
        Cmd(0x04ED).S(block).Run();
        LoadAllRequestedModels(false);
    }
}

void PlayAnim(int idx) {
    if (idx < 0 || idx >= (int)kAnimsCount) return;
    CPlayerPed* me = FindPlayerPed(0);
    if (!me || me->InVehicle() || me->Health() <= 0.0f) {
        ui::TheChat().AddError(TR("Нельзя воспроизвести анимацию в транспорте или при смерти.",
                                  "Cannot play animation in vehicle or when dead."));
        return;
    }
    const auto& def = kAnims[idx];
    EnsureBlock(def.block);
    int handle = GetPedRef(me);
    Cmd(0x0605).I(handle).S(def.anim).S(def.block).F(4.0f).I(def.loop ? 1 : 0).I(0).I(0).I(0).I(-1).Run();
    g_playingIdx = idx;
    g_playing = true;
    LOGI("Played animation %s from block %s", def.anim, def.block);
}

void StopAnim() {
    if (!g_playing && g_playingIdx < 0) return;
    CPlayerPed* me = FindPlayerPed(0);
    if (me) {
        Cmd(0x0687).I(GetPedRef(me)).Run();  // CLEAR_CHAR_TASKS
    }
    g_playing = false;
    g_playingIdx = -1;
}

}  // namespace

namespace ui {

void SetAnimMenuVisible(bool visible) {
    if (visible == g_visible) return;
    g_visible = visible;
    if (visible) {
        if (IsConnectMenuVisible()) SetConnectMenuVisible(false);
        if (IsDuelMenuVisible()) SetDuelMenuVisible(false);
        if (TheChat().IsOpen()) TheChat().Close();
        In().SetKeyboardCapture(true);
        In().SetMouseCapture(true);
    } else {
        if (!IsConnectMenuVisible() && !IsDuelMenuVisible()) {
            In().SetKeyboardCapture(false);
            In().SetMouseCapture(false);
        }
    }
}

bool IsAnimMenuVisible() { return g_visible; }

void ProcessAnimMenu() {}

void RenderAnimMenu() {
    if (!g_visible) return;
    auto& g = Gfx();
    float s = g.Scale();

    float lh = g.LineHeight(FONT_NORMAL), sh = g.LineHeight(FONT_SMALL),
          ch = g.LineHeight(FONT_MENU_SMALL);
    float W = 760 * s, H = 570 * s;
    float x = (g.Width() - W) * 0.5f, y = (g.Height() - H) * 0.5f;
    float cx = x + 36 * s, cw = W - 72 * s;

    g.Rect(0, 0, g.Width(), g.Height(), 0x99000000);
    w::BeginFrame();

    float cy = w::Panel(x, y, W, H, "ANIMATIONS") + 4 * s;

    // ---- Category Tabs ----
    const struct {
        const char* labelRu;
        const char* labelEn;
        int cat;
    } cats[] = {
        {"Все", "All", -1},
        {"Танцы", "Dances", 0},
        {"Позы", "Poses", 1},
        {"Действия", "Actions", 2},
        {"Жесты", "Gestures", 3},
        {"Спорт", "Sports", 4},
    };

    float tabW = cw / 6.0f;
    for (int i = 0; i < 6; ++i) {
        char tabId[16];
        snprintf(tabId, sizeof tabId, "acat_%d", i);
        if (w::Tab(tabId, cx + (float)i * tabW, cy, tabW, ch + 8 * s, TR(cats[i].labelRu, cats[i].labelEn),
                   g_category == cats[i].cat, true)) {
            g_category = cats[i].cat;
            g_page = 0;
        }
    }
    cy += ch + 14 * s;

    // Filter items
    std::vector<int> filtered;
    for (size_t i = 0; i < kAnimsCount; ++i) {
        if (g_category == -1 || kAnims[i].category == g_category) filtered.push_back((int)i);
    }

    int totalPages = std::max(1, ((int)filtered.size() + kPageSize - 1) / kPageSize);
    if (g_page >= totalPages) g_page = totalPages - 1;
    if (g_page < 0) g_page = 0;

    // Grid of buttons: 2 columns of 5 items
    float gridTop = cy + 4 * s;
    float colW = (cw - 16 * s) * 0.5f;
    float btnH = lh + 10 * s;

    int startIdx = g_page * kPageSize;
    int endIdx = std::min((int)filtered.size(), startIdx + kPageSize);

    for (int i = startIdx; i < endIdx; ++i) {
        int slot = i - startIdx;
        int col = slot % 2;
        int row = slot / 2;
        float bx = cx + (float)col * (colW + 16 * s);
        float by = gridTop + (float)row * (btnH + 6 * s);
        int animIdx = filtered[i];
        bool lit = (g_playing && g_playingIdx == animIdx);

        char btnId[24];
        snprintf(btnId, sizeof btnId, "animb_%d", animIdx);
        std::string label = TR(kAnims[animIdx].nameRu, kAnims[animIdx].nameEn);
        if (lit) label = "▶ " + label;

        if (w::Button(btnId, bx, by, colW, btnH, label, true, lit)) {
            PlayAnim(animIdx);
        }
    }

    float navY = gridTop + 5.0f * (btnH + 6 * s) + 8 * s;

    // Pagination
    float navBtnW = 140 * s;
    if (w::Button("page_prev", cx, navY, navBtnW, btnH, TR("< Предыдущая", "< Previous"), g_page > 0)) {
        --g_page;
    }

    std::string pageStr = Format(TR("Страница %d из %d (%d аним.)", "Page %d of %d (%d anims)"), g_page + 1, totalPages,
                                 (int)filtered.size());
    g.Text(FONT_NORMAL, cx + cw * 0.5f, navY + (btnH - lh) * 0.5f, pageStr, kSaBlue, TEXT_CENTER | TEXT_SHADOW);

    if (w::Button("page_next", cx + cw - navBtnW, navY, navBtnW, btnH, TR("Следующая >", "Next >"),
                  g_page + 1 < totalPages)) {
        ++g_page;
    }

    // Divider line
    float divY = navY + btnH + 12 * s;
    g.Rect(cx, divY, cw, std::max(1.0f, s), kSaLine);

    // Footer
    float footerY = divY + 12 * s;
    float stopW = 260 * s;
    std::string stopText = TR("■ Остановить анимацию", "■ Stop animation");
    if (w::Button("stop_anim", cx, footerY, stopW, btnH, stopText, true, g_playing)) {
        StopAnim();
    }

    std::string closeText = TR("Закрыть", "Close");
    float closeW = g.TextWidth(FONT_MENU, closeText) + 30 * s;
    if (w::Button("close_anim", cx + cw - closeW, footerY, closeW, btnH, closeText)) {
        SetAnimMenuVisible(false);
    }

    float hintY = y + H - 16 * s - sh;
    g.Text(FONT_SMALL, cx, hintY,
           TR("{ACCBF1}ESC{8D9CAD} закрыть      {ACCBF1}Клик{8D9CAD} воспроизвести      "
              "{ACCBF1}W/A/S/D{8D9CAD} прервать движение",
              "{ACCBF1}ESC{8D9CAD} close      {ACCBF1}Click{8D9CAD} play      "
              "{ACCBF1}W/A/S/D{8D9CAD} interrupt movement"),
           kSaMuted, TEXT_SHADOW);

    w::EndFrame();
    w::DrawCursor();
}

}  // namespace ui

class AnimModule : public Module {
public:
    const char* Name() const override { return "AnimMenu"; }

    void OnGameShutdown() override {
        StopAnim();
        ui::SetAnimMenuVisible(false);
    }

    void OnPostProcess() override {
        if (!g_playing) return;
        CPlayerPed* me = FindPlayerPed(0);
        if (!me || me->Health() <= 0.0f || me->InVehicle()) {
            StopAnim();
            return;
        }
        // If movement, combat or jump controls touched, stop the animation smoothly
        CPad* pad = game::GetPad(0);
        if (pad && (pad->NewState.LeftStickX != 0 || pad->NewState.LeftStickY != 0 ||
                    pad->NewState.ButtonCross || pad->NewState.ButtonSquare ||
                    pad->NewState.RightShoulder1 || pad->NewState.LeftShoulder1 ||
                    pad->NewState.ShockButtonL)) {
            StopAnim();
        }
    }

    bool OnCommand(const std::string& cmd, const std::vector<std::string>& args) override {
        if (cmd == "anim" || cmd == "anims" || cmd == "animation" || cmd == "аним" || cmd == "анимации" || cmd == "анимация") {
            if (!args.empty()) {
                std::string sub = ToLower(args[0]);
                if (sub == "stop" || sub == "clear" || sub == "cancel" || sub == "стоп" || sub == "отмена") {
                    StopAnim();
                    ui::TheChat().AddSystem(TR("Анимация остановлена.", "Animation stopped."));
                    return true;
                }
                if (sub == "list" || sub == "menu" || sub == "help") {
                    ui::SetAnimMenuVisible(true);
                    return true;
                }
                // Try to find anim by prefix/name across joined arguments
                std::string search;
                for (size_t i = 0; i < args.size(); ++i) {
                    if (i > 0) search += " ";
                    search += args[i];
                }
                search = ToLower(search);

                for (size_t i = 0; i < kAnimsCount; ++i) {
                    if (ToLower(kAnims[i].anim).find(search) != std::string::npos ||
                        ToLower(kAnims[i].nameRu).find(search) != std::string::npos ||
                        ToLower(kAnims[i].nameEn).find(search) != std::string::npos) {
                        PlayAnim((int)i);
                        ui::TheChat().AddSystem(Format(TR("Воспроизведение: %s", "Playing: %s"), kAnims[i].nameRu));
                        return true;
                    }
                }
            }
            ui::SetAnimMenuVisible(!ui::IsAnimMenuVisible());
            return true;
        }
        return false;
    }
};

static AnimModule g_animModule;
struct RegAnim {
    RegAnim() { core::RegisterModule(&g_animModule); }
} g_regAnim;

}  // namespace coop
