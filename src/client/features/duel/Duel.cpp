#include "features/duel/Duel.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "common/api/protocol/Protocol.h"
#include "common/lib/log/Log.h"
#include "common/lib/util/Util.h"
#include "entities/player/Players.h"
#include "features/anims/AnimMenu.h"
#include "pages/connect-menu/ConnectMenu.h"
#include "shared/api/net-session/Session.h"
#include "shared/config/Config.h"
#include "shared/game/Anim.h"
#include "shared/game/Game.h"
#include "shared/game/Script.h"
#include "shared/lib/core/Core.h"
#include "shared/ui/chat/Chat.h"
#include "shared/ui/input/Input.h"
#include "shared/ui/renderer/Renderer.h"
#include "shared/ui/widgets/Widgets.h"

namespace coop::duel {

namespace {
using namespace game;
using namespace ui;
using game::script::Cmd;
using ui::w::kSaBlue, ui::w::kSaSlate, ui::w::kSaMuted, ui::w::kSaRed, ui::w::kSaLine, ui::w::kSaGreen;
constexpr uint16_t kDuelControlsBit = 0x2000;
constexpr uint32_t kProofMask = CPhysical::PF_BULLET_PROOF | CPhysical::PF_FIRE_PROOF |
                                CPhysical::PF_COLLISION_PROOF | CPhysical::PF_MELEE_PROOF |
                                CPhysical::PF_EXPLOSION_PROOF;
constexpr uint32_t kSubmerged = 1u << 8;  // CPhysical::physicalFlags.bSubmergedInWater

enum Op : uint8_t {
    OP_INVITE = 1,
    OP_ACCEPT = 2,
    OP_DECLINE = 3,
    OP_START = 4,
    OP_FINISH = 5,
    OP_CANCEL = 6
};

const std::vector<ArenaDef> g_arenas = {
    {
        "Заброшенный аэродром", "Verdant Meadows Airstrip",
        0,
        {385.0f, 2535.0f, 17.5f}, 90.0f,
        {415.0f, 2535.0f, 17.5f}, 270.0f
    },
    {
        "Крыша небоскрёба LS", "Los Santos Tower Roof",
        0,
        {1538.0f, -1353.0f, 330.0f}, 90.0f,
        {1550.0f, -1353.0f, 330.0f}, 270.0f
    },
    {
        "Аэропорт Лас-Вентурас", "Las Venturas Runway",
        0,
        {1390.0f, 1260.0f, 11.5f}, 90.0f,
        {1420.0f, 1260.0f, 11.5f}, 270.0f
    },
    {
        "Вершина горы Чилиад", "Mount Chiliad Peak",
        0,
        {-2305.0f, -1640.0f, 484.5f}, 90.0f,
        {-2325.0f, -1640.0f, 484.5f}, 270.0f
    },
    {
        "Текущее место", "Current Location",
        0,
        {0, 0, 0}, 0.0f,
        {0, 0, 0}, 0.0f
    }
};

const std::vector<WeaponDef> g_weapons = {
    {"Desert Eagle", "Desert Eagle", 24, 150},
    {"M4", "M4", 31, 300},
    {"AK-47", "AK-47", 30, 300},
    {"Дробовик", "Shotgun", 25, 100},
    {"SPAS-12", "Combat Shotgun", 27, 100},
    {"MP5", "MP5", 29, 300},
    {"Micro Uzi", "Micro Uzi", 28, 300},
    {"Снайперская винтовка", "Sniper Rifle", 34, 50},
    {"Винтовка", "Rifle", 33, 50},
    {"Катана", "Katana", 8, 1},
    {"Кулаки", "Fists", 0, 1},
    {"РПГ", "RPG", 35, 20}
};

const int32_t kPresetBets[] = {0, 100, 250, 500, 1000, 2500, 5000, 10000, 25000, 50000, 100000};
constexpr size_t kPresetBetsCount = sizeof(kPresetBets) / sizeof(kPresetBets[0]);

State g_state = State::Idle;
bool g_menuVisible = false;

uint8_t g_opponentId = 255;
int g_selectedWeaponIdx = 0;
int32_t g_selectedBet = 1000;
int g_selectedArenaIdx = 0;
size_t g_selectedOpponentPeerIdx = 0;

// Outgoing challenge
uint64_t g_inviteSentMs = 0;

// Incoming challenge
struct IncomingInvite {
    bool active = false;
    uint8_t challengerId = 255;
    int weaponIdx = 0;
    int32_t bet = 0;
    int arenaIdx = 0;
    CVector pos{};
    int8_t area = 0;
    float heading = 0.0f;
    uint64_t recvMs = 0;
} g_incoming;

// Active duel state
uint64_t g_countdownStartMs = 0;
uint64_t g_fightStartMs = 0;
int g_matchWeaponIdx = 0;
int32_t g_matchBet = 0;
int g_matchArenaIdx = 0;

// Saved pre-duel player state
CVector g_savedPos{};
int8_t g_savedArea = 0;
float g_savedHeading = 0.0f;
float g_savedHealth = 100.0f;
float g_savedArmour = 0.0f;
uint32_t g_savedProofs = 0;
struct SavedSlot { int type = 0, ammo = 0; } g_savedWeapons[13];
int g_savedActiveWeapon = 0;

void ClearStars() {
    void* w = Call<0x56E230, void*>(-1);  // FindPlayerWanted
    if (w && Field<int32_t>(w, 0x2C) > 0) Cmd(0x0110).I(0).Run();  // CLEAR_WANTED_LEVEL
}

void StartDuelMatch(uint8_t opponentId, int weaponIdx, int32_t bet, int arenaIdx, bool isChallenger) {
    CPlayerPed* me = FindPlayerPed(0);
    if (!me || me->Health() <= 0.0f) return;

    ui::SetDuelMenuVisible(false);
    ui::SetAnimMenuVisible(false);
    ui::SetConnectMenuVisible(false);

    g_opponentId = opponentId;
    g_matchWeaponIdx = weaponIdx;
    g_matchBet = bet;
    g_matchArenaIdx = arenaIdx;

    // Deduct bet from money
    if (bet > 0) {
        PlayerInfos()[0].money = std::max(0, PlayerInfos()[0].money - bet);
        PlayerInfos()[0].displayMoney = PlayerInfos()[0].money;
    }

    // Save pre-duel state
    g_savedPos = me->GetPosition();
    g_savedArea = me->AreaCode();
    g_savedHeading = me->CurrentRotation();
    g_savedHealth = me->Health();
    g_savedArmour = me->Armour();
    g_savedProofs = me->PhysicalFlags() & kProofMask;
    for (int i = 0; i < 13; ++i) {
        g_savedWeapons[i] = {me->Weapons()[i].type, me->Weapons()[i].ammoTotal};
    }
    g_savedActiveWeapon = me->ActiveWeapon().type;

    // If in vehicle, exit immediately
    int handle = GetPedRef(me);
    if (me->InVehicle()) {
        Cmd(0x0362).I(handle).F(g_savedPos.x).F(g_savedPos.y).F(g_savedPos.z).Run();
    }

    // Determine arena spawn
    const auto& arena = g_arenas[arenaIdx];
    CVector spawnPos;
    float spawnHeading = 0.0f;
    int targetArea = arena.area;

    if (arenaIdx == 4) {  // Current Location
        targetArea = g_savedArea;
        if (isChallenger) {
            spawnPos = g_savedPos;
            spawnHeading = g_savedHeading;
        } else {
            float rad = g_incoming.heading * 0.017453292f;
            spawnPos = g_incoming.pos + CVector{std::sin(-rad) * 8.0f, std::cos(-rad) * 8.0f, 0.0f};
            spawnHeading = g_incoming.heading + 180.0f;
            float gz = 0.0f;
            if (Cmd(0x02CE).F(spawnPos.x).F(spawnPos.y).F(g_incoming.pos.z + 5.0f).Out(&gz).Run() && gz > -50.0f) {
                spawnPos.z = gz;
            } else {
                spawnPos.z = g_incoming.pos.z;
            }
        }
    } else {
        if (isChallenger) {
            spawnPos = arena.spawnChallenger;
            spawnHeading = arena.headingChallenger;
        } else {
            spawnPos = arena.spawnOpponent;
            spawnHeading = arena.headingOpponent;
        }
    }

    // Freeze character in place during loading & countdown so opponent never falls through ground
    Cmd(0x0350).I(handle).I(1).Run();                  // SET_CHAR_STAY_IN_SAME_PLACE (Freeze)
    Cmd(0x04BB).I(targetArea).Run();                  // SET_AREA_VISIBLE
    Cmd(0x0860).I(handle).I(targetArea).Run();         // SET_CHAR_AREA_VISIBLE
    Cmd(0x04E4).F(spawnPos.x).F(spawnPos.y).Run();     // REQUEST_COLLISION
    Cmd(0x03CB).F(spawnPos.x).F(spawnPos.y).F(spawnPos.z).Run();  // LOAD_SCENE
    Cmd(0x00A1).I(handle).F(spawnPos.x).F(spawnPos.y).F(spawnPos.z + 0.5f).Run();  // SET_CHAR_COORDINATES (+0.5f)
    LightArea(targetArea, spawnPos);
    me->CurrentRotation() = me->AimingRotation() = spawnHeading;
    me->MoveSpeed() = {0.0f, 0.0f, 0.0f}; me->TurnSpeed() = {0.0f, 0.0f, 0.0f};
    Cmd(0x0373).Run();  // SET_CAMERA_BEHIND_PLAYER

    // Heal to full health & armour
    me->Health() = 100.0f;
    me->Armour() = 100.0f;

    // Give duel weapon
    Cmd(0x048F).I(handle).Run();  // REMOVE_ALL_CHAR_WEAPONS
    if (weaponIdx >= 0 && weaponIdx < (int)g_weapons.size()) {
        const auto& wep = g_weapons[weaponIdx];
        if (wep.type > 0) {
            int model = -1;
            Cmd(0x0781).I(wep.type).Out(&model).Run();  // GET_WEAPONTYPE_MODEL
            if (model > 0 && !IsModelLoaded(model)) LoadModelNow(model);
            Cmd(0x01B2).I(handle).I(wep.type).I(wep.ammo).Run();  // GIVE_WEAPON_TO_CHAR
            Cmd(0x01B9).I(handle).I(wep.type).Run();               // SET_CURRENT_CHAR_WEAPON
        }
    }

    // Ignore police during duel
    Cmd(0x01F7).I(0).I(1).Run();  // SET_POLICE_IGNORE_PLAYER
    ClearStars();

    // Start 3-second countdown
    g_countdownStartMs = NowMs();
    g_state = State::StartingCountdown;
    CPad* pad = game::GetPad(0);
    if (pad) pad->DisablePlayerControls |= kDuelControlsBit;
    me->SetProofs(true, true, true, true, true);

    LOGI("Duel match started against peer %d (weapon %d, bet %d, arena %d)", opponentId, weaponIdx, bet, arenaIdx);
}

void HandleFinish(uint8_t winnerId, uint8_t loserId) {
    if (g_state != State::InProgress && g_state != State::StartingCountdown) return;

    bool iWon = (winnerId == Net().MyId());
    bool iLost = (loserId == Net().MyId());
    CPlayerPed* me = FindPlayerPed(0);
    int handle = me ? GetPedRef(me) : -1;

    CPad* pad = game::GetPad(0);
    if (pad) pad->DisablePlayerControls &= (uint16_t)~kDuelControlsBit;
    if (handle >= 0) Cmd(0x0350).I(handle).I(0).Run();  // Unfreeze
    if (me) me->PhysicalFlags() = (me->PhysicalFlags() & ~kProofMask) | g_savedProofs;
    Cmd(0x01F7).I(0).I(0).Run();  // SET_POLICE_IGNORE_PLAYER: off
    ClearStars();

    if (iWon) {
        int32_t pot = g_matchBet * 2;
        PlayerInfos()[0].money += pot;
        PlayerInfos()[0].displayMoney = PlayerInfos()[0].money;
        ui::TheChat().AddSystem(
            Format(TR("{5FA04E}★ Вы победили в дуэли против %s! Выигрыш: $%d ★{FFFFFF}",
                      "{5FA04E}★ You won the duel against %s! Prize: $%d ★{FFFFFF}"),
                   Net().PeerName(loserId).c_str(), pot));
    } else if (iLost) {
        ui::TheChat().AddSystem(
            Format(TR("{C8282D}Вы проиграли в дуэли против %s! Потеряно: $%d.{FFFFFF}",
                      "{C8282D}You lost the duel against %s! Lost: $%d.{FFFFFF}"),
                   Net().PeerName(winnerId).c_str(), g_matchBet));
    }

    // Restore health, armour, weapons, position
    if (me && handle >= 0) {
        me->Health() = std::max(50.0f, g_savedHealth);
        me->Armour() = g_savedArmour;

        // Restore weapons
        Cmd(0x048F).I(handle).Run();
        for (int i = 0; i < 13; ++i) {
            int type = g_savedWeapons[i].type;
            int ammo = g_savedWeapons[i].ammo;
            if (type <= 0) continue;
            if (ammo <= 0 && type >= 16) continue;
            int model = -1;
            Cmd(0x0781).I(type).Out(&model).Run();
            if (model > 0 && !IsModelLoaded(model) && !LoadModelNow(model)) continue;
            Cmd(0x01B2).I(handle).I(type).I(std::max(ammo, 1)).Run();
        }
        if (g_savedActiveWeapon > 0) {
            Cmd(0x01B9).I(handle).I(g_savedActiveWeapon).Run();
        }

        // Teleport back to saved position cleanly and safely
        Cmd(0x04BB).I(g_savedArea).Run();
        Cmd(0x0860).I(handle).I(g_savedArea).Run();
        Cmd(0x04E4).F(g_savedPos.x).F(g_savedPos.y).Run();  // REQUEST_COLLISION
        Cmd(0x03CB).F(g_savedPos.x).F(g_savedPos.y).F(g_savedPos.z).Run();
        Cmd(0x00A1).I(handle).F(g_savedPos.x).F(g_savedPos.y).F(g_savedPos.z + 0.3f).Run();  // SET_CHAR_COORDINATES (+0.3f)
        LightArea(g_savedArea, g_savedPos);
        me->CurrentRotation() = me->AimingRotation() = g_savedHeading;
        me->MoveSpeed() = {0.0f, 0.0f, 0.0f}; me->TurnSpeed() = {0.0f, 0.0f, 0.0f};
        Cmd(0x0373).Run();  // SET_CAMERA_BEHIND_PLAYER
    }

    g_state = State::Idle;
    g_opponentId = 255;
    g_incoming.active = false;
}

void AbortMatch(const std::string& reason) {
    if (g_state != State::InProgress && g_state != State::StartingCountdown) return;

    // Refund bet
    if (g_matchBet > 0) {
        PlayerInfos()[0].money += g_matchBet;
        PlayerInfos()[0].displayMoney = PlayerInfos()[0].money;
    }
    if (!reason.empty()) {
        ui::TheChat().AddSystem(reason);
    }

    CPlayerPed* me = FindPlayerPed(0);
    int handle = me ? GetPedRef(me) : -1;
    CPad* pad = game::GetPad(0);
    if (pad) pad->DisablePlayerControls &= (uint16_t)~kDuelControlsBit;
    if (handle >= 0) Cmd(0x0350).I(handle).I(0).Run();  // Unfreeze
    if (me) me->PhysicalFlags() = (me->PhysicalFlags() & ~kProofMask) | g_savedProofs;
    Cmd(0x01F7).I(0).I(0).Run();
    ClearStars();

    if (me && handle >= 0) {
        me->Health() = std::max(50.0f, g_savedHealth);
        me->Armour() = g_savedArmour;
        Cmd(0x048F).I(handle).Run();
        for (int i = 0; i < 13; ++i) {
            int type = g_savedWeapons[i].type;
            int ammo = g_savedWeapons[i].ammo;
            if (type <= 0) continue;
            if (ammo <= 0 && type >= 16) continue;
            int model = -1;
            Cmd(0x0781).I(type).Out(&model).Run();
            if (model > 0 && !IsModelLoaded(model) && !LoadModelNow(model)) continue;
            Cmd(0x01B2).I(handle).I(type).I(std::max(ammo, 1)).Run();
        }
        if (g_savedActiveWeapon > 0) {
            Cmd(0x01B9).I(handle).I(g_savedActiveWeapon).Run();
        }
        Cmd(0x04BB).I(g_savedArea).Run();
        Cmd(0x0860).I(handle).I(g_savedArea).Run();
        Cmd(0x04E4).F(g_savedPos.x).F(g_savedPos.y).Run();
        Cmd(0x03CB).F(g_savedPos.x).F(g_savedPos.y).F(g_savedPos.z).Run();
        Cmd(0x00A1).I(handle).F(g_savedPos.x).F(g_savedPos.y).F(g_savedPos.z + 0.3f).Run();
        LightArea(g_savedArea, g_savedPos);
        me->CurrentRotation() = me->AimingRotation() = g_savedHeading;
        me->MoveSpeed() = {0.0f, 0.0f, 0.0f}; me->TurnSpeed() = {0.0f, 0.0f, 0.0f};
        Cmd(0x0373).Run();
    }

    g_state = State::Idle;
    g_opponentId = 255;
    g_incoming.active = false;
}

}  // namespace

const std::vector<ArenaDef>& GetArenas() { return g_arenas; }
const std::vector<WeaponDef>& GetWeapons() { return g_weapons; }

State GetState() { return g_state; }

bool IsActive() {
    return g_state == State::StartingCountdown || g_state == State::InProgress;
}

bool IsDuelingWith(uint8_t peerId) {
    return IsActive() && g_opponentId == peerId;
}

void OnLocalDefeat() {
    if (g_state != State::InProgress && g_state != State::StartingCountdown) return;

    CPlayerPed* me = FindPlayerPed(0);
    if (me) {
        me->Health() = 100.0f;
        Cmd(0x0792).I(GetPedRef(me)).Run();
    }

    uint8_t opp = g_opponentId;
    Writer w(proto::G_DUEL);
    w.U8(OP_FINISH);
    w.U8(opp);            // winner is opponent
    w.U8(Net().MyId());   // loser is me
    w.I32(g_matchBet * 2);
    Net().Send(proto::CH_RELIABLE, w);

    HandleFinish(opp, Net().MyId());
}

void SendInvite(uint8_t targetId, int weaponIdx, int32_t bet, int arenaIdx) {
    if (!Net().Online() || !core::IsPlayerReady()) {
        ui::TheChat().AddError(TR("Вы не в игре или не подключены.", "You are not in game or not connected."));
        return;
    }
    if (IsActive() || g_state == State::Inviting) {
        ui::TheChat().AddError(TR("Вы уже находитесь в дуэли или ожидаете ответа.", "You are already in a duel or waiting for reply."));
        return;
    }
    if (PlayerInfos()[0].money < bet) {
        ui::TheChat().AddError(TR("У вас недостаточно денег для этой ставки.", "You don't have enough money for this bet."));
        return;
    }
    if (core::IsOnMission()) {
        ui::TheChat().AddError(TR("Нельзя начать дуэль во время миссии.", "Cannot duel during a mission."));
        return;
    }

    CPlayerPed* me = FindPlayerPed(0);
    g_opponentId = targetId;
    g_matchWeaponIdx = weaponIdx;
    g_matchBet = bet;
    g_matchArenaIdx = arenaIdx;
    g_state = State::Inviting;
    g_inviteSentMs = NowMs();

    Writer w(proto::G_DUEL);
    w.U8(OP_INVITE);
    w.U8(Net().MyId());
    w.U8(targetId);
    w.U8((uint8_t)weaponIdx);
    w.I32(bet);
    w.U8((uint8_t)arenaIdx);
    CVector curPos = me ? me->GetPosition() : CVector{};
    w.Vec({curPos.x, curPos.y, curPos.z});
    w.I8(me ? me->AreaCode() : (int8_t)0);
    w.F32(me ? me->CurrentRotation() : 0.0f);
    Net().Send(proto::CH_RELIABLE, w);

    ui::TheChat().AddSystem(Format(
        TR("Вы вызвали %s на дуэль: оружие %s, ставка $%d, арена: %s. Ожидание ответа...",
           "You challenged %s to a duel: weapon %s, bet $%d, arena: %s. Waiting for answer..."),
        Net().PeerName(targetId).c_str(), g_weapons[weaponIdx].nameRu, bet, g_arenas[arenaIdx].nameRu));
}

void AcceptInvite() {
    if (!g_incoming.active) return;
    if (PlayerInfos()[0].money < g_incoming.bet) {
        ui::TheChat().AddError(TR("У вас недостаточно денег для этой ставки!", "You don't have enough money for this bet!"));
        DeclineInvite(1);
        return;
    }
    if (core::IsOnMission()) {
        ui::TheChat().AddError(TR("Нельзя начать дуэль во время миссии.", "Cannot duel during a mission."));
        DeclineInvite(2);
        return;
    }

    uint8_t challengerId = g_incoming.challengerId;
    Writer w(proto::G_DUEL);
    w.U8(OP_ACCEPT);
    w.U8(Net().MyId());
    w.U8(challengerId);
    Net().Send(proto::CH_RELIABLE, w);

    // Opponent transitions to wait for OP_START confirmation
    g_state = State::WaitingStart;
}

void DeclineInvite(uint8_t reason) {
    if (!g_incoming.active) return;
    Writer w(proto::G_DUEL);
    w.U8(OP_DECLINE);
    w.U8(Net().MyId());
    w.U8(g_incoming.challengerId);
    w.U8(reason);
    Net().Send(proto::CH_RELIABLE, w);

    ui::TheChat().AddSystem(Format(TR("Вы отклонили вызов на дуэль от %s.", "You declined duel invite from %s."),
                                   Net().PeerName(g_incoming.challengerId).c_str()));
    g_incoming.active = false;
    if (g_state == State::Invited) g_state = State::Idle;
}

void Surrender() {
    if (!IsActive()) return;
    ui::TheChat().AddSystem(TR("Вы сдались в дуэли.", "You surrendered in the duel."));
    OnLocalDefeat();
}

void CancelInvite() {
    if (g_state != State::Inviting) return;
    Writer w(proto::G_DUEL);
    w.U8(OP_CANCEL);
    w.U8(Net().MyId());
    w.U8(g_opponentId);
    Net().Send(proto::CH_RELIABLE, w);

    ui::TheChat().AddSystem(TR("Вы отменили вызов на дуэль.", "You cancelled duel invitation."));
    g_state = State::Idle;
    g_opponentId = 255;
}

void SetMenuVisible(bool visible) {
    ui::SetDuelMenuVisible(visible);
}

bool IsMenuVisible() {
    return ui::IsDuelMenuVisible();
}

void ProcessMenu() {}

void RenderHUD() {
    if (!IsActive()) return;
    auto& g = Gfx();
    float s = g.Scale();

    // Countdown 3.. 2.. 1.. FIGHT!
    if (g_state == State::StartingCountdown) {
        uint64_t elapsedMs = NowMs() - g_countdownStartMs;
        float elapsed = (float)elapsedMs / 1000.0f;

        std::string countStr;
        uint32_t color = kSaBlue;
        if (elapsed < 1.0f) {
            countStr = "3";
            color = kSaRed;
        } else if (elapsed < 2.0f) {
            countStr = "2";
            color = kSaRed;
        } else if (elapsed < 3.0f) {
            countStr = "1";
            color = kSaBlue;
        }

        if (!countStr.empty()) {
            float th = 90.0f * s;
            g.GameText(GAME_FONT_GOTHIC, g.Width() * 0.5f, g.Height() * 0.35f, countStr.c_str(), th, color);
        }

        std::string banner = Format(TR("ДУЭЛЬ против %s  ·  Ставка: $%d", "DUEL vs %s  ·  Bet: $%d"),
                                    Net().PeerName(g_opponentId).c_str(), g_matchBet);
        g.Text(FONT_MENU, g.Width() * 0.5f, g.Height() * 0.22f, banner, kSaBlue, TEXT_CENTER | TEXT_SHADOW);
        return;
    }

    // FIGHT! splash right after countdown
    uint64_t sinceFightMs = NowMs() - g_fightStartMs;
    if (sinceFightMs < 1500) {
        g.GameText(GAME_FONT_GOTHIC, g.Width() * 0.5f, g.Height() * 0.35f, TR("В БОЙ!", "FIGHT!"), 80.0f * s, kSaGreen);
    }

    // Active Duel HUD Bar at top center
    CPlayerPed* me = FindPlayerPed(0);
    const RemotePlayer* opp = Players::Instance().Get(g_opponentId);

    float boxW = 540 * s, boxH = 54 * s;
    float boxX = (g.Width() - boxW) * 0.5f, boxY = 24 * s;

    g.Rect(boxX, boxY, boxW, boxH, 0xD0000000);
    g.RectOutline(boxX, boxY, boxW, boxH, kSaLine, std::max(1.0f, s));

    std::string title = Format(TR("ДУЭЛЬ: %s VS %s  ·  БАНК: $%d", "DUEL: %s VS %s  ·  POT: $%d"),
                               Cfg().nickname.c_str(), Net().PeerName(g_opponentId).c_str(), g_matchBet * 2);
    g.Text(FONT_SMALL, g.Width() * 0.5f, boxY + 6 * s, title, kSaBlue, TEXT_CENTER | TEXT_SHADOW);

    float dist = (me && opp && opp->ped) ? (opp->ped->GetPosition() - me->GetPosition()).Length() : -1.0f;
    float myHp = me ? me->Health() : 0.0f;
    float oppHp = (opp && opp->hasState) ? (float)opp->state.health : (opp && opp->ped ? opp->ped->Health() : 100.0f);

    std::string statsStr = Format(TR("Ваше HP: %.0f%%    ·    HP соперника: %.0f%%    ·    Дистанция: %.0f м",
                                     "Your HP: %.0f%%    ·    Opponent HP: %.0f%%    ·    Distance: %.0f m"),
                                  myHp, oppHp, std::max(0.0f, dist));
    g.Text(FONT_SMALL, g.Width() * 0.5f, boxY + 28 * s, statsStr, oppHp < 35.0f ? kSaRed : kSaGreen,
           TEXT_CENTER | TEXT_SHADOW);
}

void RenderMenu() {
    if (!g_menuVisible) return;
    auto& g = Gfx();
    float s = g.Scale();

    float lh = g.LineHeight(FONT_NORMAL), sh = g.LineHeight(FONT_SMALL), mh = g.LineHeight(FONT_MENU);
    float W = 720 * s, H = 570 * s;
    float x = (g.Width() - W) * 0.5f, y = (g.Height() - H) * 0.5f;
    float cx = x + 44 * s, cw = W - 88 * s;
    float rowH = lh + 14 * s;

    g.Rect(0, 0, g.Width(), g.Height(), 0x99000000);
    w::BeginFrame();

    float cy = w::Panel(x, y, W, H, "DUELS") + 12 * s;

    int32_t myMoney = PlayerInfos()[0].money;

    // ---- Case A: Incoming Invite ----
    if (g_incoming.active) {
        g.Text(FONT_MENU, cx + cw * 0.5f, cy, TR("ВХОДЯЩИЙ ВЫЗОВ НА ДУЭЛЬ!", "INCOMING DUEL CHALLENGE!"), kSaBlue,
               TEXT_CENTER | TEXT_SHADOW);
        cy += mh + 16 * s;

        float cardH = 170 * s;
        g.Rect(cx, cy, cw, cardH, 0xAA101620);
        g.RectOutline(cx, cy, cw, cardH, kSaLine, std::max(1.0f, s));

        float ly = cy + 14 * s;
        auto line = [&](const std::string& label, const std::string& val, uint32_t col = 0xFFFFFFFF) {
            g.Text(FONT_NORMAL, cx + 16 * s, ly, label, kSaMuted, TEXT_SHADOW);
            g.Text(FONT_NORMAL, cx + cw - 16 * s, ly, val, col, TEXT_RIGHT | TEXT_SHADOW);
            ly += lh + 8 * s;
        };

        line(TR("Вызывает:", "Challenger:"), Net().PeerName(g_incoming.challengerId), kSaBlue);
        line(TR("Оружие:", "Weapon:"),
             g_incoming.weaponIdx < (int)g_weapons.size() ? TR(g_weapons[g_incoming.weaponIdx].nameRu, g_weapons[g_incoming.weaponIdx].nameEn) : "?");
        line(TR("Ставка:", "Bet:"), Format("$%d", g_incoming.bet), kSaGreen);
        line(TR("Арена:", "Arena:"),
             g_incoming.arenaIdx < (int)g_arenas.size() ? TR(g_arenas[g_incoming.arenaIdx].nameRu, g_arenas[g_incoming.arenaIdx].nameEn) : "?");
        line(TR("Ваш баланс:", "Your balance:"), Format("$%d", myMoney), myMoney >= g_incoming.bet ? kSaGreen : kSaRed);

        cy += cardH + 24 * s;

        float btnW = (cw - 20 * s) * 0.5f;
        bool canAccept = myMoney >= g_incoming.bet && !core::IsOnMission();
        if (w::Button("accept_duel", cx, cy, btnW, rowH + 6 * s, TR("✔ Принять вызов", "✔ Accept Challenge"), canAccept, canAccept)) {
            AcceptInvite();
        }
        if (w::Button("decline_duel", cx + btnW + 20 * s, cy, btnW, rowH + 6 * s, TR("✖ Отклонить", "✖ Decline"), true)) {
            DeclineInvite(0);
        }
        cy += rowH + 24 * s;

        if (!canAccept && myMoney < g_incoming.bet) {
            g.Text(FONT_SMALL, cx + cw * 0.5f, cy, TR("У вас недостаточно средств для принятия ставки!", "You don't have enough money for this bet!"),
                   kSaRed, TEXT_CENTER | TEXT_SHADOW);
        }
    }
    // ---- Case B: Outgoing Invite Pending ----
    else if (g_state == State::Inviting) {
        g.Text(FONT_MENU, cx + cw * 0.5f, cy, TR("ОЖИДАНИЕ ОТВЕТА СОПЕРНИКА...", "WAITING FOR OPPONENT..."), kSaBlue,
               TEXT_CENTER | TEXT_SHADOW);
        cy += mh + 20 * s;

        float cardH = 140 * s;
        g.Rect(cx, cy, cw, cardH, 0xAA101620);
        g.RectOutline(cx, cy, cw, cardH, kSaLine, std::max(1.0f, s));

        float ly = cy + 14 * s;
        auto line = [&](const std::string& label, const std::string& val, uint32_t col = 0xFFFFFFFF) {
            g.Text(FONT_NORMAL, cx + 16 * s, ly, label, kSaMuted, TEXT_SHADOW);
            g.Text(FONT_NORMAL, cx + cw - 16 * s, ly, val, col, TEXT_RIGHT | TEXT_SHADOW);
            ly += lh + 8 * s;
        };

        line(TR("Соперник:", "Opponent:"), Net().PeerName(g_opponentId), kSaBlue);
        line(TR("Оружие:", "Weapon:"), TR(g_weapons[g_matchWeaponIdx].nameRu, g_weapons[g_matchWeaponIdx].nameEn));
        line(TR("Ставка:", "Bet:"), Format("$%d", g_matchBet), kSaGreen);
        line(TR("Арена:", "Arena:"), TR(g_arenas[g_matchArenaIdx].nameRu, g_arenas[g_matchArenaIdx].nameEn));

        cy += cardH + 24 * s;

        if (w::Button("cancel_duel", cx + (cw - 240 * s) * 0.5f, cy, 240 * s, rowH + 6 * s, TR("Отменить вызов", "Cancel Invitation"), true)) {
            CancelInvite();
        }
    }
    // ---- Case C: In Progress ----
    else if (IsActive()) {
        g.Text(FONT_MENU, cx + cw * 0.5f, cy, TR("ДУЭЛЬ В РАЗГАРЕ!", "DUEL IN PROGRESS!"), kSaRed, TEXT_CENTER | TEXT_SHADOW);
        cy += mh + 20 * s;

        std::string inf = Format(TR("Вы сражаетесь против %s за банк $%d.", "You are fighting against %s for pot $%d."),
                                 Net().PeerName(g_opponentId).c_str(), g_matchBet * 2);
        g.Text(FONT_NORMAL, cx + cw * 0.5f, cy, inf, 0xFFFFFFFF, TEXT_CENTER | TEXT_SHADOW);
        cy += lh + 30 * s;

        if (w::Button("surrender_duel", cx + (cw - 220 * s) * 0.5f, cy, 220 * s, rowH + 6 * s, TR("Сдаться", "Surrender"), true)) {
            Surrender();
        }
    }
    // ---- Case D: Create / Setup Duel ----
    else {
        // Collect available peers
        std::vector<uint8_t> peers;
        for (auto& [id, p] : Net().Peers()) {
            if (id != Net().MyId()) peers.push_back(id);
        }

        if (peers.empty()) {
            g.Text(FONT_NORMAL, cx + cw * 0.5f, cy + 40 * s,
                   TR("На сервере нет других игроков для дуэли.", "No other players on the server for a duel."), kSaMuted,
                   TEXT_CENTER | TEXT_SHADOW);
            cy += 120 * s;
        } else {
            if (g_selectedOpponentPeerIdx >= peers.size()) g_selectedOpponentPeerIdx = 0;
            uint8_t targetPeerId = peers[g_selectedOpponentPeerIdx];

            // 1. Opponent selector
            g.Text(FONT_SMALL, cx, cy, TR("Выберите соперника:", "Choose opponent:"), kSaMuted, TEXT_SHADOW);
            cy += sh + 4 * s;

            std::string oppText = Format("%s (%u ms)", Net().PeerName(targetPeerId).c_str(),
                                         Net().Peer(targetPeerId) ? Net().Peer(targetPeerId)->ping : 0);
            int osel = w::Selector("opp_sel", cx, cy, cw, rowH, oppText);
            if (osel < 0) {
                g_selectedOpponentPeerIdx = (g_selectedOpponentPeerIdx + peers.size() - 1) % peers.size();
            } else if (osel > 0) {
                g_selectedOpponentPeerIdx = (g_selectedOpponentPeerIdx + 1) % peers.size();
            }
            cy += rowH + 10 * s;

            // 2. Weapon selector
            g.Text(FONT_SMALL, cx, cy, TR("Выберите оружие:", "Choose weapon:"), kSaMuted, TEXT_SHADOW);
            cy += sh + 4 * s;

            std::string wepText = TR(g_weapons[g_selectedWeaponIdx].nameRu, g_weapons[g_selectedWeaponIdx].nameEn);
            int wsel = w::Selector("wep_sel", cx, cy, cw, rowH, wepText);
            if (wsel < 0) {
                g_selectedWeaponIdx = (g_selectedWeaponIdx + (int)g_weapons.size() - 1) % (int)g_weapons.size();
            } else if (wsel > 0) {
                g_selectedWeaponIdx = (g_selectedWeaponIdx + 1) % (int)g_weapons.size();
            }
            cy += rowH + 10 * s;

            // 3. Arena selector
            g.Text(FONT_SMALL, cx, cy, TR("Выберите арену:", "Choose arena:"), kSaMuted, TEXT_SHADOW);
            cy += sh + 4 * s;

            std::string arenaText = TR(g_arenas[g_selectedArenaIdx].nameRu, g_arenas[g_selectedArenaIdx].nameEn);
            int asel = w::Selector("arena_sel", cx, cy, cw, rowH, arenaText);
            if (asel < 0) {
                g_selectedArenaIdx = (g_selectedArenaIdx + (int)g_arenas.size() - 1) % (int)g_arenas.size();
            } else if (asel > 0) {
                g_selectedArenaIdx = (g_selectedArenaIdx + 1) % (int)g_arenas.size();
            }
            cy += rowH + 10 * s;

            // 4. Bet selector & quick buttons
            g.Text(FONT_SMALL, cx, cy, TR("Ставка на дуэль:", "Duel bet:"), kSaMuted, TEXT_SHADOW);
            g.Text(FONT_SMALL, cx + cw, cy, Format(TR("Ваш баланс: $%d", "Your balance: $%d"), myMoney), kSaGreen,
                   TEXT_RIGHT | TEXT_SHADOW);
            cy += sh + 4 * s;

            std::string betText = (g_selectedBet == 0) ? TR("$0 (Дружеская)", "$0 (Friendly)") : Format("$%d", g_selectedBet);
            int bsel = w::Selector("bet_sel", cx, cy, cw, rowH, betText);
            if (bsel != 0) {
                int curPreset = 0;
                int32_t bestDiff = std::abs(kPresetBets[0] - g_selectedBet);
                for (size_t i = 1; i < kPresetBetsCount; ++i) {
                    int32_t diff = std::abs(kPresetBets[i] - g_selectedBet);
                    if (diff < bestDiff) {
                        bestDiff = diff;
                        curPreset = (int)i;
                    }
                }
                curPreset = std::clamp(curPreset + bsel, 0, (int)kPresetBetsCount - 1);
                g_selectedBet = std::min(myMoney, kPresetBets[curPreset]);
            }
            cy += rowH + 6 * s;

            // Quick bet buttons
            float qbW = (cw - 25 * s) / 6.0f;
            if (w::Button("b_m500", cx, cy, qbW, rowH * 0.85f, "-$500")) {
                g_selectedBet = std::max<int32_t>(0, g_selectedBet - 500);
            }
            if (w::Button("b_p500", cx + qbW + 5 * s, cy, qbW, rowH * 0.85f, "+$500")) {
                g_selectedBet = std::min(myMoney, g_selectedBet + 500);
            }
            if (w::Button("b_m1k", cx + (qbW + 5 * s) * 2, cy, qbW, rowH * 0.85f, "-$1000")) {
                g_selectedBet = std::max<int32_t>(0, g_selectedBet - 1000);
            }
            if (w::Button("b_p1k", cx + (qbW + 5 * s) * 3, cy, qbW, rowH * 0.85f, "+$1000")) {
                g_selectedBet = std::min(myMoney, g_selectedBet + 1000);
            }
            if (w::Button("b_min", cx + (qbW + 5 * s) * 4, cy, qbW, rowH * 0.85f, TR("Мин", "Min"))) {
                g_selectedBet = 0;
            }
            if (w::Button("b_max", cx + (qbW + 5 * s) * 5, cy, qbW, rowH * 0.85f, TR("Макс", "Max"))) {
                g_selectedBet = std::min<int32_t>(myMoney, 1000000);
            }
            cy += rowH * 0.85f + 18 * s;

            // Challenge Button
            bool canChallenge = !peers.empty() && myMoney >= g_selectedBet && !core::IsOnMission();
            float chalW = 280 * s;
            if (w::Button("send_chal", cx + (cw - chalW) * 0.5f, cy, chalW, rowH + 6 * s,
                          TR("⚔ Вызвать на дуэль ⚔", "⚔ Challenge to Duel ⚔"), canChallenge, canChallenge)) {
                SendInvite(peers[g_selectedOpponentPeerIdx], g_selectedWeaponIdx, g_selectedBet, g_selectedArenaIdx);
            }
            cy += rowH + 14 * s;
        }
    }

    // Divider & Footer
    float divY = y + H - 64 * s;
    g.Rect(cx, divY, cw, std::max(1.0f, s), kSaLine);

    std::string closeText = TR("Закрыть", "Close");
    float closeW = g.TextWidth(FONT_MENU, closeText) + 30 * s;
    if (w::Button("close_duel", cx + cw - closeW, divY + 10 * s, closeW, rowH, closeText)) {
        ui::SetDuelMenuVisible(false);
    }

    float hintY = y + H - 16 * s - sh;
    g.Text(FONT_SMALL, cx, hintY,
           TR("{ACCBF1}ESC{8D9CAD} закрыть      {ACCBF1}Клик{8D9CAD} выбор параметров дуэли",
              "{ACCBF1}ESC{8D9CAD} close      {ACCBF1}Click{8D9CAD} choose duel parameters"),
           kSaMuted, TEXT_SHADOW);

    w::EndFrame();
    w::DrawCursor();
}

void HandleNetwork(uint8_t from, Reader& r) {
    uint8_t op = r.U8();
    switch (op) {
        case OP_INVITE: {
            uint8_t challengerId = r.U8();
            uint8_t targetId = r.U8();
            uint8_t weaponIdx = r.U8();
            int32_t bet = r.I32();
            uint8_t arenaIdx = r.U8();
            Vec3 v = r.Vec();
            CVector pos{v.x, v.y, v.z};
            int8_t area = r.I8();
            float heading = r.F32();

            if (targetId != Net().MyId() || !r.Ok()) return;
            if (IsActive() || g_state == State::Inviting) {
                // Busy in duel or inviting someone else
                Writer w(proto::G_DUEL);
                w.U8(OP_DECLINE);
                w.U8(Net().MyId());
                w.U8(challengerId);
                w.U8(2);  // busy
                Net().Send(proto::CH_RELIABLE, w);
                return;
            }

            g_incoming.active = true;
            g_incoming.challengerId = challengerId;
            g_incoming.weaponIdx = (int)weaponIdx;
            g_incoming.bet = bet;
            g_incoming.arenaIdx = (int)arenaIdx;
            g_incoming.pos = pos;
            g_incoming.area = area;
            g_incoming.heading = heading;
            g_incoming.recvMs = NowMs();
            g_state = State::Invited;

            std::string wName = (weaponIdx < g_weapons.size()) ? g_weapons[weaponIdx].nameRu : "?";
            std::string aName = (arenaIdx < g_arenas.size()) ? g_arenas[arenaIdx].nameRu : "?";

            ui::TheChat().AddSystem(
                Format(TR("{ACCBF1}%s{FFFFFF} вызывает вас на дуэль: оружие {ACCBF1}%s{FFFFFF}, ставка {5FA04E}$%d{FFFFFF}, арена {ACCBF1}%s{FFFFFF}.\nВведите {ACCBF1}/duel{FFFFFF} или {ACCBF1}/duel accept{FFFFFF}.",
                          "{ACCBF1}%s{FFFFFF} challenged you to a duel: weapon {ACCBF1}%s{FFFFFF}, bet {5FA04E}$%d{FFFFFF}, arena {ACCBF1}%s{FFFFFF}.\nType {ACCBF1}/duel{FFFFFF} or {ACCBF1}/duel accept{FFFFFF}."),
                       Net().PeerName(challengerId).c_str(), wName.c_str(), bet, aName.c_str()));
            break;
        }

        case OP_ACCEPT: {
            uint8_t opponentId = r.U8();
            uint8_t challengerId = r.U8();
            if (challengerId != Net().MyId() || !r.Ok()) return;
            if (g_state != State::Inviting || g_opponentId != opponentId) return;

            // Send start packet
            Writer w(proto::G_DUEL);
            w.U8(OP_START);
            w.U8(Net().MyId());
            w.U8(opponentId);
            Net().Send(proto::CH_RELIABLE, w);

            // Challenger starts match
            StartDuelMatch(opponentId, g_matchWeaponIdx, g_matchBet, g_matchArenaIdx, true);
            break;
        }

        case OP_START: {
            uint8_t challengerId = r.U8();
            uint8_t opponentId = r.U8();
            if (opponentId != Net().MyId() || !r.Ok()) return;
            if (!g_incoming.active || g_incoming.challengerId != challengerId) return;

            // Opponent starts match on start signal
            StartDuelMatch(challengerId, g_incoming.weaponIdx, g_incoming.bet, g_incoming.arenaIdx, false);
            break;
        }

        case OP_DECLINE: {
            uint8_t opponentId = r.U8();
            uint8_t challengerId = r.U8();
            uint8_t reason = r.U8();
            if (challengerId != Net().MyId() || !r.Ok()) return;
            if (g_state != State::Inviting || g_opponentId != opponentId) return;

            std::string reasonStr = (reason == 1) ? TR("недостаточно денег для ставки", "not enough money for the bet")
                                  : (reason == 2) ? TR("игрок занят или на миссии", "player is busy or on mission")
                                                  : TR("игрок отклонил предложение", "player declined invitation");

            ui::TheChat().AddSystem(Format(TR("%s отклонил дуэль (%s).", "%s declined duel (%s)."),
                                           Net().PeerName(opponentId).c_str(), reasonStr.c_str()));

            g_state = State::Idle;
            g_opponentId = 255;
            break;
        }

        case OP_FINISH: {
            uint8_t winnerId = r.U8();
            uint8_t loserId = r.U8();
            (void)r.I32();  // pot
            if (!r.Ok()) return;
            if (IsActive() && (winnerId == Net().MyId() || loserId == Net().MyId())) {
                HandleFinish(winnerId, loserId);
            }
            break;
        }

        case OP_CANCEL: {
            uint8_t challengerId = r.U8();
            uint8_t targetId = r.U8();
            if (targetId != Net().MyId() || !r.Ok()) return;
            if (IsActive() && g_opponentId == challengerId) {
                AbortMatch(Format(TR("Дуэль отменена соперником %s.", "Duel was cancelled by %s."),
                                  Net().PeerName(challengerId).c_str()));
                break;
            }
            if (g_incoming.active && g_incoming.challengerId == challengerId) {
                ui::TheChat().AddSystem(Format(TR("%s отменил вызов на дуэль.", "%s cancelled the duel challenge."),
                                               Net().PeerName(challengerId).c_str()));
                g_incoming.active = false;
                if (g_state == State::Invited || g_state == State::StartingCountdown || g_state == State::WaitingStart) g_state = State::Idle;
            }
            break;
        }
    }
}

class DuelModule : public Module {
public:
    const char* Name() const override { return "Duel"; }

    void OnInstallHooks() override {
        Net().SetHandler(proto::G_DUEL, &HandleNetwork);
    }

    void OnSessionEnd() override {
        if (IsActive()) {
            AbortMatch({});
        }
        g_state = State::Idle;
        g_opponentId = 255;
        g_incoming.active = false;
        ui::SetDuelMenuVisible(false);
    }

    void OnPeerLeave(uint8_t id) override {
        if (g_incoming.active && g_incoming.challengerId == id) {
            g_incoming.active = false;
            if (g_state == State::Invited) g_state = State::Idle;
        }
        if (g_state == State::Inviting && g_opponentId == id) {
            ui::TheChat().AddSystem(TR("Соперник покинул игру. Вызов отменен.", "Opponent left the game. Invite cancelled."));
            g_state = State::Idle;
            g_opponentId = 255;
        }
        if (IsActive() && g_opponentId == id) {
            AbortMatch(TR("Соперник вышел из игры во время дуэли! Ставка возвращена.",
                          "Opponent disconnected during duel! Bet refunded."));
        }
    }

    void OnGameShutdown() override {
        if (IsActive()) {
            AbortMatch({});
        }
        g_state = State::Idle;
        ui::SetDuelMenuVisible(false);
    }

    void OnPostProcess() override {
        // Invite expiration checks (30s timeout)
        if (g_state == State::Inviting && NowMs() - g_inviteSentMs > 30000) {
            ui::TheChat().AddSystem(TR("Время ожидания ответа на вызов истекло.", "Duel invite timed out."));
            CancelInvite();
        }
        if (g_incoming.active && NowMs() - g_incoming.recvMs > 30000) {
            g_incoming.active = false;
            if (g_state == State::Invited) g_state = State::Idle;
        }

        CPlayerPed* me = FindPlayerPed(0);
        if (!me) return;

        // Keep police away during duel
        if (IsActive()) {
            ClearStars();
        }

        // Countdown timer tick
        if (g_state == State::StartingCountdown) {
            if (NowMs() - g_countdownStartMs >= 3000) {
                g_state = State::InProgress;
                g_fightStartMs = NowMs();
                CPad* pad = game::GetPad(0);
                if (pad) pad->DisablePlayerControls &= (uint16_t)~kDuelControlsBit;
                me->PhysicalFlags() = (me->PhysicalFlags() & ~kProofMask) | g_savedProofs;
                int h = GetPedRef(me);
                if (h >= 0) Cmd(0x0350).I(h).I(0).Run();  // SET_CHAR_STAY_IN_SAME_PLACE: OFF (Unfreeze)
                LOGI("Duel countdown finished, combat active!");
            }
            return;
        }

        // Active fight tick
        if (g_state == State::InProgress) {
            bool lost = false;

            // Health ran out (< 5 HP to safely finish before death engine)
            if (me->Health() <= 5.0f) {
                lost = true;
            }

            // Universal fall check for ANY arena
            float expectedZ = (g_matchArenaIdx >= 0 && g_matchArenaIdx < (int)g_arenas.size() && g_matchArenaIdx != 4)
                                  ? g_arenas[g_matchArenaIdx].spawnChallenger.z
                                  : g_savedPos.z;
            if (me->GetPosition().z < expectedZ - 20.0f || me->GetPosition().z < -10.0f) {
                lost = true;
            }

            // Submerged / vehicle check
            if (me->InVehicle() || (me->PhysicalFlags() & kSubmerged)) {
                lost = true;
            }

            // Out-of-bounds horizontal check for arenas (200m radius)
            if (g_matchArenaIdx != 4) {
                const auto& arena = g_arenas[g_matchArenaIdx];
                if ((me->GetPosition() - arena.spawnChallenger).Length2D() > 200.0f) {
                    lost = true;
                }
            }

            if (lost) {
                OnLocalDefeat();
            }
        }
    }

    void OnRender() override {
        RenderHUD();
    }

    bool OnCommand(const std::string& cmd, const std::vector<std::string>& args) override {
        if (cmd == "duel" || cmd == "duels" || cmd == "дуэль" || cmd == "дуэли") {
            if (!args.empty()) {
                std::string sub = ToLower(args[0]);
                if (sub == "stop" || sub == "cancel" || sub == "abort" || sub == "end" || sub == "стоп" || sub == "отмена") {
                    if (IsActive()) {
                        uint8_t opp = g_opponentId;
                        AbortMatch(TR("Дуэль была отменена.", "Duel was aborted."));
                        Writer w(proto::G_DUEL);
                        w.U8(OP_CANCEL);
                        w.U8(Net().MyId());
                        w.U8(opp);
                        Net().Send(proto::CH_RELIABLE, w);
                        return true;
                    }
                    if (g_state == State::Inviting) {
                        CancelInvite();
                        return true;
                    }
                    if (g_state == State::Invited) {
                        DeclineInvite(0);
                        return true;
                    }
                    ui::TheChat().AddSystem(TR("Вы не в дуэли.", "You are not in a duel."));
                    return true;
                }
                if (sub == "accept" || sub == "yes" || sub == "принять" || sub == "да") {
                    if (g_incoming.active) {
                        AcceptInvite();
                    } else {
                        ui::TheChat().AddError(TR("У вас нет входящих вызовов на дуэль.", "You have no incoming duel invites."));
                    }
                    return true;
                }
                if (sub == "decline" || sub == "no" || sub == "отклонить" || sub == "нет") {
                    if (g_incoming.active) {
                        DeclineInvite(0);
                    } else {
                        ui::TheChat().AddError(TR("У вас нет входящих вызовов на дуэль.", "You have no incoming duel invites."));
                    }
                    return true;
                }
                if (sub == "cancel") {
                    if (g_state == State::Inviting) {
                        CancelInvite();
                    }
                    return true;
                }
                if (sub == "surrender" || sub == "leave") {
                    if (IsActive()) {
                        Surrender();
                    }
                    return true;
                }
            }
            ui::SetDuelMenuVisible(!ui::IsDuelMenuVisible());
            return true;
        }
        return false;
    }
};

static DuelModule g_duelModule;
struct RegDuel {
    RegDuel() { core::RegisterModule(&g_duelModule); }
} g_regDuel;

}  // namespace coop::duel

namespace coop::ui {

void SetDuelMenuVisible(bool visible) {
    if (visible == duel::g_menuVisible) return;
    duel::g_menuVisible = visible;
    if (visible) {
        if (IsConnectMenuVisible()) SetConnectMenuVisible(false);
        if (IsAnimMenuVisible()) SetAnimMenuVisible(false);
        if (TheChat().IsOpen()) TheChat().Close();
        In().SetKeyboardCapture(true);
        In().SetMouseCapture(true);
    } else {
        if (!IsConnectMenuVisible() && !IsAnimMenuVisible()) {
            In().SetKeyboardCapture(false);
            In().SetMouseCapture(false);
        }
    }
}

bool IsDuelMenuVisible() { return duel::g_menuVisible; }

void ProcessDuelMenu() {}

void RenderDuelMenu() {
    duel::RenderMenu();
}

}  // namespace coop::ui
