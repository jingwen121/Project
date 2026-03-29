/* Start Header ***************************************************************/
/*!
\file game_math.h
\author TODO
\par email: TODO
\date 27 Mar, 2026
\brief
  Lightweight fixed-point math and 2D helpers shared between server and client.
  Using integer fixed-point (x1000) avoids float drift across machines.
Copyright (C) 2026 DigiPen Institute of Technology.
Reproduction or disclosure of this file or its contents without the prior
written consent of DigiPen Institute of Technology is prohibited. */
/* End Header *****************************************************************/

#ifndef GAME_MATH_H
#define GAME_MATH_H

#include <cstdint>
#include <cmath>

// ── Fixed-point scale ────────────────────────────────────────────────────────
constexpr int32_t FP_SCALE = 1000; // store positions as int * 1000

// ── World dimensions (logical pixels) ───────────────────────────────────────
constexpr int32_t WORLD_W = 800;
constexpr int32_t WORLD_H = 600;

// ── Wrap a fixed-point coordinate around the world ──────────────────────────
inline int32_t WrapFP(int32_t v, int32_t maxFP) {
    while (v < 0)      v += maxFP;
    while (v >= maxFP) v -= maxFP;
    return v;
}

// ── Convert degrees to radians ───────────────────────────────────────────────
inline float DegToRad(float deg) {
    return deg * 3.14159265f / 180.0f;
}

// ── Euclidean distance squared (fixed-point inputs, returns FP^2) ────────────
inline int64_t DistSqFP(int32_t ax, int32_t ay, int32_t bx, int32_t by) {
    int64_t dx = ax - bx, dy = ay - by;
    return dx * dx + dy * dy;
}

// ── Collision radius per asteroid size (in FP units) ────────────────────────
inline int32_t AsteroidRadiusFP(uint8_t size) {
    // size: 3=large(30px), 2=medium(20px), 1=small(10px)
    return static_cast<int32_t>(size) * 10 * FP_SCALE;
}

// ── Ship collision radius ────────────────────────────────────────────────────
constexpr int32_t SHIP_RADIUS_FP   = 12 * FP_SCALE;
constexpr int32_t BULLET_RADIUS_FP = 3 * FP_SCALE;

// ── Fighter health/damage (logical units, not FP) ────────────────────────────
constexpr int32_t PLAYER_MAX_HEALTH = 100;
constexpr int32_t BULLET_DAMAGE_HP  = 20;

// ── Angle helpers (stored as int16 degrees*10) ───────────────────────────────
inline int16_t WrapAngle(int16_t a) {
    while (a <     0) a += 3600;
    while (a >= 3600) a -= 3600;
    return a;
}

// ── Physics constants ────────────────────────────────────────────────────────
constexpr int32_t SHIP_TURN_RATE      = 50;   // degrees*10 per tick
constexpr int32_t SHIP_THRUST_FP      = 200;  // FP velocity added per tick
constexpr int32_t SHIP_MAX_SPEED_FP   = 5000; // max velocity magnitude in FP
constexpr int32_t SHIP_FRICTION       = 990;  // velocity *= 990/1000 each tick (0.99)
constexpr int32_t BULLET_SPEED_FP     = 12000;// bullet speed in FP per tick
constexpr uint32_t BULLET_LIFETIME    = 60;   // ticks before despawn
constexpr uint32_t RESPAWN_TICKS      = 120;  // 4 seconds at 30Hz
constexpr int      INITIAL_LIVES      = 3;

// ── Fighting-game style movement (server-side) ───────────────────────────────
constexpr int32_t FIGHTER_MOVE_SPEED_FP  = 4000;  // horizontal speed per tick
constexpr int32_t FIGHTER_JUMP_SPEED_FP  = 14000; // initial jump velocity (upward)
constexpr int32_t FIGHTER_GRAVITY_FP     = 600;   // gravity applied each tick (downward)

// ── Scoring ──────────────────────────────────────────────────────────────────
constexpr uint16_t SCORE_PLAYER_KILL     = 200;
constexpr uint16_t SCORE_LARGE_ASTEROID  = 20;
constexpr uint16_t SCORE_MEDIUM_ASTEROID = 50;
constexpr uint16_t SCORE_SMALL_ASTEROID  = 100;

inline uint16_t AsteroidScore(uint8_t size) {
    switch (size) {
        case 3: return SCORE_LARGE_ASTEROID;
        case 2: return SCORE_MEDIUM_ASTEROID;
        default: return SCORE_SMALL_ASTEROID;
    }
}

// ── Asteroid spawn velocities (FP) ───────────────────────────────────────────
constexpr int32_t ASTEROID_SPEED_LARGE_FP  = 800;
constexpr int32_t ASTEROID_SPEED_MEDIUM_FP = 1400;
constexpr int32_t ASTEROID_SPEED_SMALL_FP  = 2200;

inline int32_t AsteroidSpeedFP(uint8_t size) {
    switch (size) {
        case 3: return ASTEROID_SPEED_LARGE_FP;
        case 2: return ASTEROID_SPEED_MEDIUM_FP;
        default: return ASTEROID_SPEED_SMALL_FP;
    }
}

// ── Initial asteroid count ────────────────────────────────────────────────────
constexpr int INITIAL_ASTEROIDS = 6; // large asteroids at round start

#endif // GAME_MATH_H
