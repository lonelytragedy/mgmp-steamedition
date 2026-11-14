# mgmp Steam Edition — co-op multiplayer for Mewgenics over Steam

An unofficial mod that lets two people play one Mewgenics adventure together —
invite a friend through Steam and play the same run, the same map, the same cats
and the same fights.

This is a fork of [mgmp](https://github.com/SanTertrust/mgmp) by SanTertrust,
which did the hard part: deterministic battle lockstep and host-authoritative
sync of everything outside battle. The Steam Edition replaces "forward a port
and edit a config file" with **one file and a button**.

It is not an official feature and has nothing to do with the developers of
Mewgenics.

---

## What is different in the Steam Edition

| | original mgmp | Steam Edition |
|---|---|---|
| Install | loader + `mgmp.dll` + `mgmp.json` | drop **one** `version.dll` into the game folder |
| Start | run `mgmp_loader.exe` | start the game from Steam as usual |
| Connect | direct TCP, port forwarding or a VPN | Steam lobby + invite, Steam P2P with relay |
| Host / join | config file or debug panel | **Multiplayer** button in the game |

Plus fixes found while playing it over Steam:

- **Statuses no longer double on the client.** Applying a host cat over a live
  cat appended its next-fight statuses (e.g. Bleed 1 became Bleed 2 on the
  client) — a guaranteed desync a few turns into the next battle.
- **Facing comes from actions only.** Previews on the cat owner's machine could
  turn a cat, the AI picked a different backstab square, and the battle split.
- **Each player keeps the same cats for the whole run**, instead of a new split
  every battle.
- **The client is never handed a copy of the host's house.** The run is sent
  once the host is out on the map, and again every time the host sets out
  after a visit home.
- **The host's cats and inventory are re-sent at every node**, so a change made
  on the client (an item equipped there) cannot linger.
- **A lobby hiccup does not end the session** — a player must be missing for
  several seconds before they count as gone.
- The save folder is found by the logged-in Steam account, not guessed.
- No crash on quitting the game; one log file instead of one per launch.

---

## What you need

- **Windows**, 64-bit.
- **Mewgenics on Steam**, the same version on both machines (currently
  `1.1.21239`). The mod checks this when you connect.
- You and your partner are **friends on Steam**.
- **Both players** install the same build of the mod.

---

## Install

1. Steam → Library → Mewgenics → right click → **Manage → Browse local files**.
2. Copy `version.dll` into that folder, next to `Mewgenics.exe`.

That is all: no loader, no config, no ports. To uninstall, delete `version.dll`.

Get `version.dll` from this repository's Releases page, or build it yourself
(see [Building](#building)).

---

## Playing

Both players start the game normally from Steam. A **Multiplayer** button sits
in the top-right corner of the screen.

**Host** (the player whose save you both play):

1. **Multiplayer → Host over Steam → Invite a friend.**
2. Main menu → Play → **click your save slot.** This click is how the mod knows
   which save to share — do it *after* the lobby is open. If you were already in
   a run, quit to the main menu and pick the slot again.
3. If the slot opens in your house, gather your cats and set out. The moment you
   are on the map, the run is sent to your partner.

**Partner:**

1. Stay on the **main menu**.
2. Accept the invite in Steam, or open **Multiplayer** and press **Join** next to
   the host's name.
3. Wait. When the host's run arrives, the mod presses Play, picks the slot and
   loads it for you. Your own saves are never touched — the host's run goes into
   a separate `mgmp_coop.sav`.

From there you just play: when it is your cat's turn, you move it; when it is
your partner's, you watch. Map choices, events, shops and level-ups are made by
the host and happen identically on both screens.

### Visiting home between areas

The house is not shared (see below). When the host goes home, each of you is in
your own copy of it. When the host sets out again, the partner's Multiplayer
window says so: **press Esc** and the mod quits to the menu and loads the host's
new run.

---

## Known limits

- **The house is single-player.** Breeding, furniture, the day cycle and
  everything at home happen only for the host. The partner's copy is a copy.
- **Equipment is managed by the host.** Anything the partner equips is reset to
  the host's version at the next map node — ask the host to equip it.
- **Cats are split automatically:** the first half of the run's cats are the
  host's, the rest the partner's, for the whole run.
- **Two players.** The code is written for up to four, but only two have been
  played.
- **One game version.** A big game patch may break the mod until it is updated.
- **Expect bugs.** It is a reverse-engineered mod in a game that was never built
  for this.

---

## When something goes wrong

Every battle turn and every map node, both games compare a fingerprint of their
state. If they ever disagree, the mod **stops both games** and writes what
differed to the log, instead of letting you play on in two different worlds.

The mod writes `mgmp_trace.log` in the game folder, overwritten on each launch;
the launch before it is kept as `mgmp_trace.prev.log`. To report a problem, send
both files from **both** players — one side's log can show that something went
wrong, but rarely what.

**F1** opens the debug panel: connection state, which cats each player controls,
turn agreement and a live log.

---

## Building

You need **Visual Studio 2022/2026** (or the MSVC build tools) and
**CMake 3.21+**.

```powershell
cmake -B build -A x64
cmake --build build --config Release
ctest --test-dir build --build-config Release
```

`build\Release\` then contains:

| file | what it is |
|---|---|
| `version.dll` | the Steam Edition: drop it next to `Mewgenics.exe` |
| `mgmp.dll` + `mgmp_loader.exe` | the original injector route, still supported |

### The original route (direct TCP)

The loader still works exactly as in upstream mgmp: put `mgmp.dll`,
`mgmp_loader.exe` and `mgmp.json` in one folder, set `game` and `net.role` /
`net.addr` in `mgmp.json`, and run the loader on both machines. It connects over
TCP port 27600, so one side has to be reachable (port forwarding, or a virtual
LAN such as Tailscale or ZeroTier). Do not use it together with `version.dll` —
the second copy notices the first and stands down.

---

## How it works, briefly

**Battles are deterministic**, so only the players' clicks are sent; both games
compute the outcome and compare a hash every turn (lockstep). **Everything
outside battle** is decided by the host and sent as state — cats, inventory, run
history — or as "which option the host picked", which the partner's game then
plays out identically.

The Steam Edition carries the same messages over Steam's P2P networking
(`ISteamNetworkingMessages`, relayed by Valve when a direct path fails), through
the game's own `steam_api64.dll` — no Steamworks SDK is shipped. A friends-only
lobby decides who is in the session. `version.dll` forwards every real
`version.dll` call to Windows and starts the mod on the game's first frame.

The full technical story of the original mod — addresses, struct offsets,
measurements and dead ends — is in `CLAUDE.md`.

---

## A note on what this is

This mod contains no Mewgenics code, art or data. It attaches to a legally
purchased copy of the game on your own machine; you need to own the game.

It is not affiliated with, endorsed by, or supported by the developers or
publisher of Mewgenics. Back up your saves before using it. If you report a bug
to the game's developers while this mod is loaded, mention that it is loaded.

Mewgenics is by Tyler Glaiel and Edmund McMillen. It is a very good game and you
should buy it.

Based on [mgmp](https://github.com/SanTertrust/mgmp) by SanTertrust. MIT
licensed, like the original — see `LICENSE`.
