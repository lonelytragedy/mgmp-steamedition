#pragma once

#include <cstdint>

namespace mgmp {

bool        steam_ready();
void        steam_detach();
uint64_t    steam_self();
const char* steam_persona_of(uint64_t steam_id);

struct SteamInMsg {
    const uint8_t* data = nullptr;
    uint32_t       size = 0;
    uint64_t       from = 0;
    void*          handle = nullptr;
};

bool steam_send(uint64_t to, const void* data, uint32_t len);
int  steam_send_result(uint64_t to, const void* data, uint32_t len);
constexpr int kSteamResultOK       = 1;
constexpr int kSteamLimitExceeded  = 25;
int  steam_recv(SteamInMsg* out, int max);
void steam_release(SteamInMsg& m);
void steam_close_session(uint64_t with);

constexpr uint32_t kSteamMaxMessage = 512u * 1024u;

enum class LobbyState { None, Creating, Hosting, Joining, Joined, Failed };

void steam_pump();

bool steam_host_lobby();
bool steam_join_lobby(uint64_t lobby);
void steam_leave_lobby();
void steam_open_invite_dialog();

LobbyState  steam_lobby_state();
const char* steam_lobby_status();
uint64_t    steam_lobby_id();
int         steam_lobby_members(uint64_t* out, int cap);

struct FriendLobby {
    uint64_t friend_id = 0;
    uint64_t lobby     = 0;
    char     name[64]  = {};
};
int steam_friend_lobbies(FriendLobby* out, int cap);

uint64_t steam_take_join_request();

}
