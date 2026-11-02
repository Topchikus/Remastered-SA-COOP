#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "shared/game/Game.h"
#include "shared/lib/core/Core.h"

namespace coop::duel {

enum class State {
    Idle,
    Inviting,           // We sent an invite, waiting for answer
    Invited,            // We received an invite, waiting for our decision
    WaitingStart,       // We accepted invite, waiting for OP_START from challenger
    StartingCountdown,  // Duel agreed, teleported, 3..2..1 countdown
    InProgress,         // Active fight
    Finished            // Round over
};

struct ArenaDef {
    const char* nameRu;
    const char* nameEn;
    int area;
    game::CVector spawnChallenger;
    float headingChallenger;
    game::CVector spawnOpponent;
    float headingOpponent;
};

struct WeaponDef {
    const char* nameRu;
    const char* nameEn;
    int type;
    int ammo;
};

const std::vector<ArenaDef>& GetArenas();
const std::vector<WeaponDef>& GetWeapons();

State GetState();
bool IsActive();                     // In countdown or active duel
bool IsDuelingWith(uint8_t peerId);  // Is active duel with specific peer

void SendInvite(uint8_t targetId, int weaponIdx, int32_t bet, int arenaIdx);
void AcceptInvite();
void DeclineInvite(uint8_t reason = 0);
void Surrender();
void CancelInvite();
void OnLocalDefeat();

void SetMenuVisible(bool visible);
bool IsMenuVisible();
void ProcessMenu();
void RenderMenu();
void RenderHUD();

}  // namespace coop::duel

namespace coop::ui {

void SetDuelMenuVisible(bool visible);
bool IsDuelMenuVisible();
void ProcessDuelMenu();
void RenderDuelMenu();

}  // namespace coop::ui
