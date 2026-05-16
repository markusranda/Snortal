#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>    
#include <stdarg.h>
#include <thread>
#include <chrono>
#include "raylib.h"
#include "raymath.h"
#include "base_num.h"
#include "net.h"

// ======================================= CONSTS ==============================================

#define MAX_STATIC_THINGS           1024
#define MAX_THINGS                  1024
#define MAX_CLIENTS                 16
#define CAMERA_SMOOTHNESS           80.0f
#define SCREEN_WIDTH                1600
#define SCREEN_HEIGHT               900
#define TILE_SIZE_F                 64.0f
#define IDX_NIL                     0
#define DEFAULT_ROT_AXIS            { 0.0f, 1.0f, 0.0f }
#define GRAVITY_ACCEL               1200.0f
#define JUMP_SPEED                  650.0f
#define WALK_ACCEL                  1200.0f
#define MAX_WALK_SPEED              1200.0f
#define WORLD_FORWARD               {  0.0f, 0.0f, 1.0f }
#define WORLD_UP                    {  0.0f, 1.0f, 0.0f }
#define WORLD_RIGHT                 {  1.0f, 0.0f, 0.0f }
#define WORLD_LEFT                  { -1.0f, 0.0f, 0.0f }
#define MAX_SPARKS                  256
#define MAX_HIT_COUNT               64
#define MAX_NET_BUFFER_BYTES        1024 * 1024
#define MAX_UDP_PACKET_BYTES        65507
#define PLAYER_MAX_HEALTH           100.0f

// ======================================= DATASTRUCTURES ======================================

enum class ThingType : u32 {
    Nil,
    Portal,
    PortalProjectile,
    Landmine,
    Player,
};

enum ThingFlag : u32 {
    Nil      = 1 << 0,
    Gravity  = 1 << 1,
    Visible  = 1 << 2,
    Left     = 1 << 3,
    Grounded = 1 << 4,
    Dead     = 1 << 5,
};

enum class Face {
    Nil,
    Left,
    Right,
    Top,
    Bottom,
    Front,
    Back,
};

struct AABB {
    Vector3 min; // World center-position of the AABB
    Vector3 max; // Half size extending from center
};

struct StaticThing {
    u32       model_idx;
    AABB      aabb;
    Vector3   pos;
    Vector3   siz;
    Vector3   rot_axis;
    f32       rot_deg;
    u32       sound_idx;
};

struct Thing {
    ThingType type;
    u32       thing_idx;
    u32       associated_thing_idx;
    u32       model_idx;
    u32       client_idx;
    u32       at_frame_count;
    Vector3   pos;                 // World space
    Vector3   vel;
    Vector3   siz;
    Vector3   dir;
    Vector3   rot_axis;
    f32       rot_deg;
    f32       friction;            // A force similar to velocity
    f32       health;
    u32       flags;               // ThingFlag
    u64       died_at_millis;
    Vector3   portal_spawn_pos;
    Vector3   basis_forward;
    Vector3   basis_up;
    Vector3   basis_right;
    Vector3   hitbox_offset;       // Local space
    Vector3   hitbox_siz;          // Local space
};

struct GeometricFace {
    Face    face;
    f64     dist;
    Vector3 vec;
};

enum class PacketType : u32 {
    Nil,
    UpdateClientState,
    UpdateStaticThings,
    UpdateThings,
    Connect,
    Disconnect,
    KeepAlive,
    Update,
};

enum InputButton : u32 {
    InputButton_Forward  = 1 << 0,
    InputButton_Brake    = 1 << 1,
    InputButton_Jump     = 1 << 2,
    InputButton_FireA    = 1 << 3,
    InputButton_FireB    = 1 << 4,
};

struct ClientToServerPacket {
    PacketType type;
    u32 client_idx;
    f32 camera_yaw;   // Degrees
    f32 camera_pitch; // Degrees
    u32 btn_state;
};

struct ServerToClientPacket {
    PacketType type;
    u32 things_count;
    u32 static_things_count;
};


enum class ClientStatus : u32 {
    Nil,
    Live,
};

struct ClientState {
    ClientStatus status;
    u32 client_idx;
    u32 player_idx;
    NetAddress address;
    u64 last_seen;
    u32 btn_pressed; // InputButton
    u32 btn_state;   // InputButton
    u32 portal_projectile_idx_a;
    u32 portal_projectile_idx_b;
    u32 portal_idx_a;
    u32 portal_idx_b;
    f32 camera_yaw;   // Degrees
    f32 camera_pitch; // Degrees
};

enum GameTexture : u32 {
    Texture_Nil,
    Texture_Floor,
    Texture_Crate,
    Texture_FullColor,
    Texture_Wall,
    Texture_Landmine,
    Texture_COUNT,
};

enum GameSound : u32 {
    Sound_Nil,
    Sound_Explode1,
    Sound_Explode2,
    Sound_Explode3,

    Sound_Die1,
    Sound_Die2,
    Sound_Die3,

    Sound_Skate1,
    Sound_Skate5,
    Sound_SkateJump1,

    Sound_Radio,

    Sound_COUNT,
};

enum GameModel : u32 {
    Model_Nil,
    Model_Floor,
    Model_Crate,
    Model_Portal,
    Model_PortalSphere,
    Model_Wall,
    Model_Landmine,
    Model_Player,
    Model_Radio,
    Model_COUNT,
};

// ======================================= HELPERS =============================================

static inline float rnd_range(float min, float max) {
    float t = (float)rand() / ((float)RAND_MAX + 1.0f);
    return min + t * (max - min);
}

static inline void sleep_seconds(double seconds) {
    std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
}

static inline double now_seconds() {
    using clock = std::chrono::steady_clock;
    return std::chrono::duration<double>(clock::now().time_since_epoch()).count();
}

static inline u64 now_millis() {
    using clock = std::chrono::steady_clock;
    return std::chrono::duration_cast<std::chrono::milliseconds>(clock::now().time_since_epoch()).count();
}

static inline u64 wall_millis() {
    using clock = std::chrono::system_clock;
    return std::chrono::duration_cast<std::chrono::milliseconds>(clock::now().time_since_epoch()).count();
}

enum LogSeverity {
    LOG_INF,
    LOG_WRN,
    LOG_ERR,
    LOG_COUNT
};

static const char *LOG_STR[LOG_COUNT] = {
    "INF",
    "WRN",
    "ERR"
};

void log_print(LogSeverity sev, const char *fmt, ...) {
    u64 t = wall_millis();

    const char *s = (sev < LOG_COUNT) ? LOG_STR[sev] : "UNK";

    printf("[%llu][%s] ", (u64)t, s);

    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);

    printf("\n");
}

AABB get_thing_aabb(Thing *thing) {
    // We just convert hitbox from local space to world space, and include the hitbox size
    Vector3 center = thing->pos + thing->hitbox_offset;
    Vector3 half_size = { thing->hitbox_siz.x * 0.5f, thing->hitbox_siz.y * 0.5f, thing->hitbox_siz.z * 0.5f };

    return AABB{
        .min = {
            center.x - half_size.x,
            center.y - half_size.y,
            center.z - half_size.z,
        },
        .max = {
            center.x + half_size.x,
            center.y + half_size.y,
            center.z + half_size.z,
        },
    };
}

Vector3 pos_player_head(Thing *player) {
    return { player->pos.x, player->pos.y + player->siz.y * 0.5f, player->pos.z };
}

Vector3 pos_player_feet(Thing *player) {
    return { player->pos.x, player->pos.y - player->siz.y * 0.5f, player->pos.z };
}
