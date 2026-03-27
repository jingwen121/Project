=====================================================================
  SPACESHIPS – Multiplayer LAN Game  |  CSD2161 Assignment 5
=====================================================================

OVERVIEW
--------
A multiplayer Spaceships (Asteroids-style) game over LAN using pure
UDP with a Client-Server architecture.  Up to 4 players can play
simultaneously.  All game logic runs on the server; clients send
inputs and render the authoritative state.

CONTROLS (in-game)
------------------
  W / Up Arrow     - Thrust forward
  A / Left Arrow   - Rotate left
  D / Right Arrow  - Rotate right
  Space            - Fire bullet
  T                - Open chat mode (Enter to send, ESC to cancel)
  Q                - Quit

VISUAL LEGEND
-------------
  @  - Your ship (alive)
  *  - Nose of your ship
  A-F - Other players
  O  - Large asteroid (30pts)
  o  - Medium asteroid (50pts)
  .  - Small asteroid (100pts)
  -  - Bullet

----------------------------------------------------------------------
HOW TO BUILD (Visual Studio 2022)
----------------------------------------------------------------------

Server project:
  - Source: server/server_main.cpp
  - Include: common/  (add to include directories)
  - Language standard: C++17
  - Linker: ws2_32.lib  (already in protocol.h via #pragma comment)
  - Output: SpaceshipsServer.exe

Client project:
  - Source: client/client_main.cpp
  - Include: common/  (add to include directories)
  - Language standard: C++17
  - Linker: ws2_32.lib
  - Output: SpaceshipsClient.exe

----------------------------------------------------------------------
HOW TO RUN
----------------------------------------------------------------------

STEP 1 – Configure the server
  Edit server/server_config.txt:
    SERVER_PORT=9000     (pick any free UDP port)
    MIN_PLAYERS=2        (game starts when this many join)
    MAX_PLAYERS=4

STEP 2 – Start the server
  > SpaceshipsServer.exe
  (Press Enter to accept defaults, or edit server_config.txt first)
  The server will print its listening port and wait for players.

STEP 3 – Configure each client
  Edit client/config.txt on each machine:
    SERVER_IP=<server's LAN IP address>   e.g. 192.168.1.10
    SERVER_PORT=9000
    CLIENT_PORT=9001   (each client must use a unique port)
    PLAYER_NAME=YourName

STEP 4 – Start clients
  > SpaceshipsClient.exe
  Press Enter to accept config file defaults, or type new values.
  Once MIN_PLAYERS have joined the server starts the round.

RUNNING MULTIPLE CLIENTS ON THE SAME MACHINE (for testing)
  Each client MUST use a different CLIENT_PORT:
    Client 1: CLIENT_PORT=9001
    Client 2: CLIENT_PORT=9002
    Client 3: CLIENT_PORT=9003
  Use SERVER_IP=127.0.0.1 when server is local.

----------------------------------------------------------------------
ARCHITECTURE NOTES (for design document)
----------------------------------------------------------------------

Network:
  - Pure UDP (required by assignment for options 1 & 2)
  - Client-Server topology; server is authoritative for all state
  - Two reliability classes:
      * Unreliable (fire-and-forget): position updates, bullet spawns
      * Reliable (seq + ACK): asteroid destruction, round-over events
        The server retransmits every ~0.5s until all clients ACK.
        This implements the lock-step hint from the spec.

Synchronisation:
  - Asteroid creation: server spawns, broadcasts reliable spawn packet;
    clients add asteroid on receipt.
  - Asteroid movement: server-authoritative positions broadcast each
    tick via MSG_GAME_STATE; clients render from this.
  - Asteroid destruction: server sends MSG_ASTEROID_DEAD with a seq
    number; server blocks removal until ACK from all clients
    (lock-step). Clients send MSG_ACK immediately on receipt.
  - Ship movement: client sends input every tick; server integrates
    physics and broadcasts positions.
  - Bullets: server spawns, broadcasts position each tick.
  - Scoring: server-authoritative; broadcast on every score change.

Robustness:
  - Client timeout (10s): server removes player, others continue.
  - Client reconnect: re-sending MSG_JOIN triggers MSG_REJOIN_ACK
    with a full state snapshot so the rejoining client catches up.
  - Server retransmits reliable messages to non-ACKing clients.

Leaderboard:
  - Top-5 scores saved to leaderboard.bin (binary struct array) on
    every round end.
  - Date/time stamp is recorded for each entry.
  - Sent to all clients in MSG_ROUND_OVER so they can display it.

----------------------------------------------------------------------
GRADING CHECKLIST (from supplementary rubric – targeting Grade A)
----------------------------------------------------------------------
[x] 2+ players over LAN via UDP
[x] Asteroid creation synchronised (server-authoritative, reliable)
[x] Asteroid movement synchronised (broadcast each tick)
[x] Asteroid destruction synchronised (lock-step ACK)
[x] Ship movement synchronised (server-integrated physics)
[x] Firing synchronised (server-validated, broadcast)
[x] Bullets synchronised (deterministic, server positions)
[x] Per-player live score display
[x] Top-5 leaderboard with names, scores, date/time
[x] Win condition (all asteroids cleared OR all lives lost)
[x] Client disconnect does not crash/hang other clients
[x] Client reconnect resumes game
[x] Text chat (T key in-game)
[x] README with run instructions
[x] Single-player mode (set MIN_PLAYERS=1 in server_config.txt)

----------------------------------------------------------------------
