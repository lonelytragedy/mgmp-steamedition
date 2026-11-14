
#include "mgmp_steam.h"
#include "mgmp_net.h"
#include "mgmp_log.h"

#include <windows.h>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace mgmp {
namespace {

typedef uint64_t SteamAPICall_t;

#pragma pack(push, 8)
struct SteamNetworkingIdentity {
    int32_t  type;
    int32_t  size;
    union {
        uint64_t steam_id64;
        char     raw[128];
    };
};
static_assert(sizeof(SteamNetworkingIdentity) == 136, "SteamNetworkingIdentity layout");

struct SteamNetworkingMessage {
    void*                   data;
    int32_t                 size;
    uint32_t                conn;
    SteamNetworkingIdentity peer;
    int64_t                 conn_user_data;
    int64_t                 usec_received;
    int64_t                 message_number;
    void (*free_data)(SteamNetworkingMessage*);
    void (*release)(SteamNetworkingMessage*);
    int32_t                 channel;
    int32_t                 flags;
    int64_t                 user_data;
    uint16_t                lane;
    uint16_t                pad;
};
static_assert(offsetof(SteamNetworkingMessage, release) == 184, "SteamNetworkingMessage layout");

struct LobbyCreated_t { int32_t result; uint64_t lobby; };
struct LobbyEnter_t   { uint64_t lobby; uint32_t perms; bool locked; uint32_t response; };
struct GameLobbyJoinRequested_t { uint64_t lobby; uint64_t friend_id; };
struct GameRichPresenceJoinRequested_t { uint64_t friend_id; char connect[256]; };
struct SessionRequest_t { SteamNetworkingIdentity remote; };
struct FriendGameInfo_t { uint64_t game_id; uint32_t ip; uint16_t port, query_port; uint64_t lobby; };
#pragma pack(pop)

static_assert(sizeof(LobbyEnter_t) == 24, "LobbyEnter_t layout");
static_assert(sizeof(FriendGameInfo_t) == 24, "FriendGameInfo_t layout");

constexpr int kCbLobbyEnter        = 504;
constexpr int kCbLobbyCreated      = 513;
constexpr int kCbLobbyJoinRequest  = 333;
constexpr int kCbRichJoinRequest   = 337;
constexpr int kCbSessionRequest    = 1251;

constexpr int32_t kIdentitySteamID = 16;
constexpr int     kSendReliable    = 8;
constexpr int     kSendAutoRestart = 32;
constexpr int32_t kResultOK        = 1;
constexpr int     kLobbyFriendsOnly = 1;
constexpr uint32_t kEnterSuccess   = 1;
constexpr int     kFriendFlagImmediate = 4;

constexpr int kChannel = 0x6D67;

class CCallbackBase {
public:
    CCallbackBase() { m_nCallbackFlags = 0; m_iCallback = 0; }
    virtual void Run(void* pvParam) = 0;
    virtual void Run(void* pvParam, bool bIOFailure, SteamAPICall_t hSteamAPICall) = 0;
    int GetICallback() { return m_iCallback; }
    virtual int GetCallbackSizeBytes() = 0;
protected:
    uint8_t m_nCallbackFlags;
    int     m_iCallback;
};

typedef void (*CbFn)(void* param);

class Callback : public CCallbackBase {
public:
    Callback(int id, int size, CbFn fn) : fn_(fn), size_(size) { m_iCallback = id; }
    void Run(void* p) override { if (fn_) fn_(p); }
    void Run(void* p, bool, SteamAPICall_t) override { if (fn_) fn_(p); }
    int GetCallbackSizeBytes() override { return size_; }
private:
    CbFn fn_;
    int  size_;
};

struct Api {
    bool resolved = false;

    void (*RegisterCallback)(CCallbackBase*, int) = nullptr;

    void* (*Matchmaking)() = nullptr;
    void* (*Friends)() = nullptr;
    void* (*Utils)() = nullptr;
    void* (*User)() = nullptr;
    void* (*Messages)() = nullptr;

    uint64_t (*User_GetSteamID)(void*) = nullptr;

    SteamAPICall_t (*MM_CreateLobby)(void*, int, int) = nullptr;
    SteamAPICall_t (*MM_JoinLobby)(void*, uint64_t) = nullptr;
    void     (*MM_LeaveLobby)(void*, uint64_t) = nullptr;
    int      (*MM_GetNumLobbyMembers)(void*, uint64_t) = nullptr;
    uint64_t (*MM_GetLobbyMemberByIndex)(void*, uint64_t, int) = nullptr;
    uint64_t (*MM_GetLobbyOwner)(void*, uint64_t) = nullptr;
    bool     (*MM_SetLobbyData)(void*, uint64_t, const char*, const char*) = nullptr;

    void        (*FR_InviteDialog)(void*, uint64_t) = nullptr;
    bool        (*FR_SetRichPresence)(void*, const char*, const char*) = nullptr;
    void        (*FR_ClearRichPresence)(void*) = nullptr;
    const char* (*FR_GetFriendPersonaName)(void*, uint64_t) = nullptr;
    const char* (*FR_GetPersonaName)(void*) = nullptr;
    int         (*FR_GetFriendCount)(void*, int) = nullptr;
    uint64_t    (*FR_GetFriendByIndex)(void*, int, int) = nullptr;
    bool        (*FR_GetFriendGamePlayed)(void*, uint64_t, FriendGameInfo_t*) = nullptr;

    bool     (*UT_IsAPICallCompleted)(void*, SteamAPICall_t, bool*) = nullptr;
    bool     (*UT_GetAPICallResult)(void*, SteamAPICall_t, void*, int, int, bool*) = nullptr;
    uint32_t (*UT_GetAppID)(void*) = nullptr;

    int  (*NM_Send)(void*, const SteamNetworkingIdentity*, const void*, uint32_t, int, int) = nullptr;
    int  (*NM_Receive)(void*, int, SteamNetworkingMessage**, int) = nullptr;
    bool (*NM_Accept)(void*, const SteamNetworkingIdentity*) = nullptr;
    bool (*NM_Close)(void*, const SteamNetworkingIdentity*) = nullptr;

    void* mm = nullptr; void* fr = nullptr; void* ut = nullptr;
    void* us = nullptr; void* nm = nullptr;
};

Api api;
DWORD g_last_try = 0;

bool g_detached = false;

template <class T> bool get(HMODULE m, const char* name, T& out) {
    out = (T)GetProcAddress(m, name);
    if (!out) log_line("STEAM", "!! steam_api64.dll has no export %s", name);
    return out != nullptr;
}

bool resolve() {
    if (g_detached) return false;
    if (api.resolved) return true;
    DWORD now = GetTickCount();
    if (g_last_try && now - g_last_try < 1000) return false;
    g_last_try = now;

    HMODULE m = GetModuleHandleW(L"steam_api64.dll");
    if (!m) return false;

    bool ok = true;
    ok &= get(m, "SteamAPI_RegisterCallback", api.RegisterCallback);
    ok &= get(m, "SteamAPI_SteamMatchmaking_v009", api.Matchmaking);
    ok &= get(m, "SteamAPI_SteamFriends_v018", api.Friends);
    ok &= get(m, "SteamAPI_SteamUtils_v010", api.Utils);
    ok &= get(m, "SteamAPI_SteamUser_v023", api.User);
    ok &= get(m, "SteamAPI_SteamNetworkingMessages_SteamAPI_v002", api.Messages);
    ok &= get(m, "SteamAPI_ISteamUser_GetSteamID", api.User_GetSteamID);
    ok &= get(m, "SteamAPI_ISteamMatchmaking_CreateLobby", api.MM_CreateLobby);
    ok &= get(m, "SteamAPI_ISteamMatchmaking_JoinLobby", api.MM_JoinLobby);
    ok &= get(m, "SteamAPI_ISteamMatchmaking_LeaveLobby", api.MM_LeaveLobby);
    ok &= get(m, "SteamAPI_ISteamMatchmaking_GetNumLobbyMembers", api.MM_GetNumLobbyMembers);
    ok &= get(m, "SteamAPI_ISteamMatchmaking_GetLobbyMemberByIndex", api.MM_GetLobbyMemberByIndex);
    ok &= get(m, "SteamAPI_ISteamMatchmaking_GetLobbyOwner", api.MM_GetLobbyOwner);
    ok &= get(m, "SteamAPI_ISteamMatchmaking_SetLobbyData", api.MM_SetLobbyData);
    ok &= get(m, "SteamAPI_ISteamFriends_ActivateGameOverlayInviteDialog", api.FR_InviteDialog);
    ok &= get(m, "SteamAPI_ISteamFriends_SetRichPresence", api.FR_SetRichPresence);
    ok &= get(m, "SteamAPI_ISteamFriends_ClearRichPresence", api.FR_ClearRichPresence);
    ok &= get(m, "SteamAPI_ISteamFriends_GetFriendPersonaName", api.FR_GetFriendPersonaName);
    ok &= get(m, "SteamAPI_ISteamFriends_GetPersonaName", api.FR_GetPersonaName);
    ok &= get(m, "SteamAPI_ISteamFriends_GetFriendCount", api.FR_GetFriendCount);
    ok &= get(m, "SteamAPI_ISteamFriends_GetFriendByIndex", api.FR_GetFriendByIndex);
    ok &= get(m, "SteamAPI_ISteamFriends_GetFriendGamePlayed", api.FR_GetFriendGamePlayed);
    ok &= get(m, "SteamAPI_ISteamUtils_IsAPICallCompleted", api.UT_IsAPICallCompleted);
    ok &= get(m, "SteamAPI_ISteamUtils_GetAPICallResult", api.UT_GetAPICallResult);
    ok &= get(m, "SteamAPI_ISteamUtils_GetAppID", api.UT_GetAppID);
    ok &= get(m, "SteamAPI_ISteamNetworkingMessages_SendMessageToUser", api.NM_Send);
    ok &= get(m, "SteamAPI_ISteamNetworkingMessages_ReceiveMessagesOnChannel", api.NM_Receive);
    ok &= get(m, "SteamAPI_ISteamNetworkingMessages_AcceptSessionWithUser", api.NM_Accept);
    ok &= get(m, "SteamAPI_ISteamNetworkingMessages_CloseSessionWithUser", api.NM_Close);
    if (!ok) { g_last_try = 0xFFFFFFFF; return false; }

    api.mm = api.Matchmaking();
    api.fr = api.Friends();
    api.ut = api.Utils();
    api.us = api.User();
    api.nm = api.Messages();
    if (!api.mm || !api.fr || !api.ut || !api.us || !api.nm) return false;
    if (!api.User_GetSteamID(api.us)) return false;

    api.resolved = true;
    log_line("STEAM", "steam api bound: user %llu (%s), app %u",
             (unsigned long long)api.User_GetSteamID(api.us),
             api.FR_GetPersonaName(api.fr), api.UT_GetAppID(api.ut));
    return true;
}

SteamNetworkingIdentity identity_of(uint64_t id) {
    SteamNetworkingIdentity i{};
    i.type = kIdentitySteamID;
    i.size = 8;
    i.steam_id64 = id;
    return i;
}

struct Lobby {
    LobbyState     state = LobbyState::None;
    uint64_t       id    = 0;
    uint64_t       owner = 0;
    SteamAPICall_t call  = 0;
    char           status[160] = "not in a lobby";

    uint64_t       known[8] = {};
    DWORD          known_seen[8] = {};
    int            known_count = 0;

    DWORD          owner_seen = 0;
};

constexpr DWORD kGoneMs = 8000;

Lobby L;

uint64_t g_join_request = 0;
bool     g_callbacks = false;
bool     g_cmdline_checked = false;

void set_status(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf_s(L.status, sizeof(L.status), _TRUNCATE, fmt, ap);
    va_end(ap);
    log_line("STEAM", "%s", L.status);
}

bool is_member(uint64_t who) {
    if (!L.id) return false;
    int n = api.MM_GetNumLobbyMembers(api.mm, L.id);
    for (int i = 0; i < n; ++i)
        if (api.MM_GetLobbyMemberByIndex(api.mm, L.id, i) == who) return true;
    return false;
}

int trusted_members(uint64_t* out, int cap) {
    if (!L.id) return -1;
    int n = api.MM_GetNumLobbyMembers(api.mm, L.id);
    if (n <= 0) return -1;
    const uint64_t me = api.User_GetSteamID(api.us);
    bool self = false;
    int k = 0;
    for (int i = 0; i < n; ++i) {
        uint64_t m = api.MM_GetLobbyMemberByIndex(api.mm, L.id, i);
        if (m == me) self = true;
        if (k < cap) out[k++] = m;
    }
    return self ? k : -1;
}

void on_lobby_join_requested(void* p) {
    auto* e = (GameLobbyJoinRequested_t*)p;
    log_line("STEAM", "invite accepted: lobby %llu from %s",
             (unsigned long long)e->lobby, steam_persona_of(e->friend_id));
    g_join_request = e->lobby;
}

uint64_t parse_connect(const char* s) {
    if (!s) return 0;
    const char* k = strstr(s, "+connect_lobby");
    if (!k) return 0;
    k += strlen("+connect_lobby");
    while (*k == ' ' || *k == '\t' || *k == '"') ++k;
    return _strtoui64(k, nullptr, 10);
}

void on_rich_join_requested(void* p) {
    auto* e = (GameRichPresenceJoinRequested_t*)p;
    uint64_t lobby = parse_connect(e->connect);
    log_line("STEAM", "join via friends list: '%s' from %s",
             e->connect, steam_persona_of(e->friend_id));
    if (lobby) g_join_request = lobby;
}

void on_session_request(void* p) {
    auto* e = (SessionRequest_t*)p;
    if (e->remote.type != kIdentitySteamID) return;
    const uint64_t who = e->remote.steam_id64;
    bool known = (who == L.owner);
    for (int i = 0; i < L.known_count; ++i) if (L.known[i] == who) known = true;
    if (known || is_member(who)) {
        api.NM_Accept(api.nm, &e->remote);
        log_line("STEAM", "accepted a P2P session from %s", steam_persona_of(who));
    } else {
        log_line("STEAM", "ignored a P2P session request from %llu -- not in our lobby",
                 (unsigned long long)who);
    }
}

Callback cb_lobby_join(kCbLobbyJoinRequest, sizeof(GameLobbyJoinRequested_t), on_lobby_join_requested);
Callback cb_rich_join(kCbRichJoinRequest, sizeof(GameRichPresenceJoinRequested_t), on_rich_join_requested);
Callback cb_session(kCbSessionRequest, sizeof(SessionRequest_t), on_session_request);

void register_callbacks() {
    if (g_callbacks) return;
    api.RegisterCallback(&cb_lobby_join, kCbLobbyJoinRequest);
    api.RegisterCallback(&cb_rich_join, kCbRichJoinRequest);
    api.RegisterCallback(&cb_session, kCbSessionRequest);
    g_callbacks = true;
}

template <class T> bool call_done(SteamAPICall_t call, int id, T& out, bool& failed) {
    bool io_fail = false;
    if (!api.UT_IsAPICallCompleted(api.ut, call, &io_fail)) return false;
    failed = io_fail || !api.UT_GetAPICallResult(api.ut, call, &out, (int)sizeof(T), id, &io_fail) || io_fail;
    return true;
}

void set_presence() {
    char connect[64];
    _snprintf_s(connect, sizeof(connect), _TRUNCATE, "+connect_lobby %llu",
                (unsigned long long)L.id);
    api.FR_SetRichPresence(api.fr, "connect", connect);
    api.FR_SetRichPresence(api.fr, "status", "Co-op (mgmp)");
}

void host_sync_members() {
    uint64_t now[8] = {};
    const int n = trusted_members(now, 8);
    if (n < 0) return;
    const uint64_t me = steam_self();
    const DWORD t = GetTickCount();

    for (int i = 0; i < n; ++i) {
        if (now[i] == me) continue;
        int had = -1;
        for (int j = 0; j < L.known_count; ++j) if (L.known[j] == now[i]) had = j;
        if (had >= 0) { L.known_seen[had] = t; continue; }
        log_line("STEAM", "%s joined the lobby", steam_persona_of(now[i]));
        net_steam_peer_joined(now[i]);
        if (L.known_count < 8) {
            L.known_seen[L.known_count] = t;
            L.known[L.known_count++]    = now[i];
        }
    }
    for (int j = 0; j < L.known_count; ) {
        bool still = false;
        for (int i = 0; i < n; ++i) if (now[i] == L.known[j]) still = true;
        if (still || t - L.known_seen[j] < kGoneMs) { ++j; continue; }
        log_line("STEAM", "%s left the lobby (missing for %lus)",
                 steam_persona_of(L.known[j]), (t - L.known_seen[j]) / 1000);
        net_steam_peer_left(L.known[j]);
        L.known_seen[j] = L.known_seen[L.known_count - 1];
        L.known[j]      = L.known[--L.known_count];
    }
}

void client_watch_host() {
    uint64_t now[8] = {};
    const int n = trusted_members(now, 8);
    const DWORD t = GetTickCount();
    if (!L.owner_seen) L.owner_seen = t;
    if (n < 0) return;
    for (int i = 0; i < n; ++i) if (now[i] == L.owner) { L.owner_seen = t; return; }
    if (t - L.owner_seen >= kGoneMs) {
        set_status("the host left the lobby");
        net_steam_host_lost();
        api.MM_LeaveLobby(api.mm, L.id);
        L = Lobby{};
        set_status("the host left -- session closed");
    }
}

}

bool steam_ready() { return resolve(); }

void steam_detach() {
    g_detached   = true;
    api.resolved = false;
}

uint64_t steam_self() {
    return resolve() ? api.User_GetSteamID(api.us) : 0;
}

const char* steam_persona_of(uint64_t id) {
    if (!resolve() || !id) return "";
    if (id == api.User_GetSteamID(api.us)) return api.FR_GetPersonaName(api.fr);
    const char* n = api.FR_GetFriendPersonaName(api.fr, id);
    return n ? n : "";
}

int steam_send_result(uint64_t to, const void* data, uint32_t len) {
    if (!api.resolved || !len || len > kSteamMaxMessage) return 0;
    SteamNetworkingIdentity id = identity_of(to);
    return api.NM_Send(api.nm, &id, data, len, kSendReliable | kSendAutoRestart, kChannel);
}

bool steam_send(uint64_t to, const void* data, uint32_t len) {
    return steam_send_result(to, data, len) == kResultOK;
}

int steam_recv(SteamInMsg* out, int max) {
    if (!api.resolved || max <= 0) return 0;
    SteamNetworkingMessage* msgs[32];
    if (max > 32) max = 32;
    int n = api.NM_Receive(api.nm, kChannel, msgs, max);
    for (int i = 0; i < n; ++i) {
        out[i].data   = (const uint8_t*)msgs[i]->data;
        out[i].size   = (uint32_t)msgs[i]->size;
        out[i].from   = msgs[i]->peer.type == kIdentitySteamID ? msgs[i]->peer.steam_id64 : 0;
        out[i].handle = msgs[i];
    }
    return n < 0 ? 0 : n;
}

void steam_release(SteamInMsg& m) {
    if (!m.handle) return;
    auto* msg = (SteamNetworkingMessage*)m.handle;
    msg->release(msg);
    m.handle = nullptr;
    m.data   = nullptr;
}

void steam_close_session(uint64_t with) {
    if (!api.resolved || !with) return;
    SteamNetworkingIdentity id = identity_of(with);
    api.NM_Close(api.nm, &id);
}

void steam_pump() {
    if (!resolve()) return;
    register_callbacks();

    if (!g_cmdline_checked) {
        g_cmdline_checked = true;
        if (uint64_t lobby = parse_connect(GetCommandLineA())) {
            log_line("STEAM", "launched from an invite: lobby %llu", (unsigned long long)lobby);
            g_join_request = lobby;
        }
    }

    switch (L.state) {
        case LobbyState::Creating: {
            LobbyCreated_t r{}; bool failed = false;
            if (!call_done(L.call, kCbLobbyCreated, r, failed)) break;
            if (failed || r.result != kResultOK || !r.lobby) {
                L.state = LobbyState::Failed;
                set_status("could not create a Steam lobby (result %d)", r.result);
                break;
            }
            L.id    = r.lobby;
            L.owner = steam_self();
            L.state = LobbyState::Hosting;
            char proto[16];
            _snprintf_s(proto, sizeof(proto), _TRUNCATE, "%u", (unsigned)kProtoVersion);
            api.MM_SetLobbyData(api.mm, L.id, "mgmp", proto);
            set_presence();
            set_status("hosting -- invite a friend");
            break;
        }
        case LobbyState::Joining: {
            LobbyEnter_t r{}; bool failed = false;
            if (!call_done(L.call, kCbLobbyEnter, r, failed)) break;
            if (failed || r.response != kEnterSuccess || !r.lobby) {
                L.state = LobbyState::Failed;
                set_status("could not join that lobby (response %u) -- it may be full "
                           "or already closed", r.response);
                break;
            }
            L.id    = r.lobby;
            L.owner = api.MM_GetLobbyOwner(api.mm, L.id);
            L.state = LobbyState::Joined;
            set_presence();
            set_status("in %s's lobby -- connecting", steam_persona_of(L.owner));
            if (!net_join_steam(L.owner))
                set_status("!! joined the lobby but the transport did not start: %s",
                           net_error());
            break;
        }
        case LobbyState::Hosting:
            host_sync_members();
            break;
        case LobbyState::Joined:
            client_watch_host();
            break;
        default:
            break;
    }
}

bool steam_host_lobby() {
    if (!resolve()) { set_status("Steam is not available"); return false; }
    steam_leave_lobby();
    L.call  = api.MM_CreateLobby(api.mm, kLobbyFriendsOnly, kMaxPeers);
    L.state = L.call ? LobbyState::Creating : LobbyState::Failed;
    set_status(L.call ? "creating a lobby..." : "CreateLobby refused");
    return L.call != 0;
}

bool steam_join_lobby(uint64_t lobby) {
    if (!resolve()) { set_status("Steam is not available"); return false; }
    steam_leave_lobby();
    L.call  = api.MM_JoinLobby(api.mm, lobby);
    L.state = L.call ? LobbyState::Joining : LobbyState::Failed;
    set_status(L.call ? "joining a lobby..." : "JoinLobby refused");
    return L.call != 0;
}

void steam_leave_lobby() {
    if (!api.resolved) return;
    if (L.id) {
        api.MM_LeaveLobby(api.mm, L.id);
        log_line("STEAM", "left lobby %llu", (unsigned long long)L.id);
    }
    if (L.state != LobbyState::None) api.FR_ClearRichPresence(api.fr);
    L = Lobby{};
}

void steam_open_invite_dialog() {
    if (!api.resolved || !L.id) return;
    api.FR_InviteDialog(api.fr, L.id);
}

LobbyState  steam_lobby_state()  { return L.state; }
const char* steam_lobby_status() { return L.status; }
uint64_t    steam_lobby_id()     { return L.id; }

int steam_lobby_members(uint64_t* out, int cap) {
    if (!api.resolved || !L.id) return 0;
    int n = api.MM_GetNumLobbyMembers(api.mm, L.id);
    int k = 0;
    for (int i = 0; i < n && k < cap; ++i) out[k++] = api.MM_GetLobbyMemberByIndex(api.mm, L.id, i);
    return k;
}

int steam_friend_lobbies(FriendLobby* out, int cap) {
    if (!resolve()) return 0;
    const uint32_t app = api.UT_GetAppID(api.ut);
    int n = api.FR_GetFriendCount(api.fr, kFriendFlagImmediate);
    int k = 0;
    for (int i = 0; i < n && k < cap; ++i) {
        uint64_t f = api.FR_GetFriendByIndex(api.fr, i, kFriendFlagImmediate);
        FriendGameInfo_t gi{};
        if (!api.FR_GetFriendGamePlayed(api.fr, f, &gi)) continue;
        if ((uint32_t)(gi.game_id & 0xFFFFFF) != app || !gi.lobby) continue;
        out[k].friend_id = f;
        out[k].lobby     = gi.lobby;
        _snprintf_s(out[k].name, sizeof(out[k].name), _TRUNCATE, "%s", steam_persona_of(f));
        ++k;
    }
    return k;
}

uint64_t steam_take_join_request() {
    uint64_t r = g_join_request;
    g_join_request = 0;
    return r;
}

}
