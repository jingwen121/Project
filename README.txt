=====================================================================
  FIGHTING GAME – Multiplayer UDP Game  |  CSD2161 Assignment 5
=====================================================================

OVERVIEW
--------
A multiplayer 2–4 player fighting game over UDP using a Client–Server
architecture. The server is authoritative; clients send inputs and
render the state snapshots.

Features
--------
- Level 1 (level_1.cpp) runs the current game (HUD + chat + volume bar)
- Lives HUD (top-right) and health bars
- Round Over screen: winners + Top 5 all-time (with datetime)
- Laser SFX (Assets\Audio\laser.wav) + adjustable volume slider (top-middle)
- Reliable messages with ACK for round over events

NOTE
----
Build and run in Visual Studio as Release | x64 or Debug | x64.
Assets and DLLs are copied next to Clients.exe via post-build steps.

CONTROLS (in-game)
------------------
  A/D or Left/Right - Move
  Space / Left Mouse - Shoot
  T                  - Chat mode (Enter to send, ESC to cancel)
  +/-                - Adjust SFX volume (also draggable slider top-middle)

HUD
---
  - Live scores (top-left)
  - Lives (top-right): 3 colored boxes for local player
  - Volume bar (top-middle): dark grey track + white fill; click/drag to set volume

SCORING
-------
  - Server-authoritative; 1 point per kill

LEADERBOARD
-----------
  - Top 5 all-time saved to leaderboard.bin; displayed at round end with datetime
  - To clear: close Servers.exe and delete leaderboard.bin in the server working directory

----------------------------------------------------------------------
HOW TO BUILD (Visual Studio 2022)
----------------------------------------------------------------------

1) Open Project.sln
2) Select: Release | x64 (or Debug | x64)
3) Build -> Build Solution

Outputs
-------
  - Clients.exe: bin\<Config>-x64\Clients.exe
  - Servers.exe: built from Servers project (set as Startup Project to run)

----------------------------------------------------------------------
HOW TO RUN (LOCAL TEST)
----------------------------------------------------------------------

Server
  - Set Servers as Startup Project -> Ctrl+F5
  - Or run built Servers.exe directly

Client
  - Set Clients as Startup Project -> Ctrl+F5
  - Or run built Clients.exe from bin\<Config>-x64\
  - Assets must exist at bin\<Config>-x64\Assets\Audio\laser.wav

LAN
  - Clients connect to 127.0.0.1 by default; for LAN, change to server LAN IP in code or config.

----------------------------------------------------------------------
TECH NOTES
----------------------------------------------------------------------

Network
  - Pure UDP, client–server
  - Reliable ACK for round-over events

Gameplay
  - Level 1 (level_1.cpp) contains HUD, chat, volume bar, lives boxes
  - Sound effect loaded relative to executable path (Assets\Audio\laser.wav)
  - Round Over shows winners + Top 5 all-time


