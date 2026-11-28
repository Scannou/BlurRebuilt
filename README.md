# Blur Rebuilt

This is the 2010 game **Blur**, by Bizarre Creations, brought up to date for modern PCs.

## What's new

The original game was a 32-bit executable using DirectX 9, and had numerous issues when framerate was allowed
to go over 60 FPS.
This project intends to solve all of that.

### DirectX 11

The original DirectX 9 renderer has been replaced with a DirectX 11 one. All shaders had to be recompiled
for the new API.

### HDR

Made possible by the switch to DX11, this version of the game supports real HDR.
On an HDR monitor with HDR turned on in Windows, the game will detect your display's peak brightness, and
headlights, neon, street lamps, power-ups and the sky will be able to shine brighter than the rest of the picture.
Menus and the HUD stay at normal brightness.

### Unlocked frame rate

The original game was designed around 60 fps, and some things (cars, cameras, audio, particles, menus, etc.)
misbehaved the farther you went above that.
This version runs with unlocked frame rate and everything behaves as expected.

### Decreased audio latency

The retail game had pretty noticeable audio latency, especially when audio quality was set to high.
This should now be fixed as well.

### Dynamic switching of input glyphs

Retail Blur doesn't show Xbox input hints when navigating the menus, even when the glyphs were there (they
are shown when joining a split screen game an other rare occasions).
We now feature dynamic switching of the on-screen glyphs as input is detected.

### Ultra widescreen support

The menus and race intro/outro have been fixed for ultra widescreen displays (tested on 32:9).

### 64-bit

The game is now a native 64-bit application.

## Online multiplayer support

Online multiplayer is supported via Amax Emu.
To set it up, unpack the Amax Emu client zip file into a folder called `amax_client_files`.
The rest of the setup is the same as described on [Amax Emu's instruction page](https://amax-emu.com/how_to_play).

## Requirements

- An installed copy of Blur for PC
- Windows 10 or 11, 64-bit
- A DirectX 11 graphics card
- [Microsoft Visual C++ Redistributable (x64)](https://aka.ms/vs/17/release/vc_redist.x64.exe)

## Installation

1. Copy `BlurRebuilt.exe`, `AmaxClientHost.exe` and the `fx11` folder into your Blur install folder. That's
   the one containing `Blur.exe`, usually `C:\Program Files (x86)\Activision\Blur(TM)`.
2. Run `BlurRebuilt.exe`.

Your original `Blur.exe` isn't affected, and you can keep using it.

Before your first race, back up `%APPDATA%\Bizarre Creations\Blur`. Your save is
stored there, and both versions of the game use it.

## Optional settings

These Windows environment variables are available if you want to adjust things:

| Variable | Effect |
|---|---|
| `BLUR_DX11_HDR=off` | Play in SDR even with Windows HDR on |
| `BLUR_DX11_HDR_PEAK_NITS=1000` | Set your display's peak brightness manually |
| `BLUR_DX11_HDR_PAPER_WHITE_NITS=200` | Set the brightness of menus and everyday surfaces |
| `BLUR2PC_FRAMELIMIT=120` | Cap the frame rate |

### BlurRebuilt.ini

Blur Rebuilt's own settings are in `%APPDATA%\Bizarre Creations\Blur\BlurRebuilt.ini`, next to your save. The game creates it on first start with every setting commented out; remove the `;` in front of a line to use it. Edit it while the game is closed; the race events settings are also picked up at the start of every race.

| Setting | Effect |
|---|---|
| `player_name=YourName` | Your name in LAN and offline games (up to 15 plain ASCII characters) |
| `speed_units=kmh` | Show speeds in km/h instead of mph (also in Options → Speed units) |
| `race_events_file=off` | Stop writing the race events file, or give a full file path to write it somewhere else |
| `race_events_udp=lights.local:5555` | Also send every race event as a UDP packet to that host and port as it happens. The host can be a name, an IP address or a broadcast address such as `192.168.1.255` |
| `audio_menu_buffer_ms=64` | Sound buffer in the menus, which stops gaps in the menu music (default 64 ms; 0 uses the race buffer) |
| `audio_buffer_ms=40` | Sound buffer in races, if sounds stutter there (default about 30 ms; longer means sounds play slightly later) |
| `server_mode=1` | Run this PC as a dedicated LAN server (see below), with `server_min_players`, `server_countdown`, `server_race1`…, `server_order`, `server_web_port` and `server_web_password` |

The environment variables `BLUR_RACE_EVENTS` and `BLUR_RACE_EVENTS_UDP` still work when the matching setting isn't in the file.

## Racing alone on LAN

A LAN host can start a race with nobody else in the lobby. If AI is off, the host races 3 medium AI opponents and the lobby's AI settings change to match (except in Motor Mash, which has no AI). Team games still need other players.

## Dedicated LAN server

A spare PC can run the LAN game for everyone else: set `server_mode=1` in its `BlurRebuilt.ini` and start the game. It goes from the title screen to a hosted LAN custom race on its own, so nobody needs to sit at it, and it keeps running while minimised or in the background.

- **Starting races:** once `server_min_players` other players are in the lobby (default 1), it picks the next race and runs the lobby's countdown (`server_countdown`, default 60 seconds). Players see the countdown and can still use the garage. Anyone still in the garage or mod shop at zero gets up to 30 seconds more.
- **The server's own car:** it leaves each race at the green light the way an eliminated car does, then the server watches. It takes one player slot and is listed as eliminated in the results.
- **After a race:** the results screens move on by themselves, everyone returns to the lobby and the next countdown starts.
- **Races:** `server_race1` to `server_race32`, each `mode track laps class`, for example `server_race1=race random 2 A` or `server_race2=motormash random 3 any`. The mode is `race` or `motormash`; the track is a name from `server_tracks.txt` (the server writes it next to `BlurRebuilt.ini`) or `random`; laps are minutes for Motor Mash; the class is `A` to `E` or `any`. Without any `server_race` line, every race is a random track with the lobby's settings. `server_order=shuffle` plays them in random order.
- **Web page:** open `http://<server PC>:8080` from any device on the LAN. It shows the lobby, the countdown, the players and their state, the race events as they happen and the server's log. Its buttons start the race now or skip to the next one, and it edits the playlist and the settings above (saved to `BlurRebuilt.ini`). `server_web_port` changes the port (`0` turns the page off) and `server_web_password` asks for a password before any button works. Windows asks once whether to let the game through its firewall: allow it on private networks.
- **Log:** the server writes what it does to `server.log` next to `BlurRebuilt.ini`.

Keep one class across the races: when the class changes, players whose car doesn't fit must pick another before the race can start.

## Race events

LAN and online races write their events to `%APPDATA%\Bizarre Creations\Blur\race_events.jsonl`, one JSON object per line, so other tools can follow races. Events: `race_start`, `player`, `lap`, `overtake`, `powerup_hit`, `wreck`, `finish` (when each player finishes, with their finishing order and race time), `result` (every player's position and statistics) and `race_end`. Players are identified by `id`, which is the same on every PC (the `index` differs between PCs). Laps, overtakes, power-up hits and wrecks are logged for every player. Each PC works these out from its own view of the race, so two PCs can differ slightly on close moments; `result` lines are the same everywhere. A wreck says how its cause was found in `by`: `attacker` is the car the game credited with the wreck, `last_hit` is the car that last hit the wrecked one, `self` means nobody else was involved, and `unknown` means another PC's car was wrecked with no recent hit seen here. To wait for the game's credit, a wreck can be written up to a second after it happened; its `t` is still the moment of the wreck.

Your name in LAN and offline games is your Windows user name (your Linux user name under Proton). To use a different name, set `player_name` in `BlurRebuilt.ini`, or put a `username.txt` file next to `BlurRebuilt.exe` with the name on its first line. Names can use up to 15 plain ASCII characters.

## Intro videos

The intro videos don't play. The game goes straight to the title screen. That's expected.

## Changelog

<table>
  <tr>
  </tr>
   <td><strong>v1.9 (2026/11/23)</strong></td>
   <td>Fixed LAN functionality</td>
  </tr>
  </tr>
   <td><strong>v1.8 (2026/11/19)</strong></td>
   <td>Fixed after-race crashes<br>Fixed color corruption on AMD GPUs</td>
  </tr>
  </tr>
   <td><strong>v1.7 (2026/11/07)</strong></td>
   <td>Fixed error downloading data when trying to play online</td>
  </tr>
  </tr>
   <td><strong>v1.6 (2026/11/02)</strong></td>
   <td>Fixed an audio timing issue that could cause stuttering or repeating sound on some systems<br>Fixed release process to include previously missing Amax Emu client</td>
  </tr>
  <tr>
    <td><strong>v1.5 (2026/10/24)</strong></td>
    <td>More ultra widescreen fixes<br>Support for online multiplayer via Amax Emu</td>
  </tr>
  <tr>
    <td><strong>v1.4 (2026/10/23)</strong></td>
    <td>Fixed menus for ultra widescreen</td>
  </tr>
  <tr>
    <td><strong>v1.3 (2026/10/23)</strong></td>
    <td>Implemented dynamic switching of on-screen input glyphs</td>
  </tr>
  <tr>
    <td><strong>v1.2 (2026/10/22)</strong></td>
    <td>Decreased audio latency in high quality setting</td>
  </tr>
  <tr>
    <td><strong>v1.1 (2026/10/22)</strong></td>
    <td>Fix race HUD in ultra widescreen</td>
  </tr>
  <tr>
    <td><strong>v1.0 (2026/10/21)</strong></td>
    <td>Initial release</td>
  </tr>
</table>

## Comments

For comments and general discussion, go to https://www.reddit.com/r/BlurGame
