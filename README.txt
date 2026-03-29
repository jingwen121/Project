=====================================================================
  FIGHTING GAME – Multiplayer UDP Game  |  CSD2161 Assignment 5
=====================================================================

OVERVIEW
--------
A multiplayer 2-4 player fighting game over UDP using a Client-Server
architecture. All game logic runs on the server; clients send inputs
and render the authoritative state.

NOTE
----
This build is intended to be run in Visual Studio as Release | x64.
The Alpha Engine DLLs and Assets are copied next to Clients.exe during
the post-build step.

CONTROLS (in-game)
------------------
  A/D or Left/Right    - Move
  Click or Space       - Shoot

HUD
---
  - Live scores are shown during gameplay.
  - Your remaining lives are shown as 3 colored boxes at the top-right.

SCORING
-------
  - Score is server-authoritative and is based on player kills.

LEADERBOARD
-----------
  - Top 5 all-time scores are saved to leaderboard.bin and shown at the
    end of the round (with date/time).
  - To clear history: close Servers.exe and delete leaderboard.bin in the
    server working directory.

----------------------------------------------------------------------
HOW TO BUILD (Visual Studio 2022)
----------------------------------------------------------------------

1) Open Project.sln
2) Select: Release | x64
3) Build -> Build Solution

Outputs
-------
  - Clients.exe: bin\Release-x64\Clients.exe
  - Servers.exe: build output for the Servers project (run from Visual Studio
    or locate the generated Servers.exe in the solution output folders).

----------------------------------------------------------------------
HOW TO RUN (LOCAL TEST)
----------------------------------------------------------------------

1) Start the server first (Release | x64)
   - In Visual Studio: set Servers as Startup Project -> Ctrl+F5
   - Or run Servers.exe directly

2) Start 2-3 clients (Release | x64)
   - Run Clients.exe multiple times to create multiple players.

Notes
-----
  - Clients currently connect to 127.0.0.1 (same machine).
  - leaderboard.bin is created in the server working directory.
    Delete it to clear the Top 5 history.

----------------------------------------------------------------------
TECH NOTES
----------------------------------------------------------------------

Network
  - Pure UDP (see protocol in common/game_protocol.h)
  - Client-Server topology; server is authoritative

Gameplay
  - Players can move and shoot.
  - Lives: each player has multiple lives; eliminated players stop rendering.
  - Round over screen shows winners + Top 5 all-time leaderboard.

----------------------------------------------------------------------
