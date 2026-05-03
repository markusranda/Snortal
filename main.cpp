#include <stdio.h>
#include <math.h>
#include <cstdint>
#include <assert.h>
#include <stdlib.h>
#include <ctime>
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"

// --- PROFILING ---
#ifdef _DEBUG
#include "tracy/Tracy.hpp"
#endif

// Important TODOS
// - Fix collisions.
// - Fix allocations.

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

typedef float f32;
typedef double f64;

/*
-------- CHEAT SHEET --------
    --- TRIG FUNCTIONS ---
        acos(ratio) => angle
        cos(angle) => ratio

    --- COLORS ---
        X / right   = red
        Y / up      = green
        Z / forward = blue

    --- CROSS PRODUCT ---
        Cross product = a vector perpendicular to both inputs
        
        Right hand rule:
        index finger first vector
        middle finger second vector
        thumb is the cross product result


    --- DOT PRODUCT --- 
        dot(a, b) = cos(angle between a and b)

        +1.0  → same direction        (0°)
        +0.7  → ~45° apart            (diagonal-ish)
        +0.0  → perpendicular         (90°)
        -0.7  → ~135° apart
        -1.0  → opposite direction    (180°)
        
        Practical meaning:
            dot(v, dir) > 0   → moving WITH dir
            dot(v, dir) < 0   → moving AGAINST dir
            dot(v, dir) == 0  → no movement along dir
-----------------------------
*/

// ======================================= CONSTS ==============================================

#define SCREEN_WIDTH      1600
#define SCREEN_HEIGHT     900
#define TILE_SIZE_F       64.0f
#define MAX_STATIC_THINGS 1024
#define MAX_THINGS        1024
#define IDX_NIL           0
#define DEFAULT_ROT_AXIS  { 0.0f, 1.0f, 0.0f }
#define GRAVITY_ACCEL     1200.0f
#define JUMP_SPEED        650.0f
#define WALK_ACCEL        2400.0f
#define MAX_WALK_SPEED    2400.0f
#define WORLD_FORWARD     {  0.0f, 0.0f, 1.0f }
#define WORLD_UP          {  0.0f, 1.0f, 0.0f }
#define WORLD_RIGHT       {  1.0f, 0.0f, 0.0f }
#define WORLD_LEFT        { -1.0f, 0.0f, 0.0f }
#define MAX_SPARKS        256
#define MAX_HIT_COUNT     64

// ======================================= DATASTRUCTURES ======================================

enum class Face {
    Nil,
    Left,
    Right,
    Top,
    Bottom,
    Front,
    Back,
};

enum class ThingType {
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
};

enum class HitType : u32 {
    Nil,
    Thing,
    StaticThing,
};

struct Hit {
    HitType   hit_type;
    ThingType thing_type;
    u32 idx;
};

struct Spark {
    Vector3 pos;
    Vector3 vel;
    f32 life;
    f32 max_life;
};

struct GeometricFace {
    Face    face;
    f64     dist;
    Vector3 vec;
};

struct Textures {
    Texture2D floor;
    Texture2D crate;
    Texture2D full_color;
    Texture2D wall;
    Texture2D landmine;
};

struct Sounds {
    Sound explode_1;
    Sound explode_2;
    Sound explode_3;
};

struct Models {
    Model floor;
    Model crate;
    Model portal;
    Model portal_sphere;
    Model wall;
    Model landmine;
};

struct AABB {
    Vector3 min; // World center-position of the AABB
    Vector3 max; // Half size extending from center
};

struct StaticThing {
    AABB      aabb;
    Vector3   pos;
    Vector3   siz;
    Model     *model;
    Vector3   rot_axis;
    f32       rot_deg;
    Color     tint;
};

struct Thing {
    ThingType type;
    Vector3   pos;              // World space
    Vector3   vel;
    Vector3   siz;
    Vector3   dir;
    Vector3   rot_axis;
    f32       rot_deg;
    Model     *model;
    Color     tint;
    u32       flags;
    Vector3   portal_spawn_pos;
    Vector3   basis_forward;
    Vector3   basis_up;
    Vector3   basis_right;
    Vector3   hitbox_offset;       // Local space
    Vector3   hitbox_siz;       // Local space
    f32       camera_yaw;         // Turning left/right around y axis
    f32       camera_pitch;       // Turning up/down around x axis
    f32       camera_render_yaw;
    f32       camera_render_pitch;
};

// ======================================= STATE ===============================================

// --- Assets ---
extern unsigned char assets_crate_png[];
extern unsigned int  assets_crate_png_len;
extern unsigned char assets_floor_tile_1_png[];
extern unsigned int  assets_floor_tile_1_png_len;
extern unsigned char assets_wall_1_png[];
extern unsigned int  assets_wall_1_png_len;
extern unsigned char assets_landmine_png[];
extern unsigned int  assets_landmine_png_len;

extern unsigned char assets_explode1_wav[];
extern unsigned int  assets_explode1_wav_len;
extern unsigned char assets_explode2_wav[];
extern unsigned int  assets_explode2_wav_len;
extern unsigned char assets_explode3_wav[];
extern unsigned int  assets_explode3_wav_len;

// --- Real things ---
Camera3D camera = {};
f32 camera_sensitivity = 0.05f;
f32 camera_smoothness = 80.0f;
Thing    *player = {};
Textures tex = {};
Models   model = {};
Sounds   sounds = {};
bool     debug_mode = false;

// Buffers
StaticThing static_things[MAX_STATIC_THINGS];
Thing       things[MAX_THINGS];
Spark       sparks[MAX_SPARKS];
u32         static_things_count = 1; // We treat 0 as IDX_NIL
u32         things_count = 1;        // We treat 0 as IDX_NIL
u32         next_spark;

// Portals
u32 portal_idx_a = IDX_NIL;
u32 portal_idx_b = IDX_NIL;
u32 portal_projectile_a = IDX_NIL;
u32 portal_projectile_b = IDX_NIL;

// ======================================= FORWARD DECLARATIONS ================================

Vector3 pos_player_head();
Vector3 pos_player_feet();
Vector3 cube_surface_normal(Vector3 cube_pos, Vector3 cube_size, Vector3 hit_pos);
u32 allocate_thing(ThingType type, Vector3 pos, Vector3 vel, Vector3 siz, Vector3 rot_axis, f32 rot_def, Model *model, Color tint, u32 flags);

// ======================================= HELPERS ================================================

void make_vertex(Mesh *mesh, int *vertex_index, Vector3 pos, Vector3 normal, Vector2 uv) {
    int idx = *vertex_index;

    mesh->vertices[idx*3 + 0] = pos.x;
    mesh->vertices[idx*3 + 1] = pos.y;
    mesh->vertices[idx*3 + 2] = pos.z;

    mesh->normals[idx*3 + 0] = normal.x;
    mesh->normals[idx*3 + 1] = normal.y;
    mesh->normals[idx*3 + 2] = normal.z;

    mesh->texcoords[idx*2 + 0] = uv.x;
    mesh->texcoords[idx*2 + 1] = uv.y;

    *vertex_index = idx + 1;
}

Mesh make_disc(f32 radius) {
    Mesh mesh = { 0 };

    f32 thickness = 50.0f;
    f32 half = thickness * 0.5f;
    int slices = 32;

    int triangleCount = slices * 4;
    int vertexCount = triangleCount * 3;

    mesh.vertexCount = vertexCount;
    mesh.triangleCount = triangleCount;

    mesh.vertices  = (f32 *)calloc(vertexCount*3, sizeof(f32));
    mesh.normals   = (f32 *)calloc(vertexCount*3, sizeof(f32));
    mesh.texcoords = (f32 *)calloc(vertexCount*2, sizeof(f32));

    if (!mesh.vertices || !mesh.normals || !mesh.texcoords) {
        free(mesh.vertices);
        free(mesh.normals);
        free(mesh.texcoords);
        return Mesh{ 0 };
    }

    int v = 0;

    for (int slice = 0; slice < slices; slice++) {
        f32 a0 = ((f32)slice/(f32)slices)*2.0f*PI;
        f32 a1 = ((f32)(slice+1)/(f32)slices)*2.0f*PI;

        f32 x0 = cosf(a0)*radius;
        f32 y0 = sinf(a0)*radius;
        f32 x1 = cosf(a1)*radius;
        f32 y1 = sinf(a1)*radius;

        f32 nx0 = cosf(a0);
        f32 ny0 = sinf(a0);
        f32 nx1 = cosf(a1);
        f32 ny1 = sinf(a1);

        f32 u0 = (f32)slice/(f32)slices;
        f32 u1 = (f32)(slice+1)/(f32)slices;

        // --- Side rim ---
        make_vertex(&mesh, &v, { x0, y0, -half }, { nx0, ny0, 0.0f }, { u0, 1.0f });
        make_vertex(&mesh, &v, { x1, y1,  half }, { nx1, ny1, 0.0f }, { u1, 0.0f });
        make_vertex(&mesh, &v, { x1, y1, -half }, { nx1, ny1, 0.0f }, { u1, 1.0f });

        make_vertex(&mesh, &v, { x0, y0, -half }, { nx0, ny0, 0.0f }, { u0, 1.0f });
        make_vertex(&mesh, &v, { x0, y0,  half }, { nx0, ny0, 0.0f }, { u0, 0.0f });
        make_vertex(&mesh, &v, { x1, y1,  half }, { nx1, ny1, 0.0f }, { u1, 0.0f });

        // --- Front (+Z) ---
        make_vertex(&mesh, &v, { 0.0f, 0.0f,  half }, { 0.0f, 0.0f, 1.0f }, { 0.5f, 0.5f });
        make_vertex(&mesh, &v, { x1, y1,  half }, { 0.0f, 0.0f, 1.0f }, { 0.5f + x1/(2.0f*radius), 0.5f + y1/(2.0f*radius) });
        make_vertex(&mesh, &v, { x0, y0,  half }, { 0.0f, 0.0f, 1.0f }, { 0.5f + x0/(2.0f*radius), 0.5f + y0/(2.0f*radius) });

        // --- Back (-Z) ---
        make_vertex(&mesh, &v, { 0.0f, 0.0f, -half }, { 0.0f, 0.0f, -1.0f }, { 0.5f, 0.5f });
        make_vertex(&mesh, &v, { x0, y0, -half }, { 0.0f, 0.0f, -1.0f }, { 0.5f + x0/(2.0f*radius), 0.5f + y0/(2.0f*radius) });
        make_vertex(&mesh, &v, { x1, y1, -half }, { 0.0f, 0.0f, -1.0f }, { 0.5f + x1/(2.0f*radius), 0.5f + y1/(2.0f*radius) });
    }

    UploadMesh(&mesh, false);
    return mesh;
}

Thing make_thing(ThingType type, Vector3 pos, Vector3 vel, Vector3 siz, Vector3 rot_axis, f32 rot_def, Model *model, Color tint, u32 flags) {
    Thing thing = { type, pos, vel, siz, { 0 }, rot_axis, rot_def,  model, tint, flags };
    
    // Default hitbox is entire thing with not offset
    thing.hitbox_offset = { };
    thing.hitbox_siz = { siz };
    thing.flags |= ThingFlag::Gravity;

    return thing;
}

void make_portal_projectile(u32 *portal_idx, Vector3 look_forward, bool left) {
    Vector3 vel = look_forward * 5000.0f;
    Vector3 pos = pos_player_head();
    Vector3 size = { 5.0f, 5.0f, 5.0f };
    Color color = left ? BLUE : ORANGE;
    u32 flags = ThingFlag::Visible;
    if (left) {
        flags |= ThingFlag::Left;
    }

    // Either we allocate one if it doens't exist or we ovewrite it
    if (*portal_idx == IDX_NIL) {
        *portal_idx = allocate_thing(ThingType::PortalProjectile, pos, vel, size, DEFAULT_ROT_AXIS, 0.0f, &model.portal_sphere, color, flags);
    } else {
        things[*portal_idx] = make_thing(ThingType::PortalProjectile, pos, vel, size, DEFAULT_ROT_AXIS, 0.0f, &model.portal_sphere, color, flags);
    }
}

// Notice: Can fail if there's not enough room
bool make_portal(u32 *portal_idx, StaticThing col_thing, Vector3 hit_pos, Vector3 projectile_vel, bool left) {
    f32 portal_radius   = 75.0f;
    f32 portal_diameter = 2.0f * portal_radius;
    Vector3 vel         = { 0 };
    Vector3 size        = { portal_diameter, portal_diameter, 1.0f };
    Color   color       = left ? BLUE : ORANGE;
    Vector3 disc_normal = { 0.0f, 0.0f, 1.0f };
    u32 flags = ThingFlag::Visible;
    if (left) {
        flags |= ThingFlag::Left;
    }

    // Find surface normal
    Vector3 surface_normal = cube_surface_normal(col_thing.pos, col_thing.siz, hit_pos);
    Vector3 normalized_projectile_vel = Vector3Normalize(projectile_vel);
    if (Vector3DotProduct(surface_normal, normalized_projectile_vel) > 0) {
        surface_normal *= -1.0f;
    }

    // --- BASIS ---
    Vector3 basis_forward = surface_normal;
    // Starting with the assumtion that WORLD_LEFT and basis_forward are not perpendicular
    Vector3 basis_right = Vector3CrossProduct(basis_forward, WORLD_UP);
    Vector3 basis_up    = Vector3CrossProduct(basis_forward, basis_right * -1.0f);
    
    // If WORLD_UP and basis_forward are perpendicular 
    if (fabs(Vector3DotProduct(basis_forward, WORLD_UP)) > 0.9) {
        basis_up    = Vector3CrossProduct(basis_forward, WORLD_LEFT);
        basis_right = Vector3CrossProduct(basis_forward, basis_up);
    }

    // check if radius of portal will be outside of cube from hitpoint
    float half_extent_along_normal =
        fabsf(surface_normal.x) * col_thing.siz.x * 0.5f +
        fabsf(surface_normal.y) * col_thing.siz.y * 0.5f +
        fabsf(surface_normal.z) * col_thing.siz.z * 0.5f;
    float face_half_width =
        fabsf(basis_right.x) * col_thing.siz.x * 0.5f +
        fabsf(basis_right.y) * col_thing.siz.y * 0.5f +
        fabsf(basis_right.z) * col_thing.siz.z * 0.5f;
    float face_half_height =
        fabsf(basis_up.x) * col_thing.siz.x * 0.5f +
        fabsf(basis_up.y) * col_thing.siz.y * 0.5f +
        fabsf(basis_up.z) * col_thing.siz.z * 0.5f;
    Vector3 face_center = col_thing.pos + surface_normal * half_extent_along_normal;
    Vector3 displacement_vec = hit_pos - face_center;
    float point_local_x = Vector3DotProduct(displacement_vec, basis_right);
    float point_local_y = Vector3DotProduct(displacement_vec, basis_up);

    if (fabsf(point_local_x) + portal_radius > face_half_width) return false;
    if (fabsf(point_local_y) + portal_radius > face_half_height) return false;

    // Move portal slightly outward
    // Notice: Since we have a moronic way to find the hit point, this can't be used right now. 
    Vector3 pos = hit_pos + surface_normal * 0.0f; 
    
    // Rotate portal to match surface
    Vector3 rot_axis = Vector3CrossProduct(disc_normal, surface_normal);
    f32 dir_diff     = Clamp(Vector3DotProduct(disc_normal, surface_normal), -1, 1);
    f32 cmp_epsilon  = 0.999f;
    if (dir_diff < -cmp_epsilon || dir_diff > cmp_epsilon) {
        rot_axis = { 0.0f, 1.0f, 0.0f };
    }
    f32 rot_degree = acosf(dir_diff) * RAD2DEG;

    // Either we allocate one if it doesn't exist or we ovewrite it
    if (*portal_idx == IDX_NIL) {
        *portal_idx = allocate_thing(ThingType::Portal, pos, vel, size, rot_axis, rot_degree, &model.portal, color, flags);
    } else {
        things[*portal_idx] = make_thing(ThingType::Portal, pos, vel, size, rot_axis, rot_degree, &model.portal, color, flags);
    }
    // We don't need no gravity for portals
    things[*portal_idx].flags &= ~ThingFlag::Gravity;

    // Calculate hitbox for portal
    Vector3 portal_size = { 0 };
    f32 portal_depth = 25.0f;
    if (surface_normal.x != 0.0f) portal_size = { portal_depth, portal_diameter, portal_diameter };
    if (surface_normal.y != 0.0f) portal_size = { portal_diameter, portal_depth, portal_diameter };
    if (surface_normal.z != 0.0f) portal_size = { portal_diameter, portal_diameter, portal_depth };
    things[*portal_idx].hitbox_siz = portal_size;
    
    // Calculate spawn pos
    f32 portal_half_depth =
    fabsf(surface_normal.x) * portal_size.x * 0.5f +
    fabsf(surface_normal.y) * portal_size.y * 0.5f +
    fabsf(surface_normal.z) * portal_size.z * 0.5f;
    things[*portal_idx].portal_spawn_pos = pos + surface_normal * portal_half_depth;
    things[*portal_idx].hitbox_offset = surface_normal * portal_half_depth;
    things[*portal_idx].dir = surface_normal;

    // Set basis
    things[*portal_idx].basis_forward = basis_forward;
    things[*portal_idx].basis_up = basis_up;
    things[*portal_idx].basis_right = basis_right;

    return true;
}

u32 allocate_static_thing(Vector3 pos, Vector3 siz, Model *model, Vector3 rot_axis, f32 rot_deg, Color tint) {
    if (static_things_count >= MAX_STATIC_THINGS) return IDX_NIL;
    
    Vector3 half_size = { siz.x * 0.5f, siz.y * 0.5f, siz.z * 0.5f };
    AABB aabb = { 
        .min = { pos.x - half_size.x, pos.y - half_size.y, pos.z - half_size.z },
        .max = { pos.x + half_size.x, pos.y + half_size.y, pos.z + half_size.z },
    };

    static_things[static_things_count] = StaticThing{
        aabb,
        pos,
        siz,
        model,
        rot_axis,
        rot_deg,
        tint,
    };

    return static_things_count++;
}

u32 allocate_thing(ThingType type, Vector3 pos, Vector3 vel, Vector3 siz, Vector3 rot_axis, f32 rot_def, Model *model, Color tint, u32 flags) {
    if (things_count >= MAX_THINGS) return IDX_NIL; // It's a bad time if you get this guy

    assert(things_count > 0);
    things[things_count] = make_thing(type, pos, vel, siz, rot_axis, rot_def, model, tint, flags); 

    // TODO do smarter stuff here in the future

    // Index of thing
    return things_count++;
}

void deallocate_thing(Thing *thing) {
    // TODO do more than just set it to zero some day
    *thing = {};
}

void allocate_spark(Vector3 pos, Vector3 vel, f32 life, f32 max_life) {
    if (next_spark > MAX_SPARKS) next_spark = 0;

    sparks[next_spark++] = { 
        pos,
        vel,
        life,
        max_life
    };
}

float rnd_range(float min, float max) {
    float t = (float)rand() / ((float)RAND_MAX + 1.0f);
    return min + t * (max - min);
}

void draw_debug_vec3(Vector3 origin, Vector3 vec, Color color) {
    Vector3 end = Vector3Add(origin, Vector3Scale(vec, 200.0f));
    DrawLine3D(origin, end, color);
}

int comp_face_asc(const void *a, const void *b) {
    GeometricFace a_val = *(const GeometricFace *)a;
    GeometricFace b_val = *(const GeometricFace *)b;

    return (a_val.dist > b_val.dist) - (a_val.dist < b_val.dist);
}

Vector3 pos_player_head() {
    return { player->pos.x, player->pos.y + player->siz.y * 0.5f, player->pos.z };
}

Vector3 pos_player_feet() {
    return { player->pos.x, player->pos.y - player->siz.y * 0.5f, player->pos.z };
}

Vector3 cube_surface_normal(Vector3 cube_pos, Vector3 cube_size, Vector3 hit_pos) {
    // Find surface normal
    // I need to find all the faces of the cube I hit
    // Then I need to find the face that is closest.
    Vector3 col_min = cube_pos - cube_size * 0.5f;
    Vector3 col_max = cube_pos + cube_size * 0.5f;
    GeometricFace faces[6] = {
        { Face::Left,   fabs(col_min.x - hit_pos.x), { -1.0f,  0.0f,  0.0f } }, 
        { Face::Right,  fabs(col_max.x - hit_pos.x), {  1.0f,  0.0f,  0.0f } }, 
        { Face::Bottom, fabs(col_min.y - hit_pos.y), {  0.0f, -1.0f,  0.0f } }, 
        { Face::Top,    fabs(col_max.y - hit_pos.y), {  0.0f,  1.0f,  0.0f } }, 
        { Face::Back,   fabs(col_min.z - hit_pos.z), {  0.0f,  0.0f,  1.0f } }, 
        { Face::Front,  fabs(col_max.z - hit_pos.z), {  0.0f,  0.0f, -1.0f } }, 
    };
    qsort(faces, 6, sizeof(GeometricFace), comp_face_asc);

    return faces[0].vec;
}

bool aabb_intersects(AABB a, AABB b) {
    return a.min.x < b.max.x && a.max.x > b.min.x &&
           a.min.y < b.max.y && a.max.y > b.min.y &&
           a.min.z < b.max.z && a.max.z > b.min.z;
}

void load_texture(unsigned char *arr, u32 len, Texture2D *out) {
    Image img = LoadImageFromMemory(".png", arr, len);
    *out = LoadTextureFromImage(img);
    SetTextureFilter(*out, TEXTURE_FILTER_POINT);
    SetTextureWrap(*out, TEXTURE_WRAP_REPEAT);
}

void load_model(Mesh *mesh, Texture2D *tex, Model *out) {
    *out = LoadModelFromMesh(*mesh);
    if (tex) out->materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = *tex;
}

void load_sound(Sound *out, unsigned char *arr, u32 len) {
    Wave wave = LoadWaveFromMemory(".wav", arr, len);
    assert(wave.frameCount > 0);
    assert(wave.data != NULL);
    *out = LoadSoundFromWave(wave);
    UnloadWave(wave);
    assert(out->frameCount > 0);
}

void play_distant_sound(Sound *sound, Vector3 sound_pos) {
    float dist = Vector3Distance(player->pos, sound_pos);
    float max_dist = 2000.0f;
    float volume = 1.0f - Clamp(dist / max_dist, 0.0f, 1.0f);
    volume *= 0.20f; // cap max volume

    SetSoundVolume(*sound, volume);
    PlaySound(*sound);
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

void hit_list_add(u32 col_idx, HitType hit_type, ThingType col_type, Hit *hit_list_out, u32 *hit_count_out) {
    if (*hit_count_out >= MAX_HIT_COUNT) {
        __debugbreak();
        return;
    }

    hit_list_out[(*hit_count_out)++] = { hit_type, col_type, col_idx,  };
}

void collision_thing_on_landmine(Thing *thing, Thing *landmine) {
    static u32 explode_idx = 0;
    Sound *explode_sounds[] = {
        &sounds.explode_1,
        &sounds.explode_2,
        &sounds.explode_3,
    };
    u32 explode_sounds_len = sizeof(explode_sounds) / sizeof(explode_sounds[0]);

    play_distant_sound(explode_sounds[explode_idx++], landmine->pos);
    if (explode_idx >= explode_sounds_len) explode_idx = 0;
    deallocate_thing(landmine);
}

void collision_thing_on_portal(Thing *thing, u32 portal_idx) {
    if (portal_idx_a == IDX_NIL || portal_idx_b == IDX_NIL) return;

    // Exit portal is always 'the other' portal 
    u32 exit_portal_idx = portal_idx_a;
    if (exit_portal_idx == portal_idx) exit_portal_idx = portal_idx_b; 
    Thing entry_portal = things[portal_idx]; 
    Thing exit_portal = things[exit_portal_idx]; 
    
    f32 thing_half_along_normal =
        fabsf(exit_portal.basis_forward.x) * thing->siz.x * 0.5f +
        fabsf(exit_portal.basis_forward.y) * thing->siz.y * 0.5f +
        fabsf(exit_portal.basis_forward.z) * thing->siz.z * 0.5f;

    f32 portal_half_depth =
        fabsf(exit_portal.basis_forward.x) * exit_portal.hitbox_siz.x * 0.5f +
        fabsf(exit_portal.basis_forward.y) * exit_portal.hitbox_siz.y * 0.5f +
        fabsf(exit_portal.basis_forward.z) * exit_portal.hitbox_siz.z * 0.5f;

    f32 exit_padding = 50.0f;
    
    // Figure out what the new camera target should be using vec3 exit_portal.basis_forward
    f32 yaw_look_rad   = thing->camera_render_yaw   * DEG2RAD;
    f32 pitch_look_rad = thing->camera_render_pitch * DEG2RAD;
    Vector3 look_forward = { 
        sinf(yaw_look_rad) * cosf(pitch_look_rad), 
        sinf(pitch_look_rad), 
        cosf(yaw_look_rad) * cosf(pitch_look_rad),
    };

    // We need to flip forward since the basis of entry portal is toward us
    look_forward *= -1.0f;

    // Find relative looking direction from the portal we entered
    f32 camera_local_entry_forward = Vector3DotProduct(look_forward, entry_portal.basis_forward);
    f32 camera_local_entry_up      = Vector3DotProduct(look_forward, entry_portal.basis_up);
    f32 camera_local_entry_right   = Vector3DotProduct(look_forward, entry_portal.basis_right);
    Vector3 camera_local_exit = 
        exit_portal.basis_forward * camera_local_entry_forward +
        exit_portal.basis_up * camera_local_entry_up +
        exit_portal.basis_right * camera_local_entry_right;
        
    // Find the entry-portal-local components of thing velocity
    Vector3 move_forward = thing->vel * -1.0f;
    f32 thing_local_forward = Vector3DotProduct(move_forward, entry_portal.basis_forward);
    f32 thing_local_up      = Vector3DotProduct(move_forward, entry_portal.basis_up);
    f32 thing_local_right   = Vector3DotProduct(move_forward, entry_portal.basis_right);
        
    // Set new camera direction
    thing->camera_yaw        = atan2f(camera_local_exit.x, camera_local_exit.z) * RAD2DEG;
    thing->camera_render_yaw = thing->camera_yaw;

    // Set new position
    thing->pos = exit_portal.portal_spawn_pos + exit_portal.basis_forward * (portal_half_depth + thing_half_along_normal + exit_padding);

    // Keep the momentum in the portals direction
    thing->vel =
        (exit_portal.basis_right   * thing_local_right) + 
        (exit_portal.basis_up      * thing_local_up) + 
        (exit_portal.basis_forward * thing_local_forward);
}

void collision_portal_projectile_on_static(Thing *projectile, StaticThing *col_thing) {
    AABB projectile_aabb = get_thing_aabb(projectile);
    AABB col_aabb = col_thing->aabb;
    
    // Back it up lorry style
    while(aabb_intersects(projectile_aabb, col_aabb)) {
        projectile_aabb = get_thing_aabb(projectile);
        projectile->pos -= Vector3Normalize(projectile->vel) * 0.001f;
    }

    bool created = false;
    if (projectile->flags & ThingFlag::Left) {
        created = make_portal(&portal_idx_a, *col_thing, projectile->pos, projectile->vel, true);
    } else {
        created = make_portal(&portal_idx_b, *col_thing, projectile->pos, projectile->vel, false);
    }

    if (!created) {
        for (u32 spark_idx = 0; spark_idx < 20; spark_idx++) {
            Vector3 dir = { rnd_range(-1.0f, 1.0f), rnd_range(-1.0f, 1.0f), rnd_range(-1.0f, 1.0f) };
            dir *= 1000.0f; // Gotta go fast
            f32 life = rnd_range(0.08f, 0.15f);
            f32 max_life = life;
            allocate_spark(projectile->pos, dir, life, max_life);
        }
    }

    deallocate_thing(projectile);
}

void sim_type_player(f32 delta) {
    // Update player movement
    Vector2 mouseDelta = GetMouseDelta();
    player->camera_yaw   -= mouseDelta.x * camera_sensitivity;
    player->camera_pitch -= mouseDelta.y * camera_sensitivity;
    if (player->camera_pitch > 89.0f)  player->camera_pitch = 89.0f;
    if (player->camera_pitch < -89.0f) player->camera_pitch = -89.0f;

    f32 smooth = 1.0f - expf(-camera_smoothness * delta);
    player->camera_render_yaw   += (player->camera_yaw   - player->camera_render_yaw)   * smooth;
    player->camera_render_pitch += (player->camera_pitch - player->camera_render_pitch) * smooth;

    // Figure out forward and right direction after converting to rads
    f32 yaw_rad          = player->camera_yaw   * DEG2RAD;
    f32 yaw_look_rad     = player->camera_render_yaw   * DEG2RAD;
    f32 pitch_look_rad   = player->camera_render_pitch * DEG2RAD;
    Vector3 look_forward = { 
        sinf(yaw_look_rad) * cosf(pitch_look_rad), 
        sinf(pitch_look_rad), 
        cosf(yaw_look_rad) * cosf(pitch_look_rad)};
    Vector3 move_forward = { sinf(yaw_rad), 0.0f, cosf(yaw_rad) }; 

    // Handle forward movement attempt
    if (IsKeyDown(KEY_W)) {
        if (fabs(player->vel.x) > 0.001f || fabs(player->vel.z) > 0.001f) {
            f32     turn_speed = PI;
            Vector3 vel_dir    = Vector3Normalize({ player->vel.x,   0.0f, player->vel.z });
            Vector3 target_dir = Vector3Normalize({ move_forward.x, 0.0f, move_forward.z });
            f32     dot        = Clamp(Vector3DotProduct(vel_dir, target_dir), -1.0f, 1.0f);
            Vector3 cross      = Vector3CrossProduct(vel_dir, target_dir);
            f32     angle      = acos(dot);
            f32     sign       = cross.y < 0.0f ? -1.0f : 1.0f;
            float   signed_angle = angle * sign;
            
            f32 applied_turn = Clamp(signed_angle, -turn_speed * delta, +turn_speed * delta);
            player->vel = Vector3RotateByAxisAngle(player->vel, WORLD_UP, applied_turn);
        }

        // Stop skating when max walk speed reached
        if (Vector2Length({player->vel.x, player->vel.z}) < MAX_WALK_SPEED) {
            player->vel += move_forward * WALK_ACCEL * delta;
        }
    } 
    // Handle braking
    else if (IsKeyDown(KEY_S)) {
        float speed = Vector2Length({ player->vel.x, player->vel.z });
        float decel = WALK_ACCEL * delta;

        if (speed <= decel) {
            player->vel.x = 0.0f;
            player->vel.z = 0.0f;
        } else {
            float scale = (speed - decel) / speed;
            player->vel.x *= scale;
            player->vel.z *= scale;
        }
    }

    // Update camera
    camera.position = pos_player_head();
    camera.target = camera.position + look_forward;

    // Did the player shoot?
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))  make_portal_projectile(&portal_projectile_a, look_forward, true);
    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) make_portal_projectile(&portal_projectile_b, look_forward, false);
}

// ======================================= MAIN FUNCS ================================================

void loop_init() {
    SetConfigFlags(FLAG_MSAA_4X_HINT);
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "SNORTAL");
    InitAudioDevice();
    DisableCursor();
    rlSetClipPlanes(10.0f, 10000000.0f);
    srand((unsigned int)time(NULL));

    // Setup camera
    camera.position = { 100.0f, 100.0f, 100.0f };
    camera.target = {50.0f, 50.0f, 25.0f};
    camera.up = { 0.0f, 1.0f, 0.0f };
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    // Load textures
    load_texture(assets_floor_tile_1_png, assets_floor_tile_1_png_len, &tex.floor);
    load_texture(assets_crate_png, assets_crate_png_len, &tex.crate);
    load_texture(assets_wall_1_png, assets_wall_1_png_len, &tex.wall);
    load_texture(assets_landmine_png, assets_landmine_png_len, &tex.landmine);
    tex.full_color = LoadTextureFromImage(GenImageColor(1, 1, WHITE));

    // Load sounds
    load_sound(&sounds.explode_1, assets_explode1_wav, assets_explode1_wav_len);
    load_sound(&sounds.explode_2, assets_explode2_wav, assets_explode2_wav_len);
    load_sound(&sounds.explode_3, assets_explode3_wav, assets_explode3_wav_len);

    // Create player
    {
        Vector3 player_siz = { 25.0f, 100.0f, 25.0f };
        Vector3 player_pos = {-150.0f, player_siz.y, 0.0f };
        u32 player_idx = allocate_thing(
            ThingType::Player, 
            
            player_pos, {}, 
            player_siz, DEFAULT_ROT_AXIS, 
            0.0f, {}, WHITE, 0
        );
        assert(player_idx != IDX_NIL);
        player = &things[player_idx]; // Save reference for convenience
    }

    // Sizes
    f32 floor_size = 8192.0f;
    f32 floor_height = 100.0f;
    f32 crate_size = 4.0f * TILE_SIZE_F;
    f32 landmine_size = TILE_SIZE_F;
    f32 portal_size = TILE_SIZE_F;
    f32 wall_height = 6.0f * TILE_SIZE_F;
    f32 wall_thick = 1.0f * TILE_SIZE_F;

    // Create meshes
    Mesh mesh_cube = GenMeshCube(TILE_SIZE_F, TILE_SIZE_F, TILE_SIZE_F);
    Mesh mesh_sphere = GenMeshSphere(TILE_SIZE_F * 0.5f, 16, 16);
    Mesh mesh_portal = make_disc(2.0f * TILE_SIZE_F);
    Mesh mesh_floor = GenMeshCube(floor_size, floor_height, floor_size);
    Mesh mesh_short_cyl = GenMeshCylinder(90.0f, 10.0f, 32);

    // Texture the floor
    f32 repeats = floor_size / TILE_SIZE_F;
    for (int i = 0; i < mesh_floor.vertexCount; i++) {
        f32 x = mesh_floor.vertices[i * 3 + 0];
        f32 z = mesh_floor.vertices[i * 3 + 2];

        f32 ny = mesh_floor.normals[i * 3 + 1];

        if (ny > 0.9f) {
            mesh_floor.texcoords[i * 2 + 0] = (x / floor_size + 0.5f) * repeats;
            mesh_floor.texcoords[i * 2 + 1] = (z / floor_size + 0.5f) * repeats;
        }
    }

    UpdateMeshBuffer(mesh_floor, 1, mesh_floor.texcoords, mesh_floor.vertexCount * 2 * sizeof(f32), 0);

    load_model(&mesh_floor, &tex.floor, &model.floor);
    load_model(&mesh_cube, &tex.crate, &model.crate);
    load_model(&mesh_cube, &tex.wall, &model.wall);
    load_model(&mesh_sphere, &tex.full_color, &model.portal_sphere);
    load_model(&mesh_portal, &tex.full_color, &model.portal);
    load_model(&mesh_short_cyl, &tex.landmine, &model.landmine);

    // Create floor
    u32 floor_idx = allocate_static_thing(
        { 0.0f, -floor_height * 0.5f, 0.0f },
        { floor_size, floor_height, floor_size },
        &model.floor,
        DEFAULT_ROT_AXIS,
        0.0f,
        WHITE
    );
    assert(floor_idx != IDX_NIL);

    // Create roof
    u32 roof_idx = allocate_static_thing(
        { 0.0f, 4096.0f, 0.0f },
        { floor_size, floor_height, floor_size },
        &model.floor,
        DEFAULT_ROT_AXIS,
        0.0f,
        WHITE
    );
    assert(roof_idx != IDX_NIL);

    struct BlockSpec {
        Vector3 pos;
        Vector3 siz;
        Model* model;
        Color color;
    };

    #define WALL_X(x, z, sx) { { (x), wall_height * 0.5f, (z) }, { (sx), wall_height, wall_thick }, &model.wall, WHITE }
    #define WALL_Z(x, z, sz) { { (x), wall_height * 0.5f, (z) }, { wall_thick, wall_height, (sz) }, &model.wall, WHITE }
    #define CRATE(x, y, z, sx, sy, sz) { { (x), (y), (z) }, { (sx), (sy), (sz) }, &model.crate, WHITE }

    BlockSpec blocks[] = {
        // Outer boundary with large gate openings
        WALL_X(0.0f, -3800.0f, 6200.0f),
        WALL_X(0.0f,  3800.0f, 6200.0f),
        WALL_Z(-3800.0f, 0.0f, 6200.0f),
        WALL_Z( 3800.0f, 0.0f, 6200.0f),

        // Spawn courtyard
        WALL_X(-950.0f, -1450.0f, 1700.0f),
        WALL_Z(-1800.0f, -850.0f, 1200.0f),
        WALL_Z(-100.0f, -850.0f, 1200.0f),
        WALL_X(-950.0f, -250.0f, 900.0f),

        // Long central portal lanes
        WALL_X(0.0f, -900.0f, 2500.0f),
        WALL_X(0.0f,  900.0f, 2500.0f),
        WALL_Z(-1250.0f, 0.0f, 1800.0f),
        WALL_Z( 1250.0f, 0.0f, 1800.0f),

        // Broken maze walls
        WALL_X(-2500.0f, -2000.0f, 1200.0f),
        WALL_X(-2500.0f, -1100.0f, 900.0f),
        WALL_Z(-3100.0f, -1550.0f, 900.0f),
        WALL_Z(-1900.0f, -1550.0f, 900.0f),

        WALL_X(2450.0f, 1800.0f, 1300.0f),
        WALL_X(2450.0f, 2700.0f, 1100.0f),
        WALL_Z(1800.0f, 2250.0f, 900.0f),
        WALL_Z(3100.0f, 2250.0f, 900.0f),

        // Tall portal target slabs / blockers
        WALL_Z(-600.0f, 2400.0f, 1500.0f),
        WALL_Z( 600.0f, 2400.0f, 1500.0f),
        WALL_X(0.0f, 3150.0f, 1300.0f),

        WALL_X(-2400.0f, 1200.0f, 1400.0f),
        WALL_Z(-3100.0f, 1850.0f, 1300.0f),
        WALL_Z(-1700.0f, 1850.0f, 1300.0f),

        WALL_X(2400.0f, -2200.0f, 1400.0f),
        WALL_Z(1700.0f, -2850.0f, 1300.0f),
        WALL_Z(3100.0f, -2850.0f, 1300.0f),


        // Crate clusters: covers, ledges, little puzzle toys
        CRATE(-250.0f, crate_size * 0.5f, -250.0f, crate_size, crate_size, crate_size),
        CRATE( 250.0f, crate_size * 0.5f,  250.0f, crate_size, crate_size, crate_size),

        CRATE(-1450.0f, crate_size * 0.5f, -1450.0f, crate_size, crate_size, crate_size),
        CRATE(-1050.0f, crate_size * 0.5f, -1450.0f, crate_size, crate_size, crate_size),
        CRATE(-650.0f,  crate_size * 0.5f, -1450.0f, crate_size, crate_size, crate_size),

        CRATE(1500.0f, crate_size * 0.5f, -650.0f, crate_size, crate_size, crate_size),
        CRATE(1900.0f, crate_size * 0.5f, -650.0f, crate_size, crate_size, crate_size),
        CRATE(2300.0f, crate_size * 0.5f, -650.0f, crate_size, crate_size, crate_size),

        CRATE(-2200.0f, crate_size * 0.5f, 500.0f, crate_size, crate_size, crate_size),
        CRATE(-2200.0f, crate_size * 1.5f, 500.0f, crate_size, crate_size, crate_size),

        CRATE(700.0f, crate_size * 0.5f, 2100.0f, crate_size, crate_size, crate_size),
        CRATE(1050.0f, crate_size * 0.5f, 2450.0f, crate_size, crate_size, crate_size),
        CRATE(1400.0f, crate_size * 0.5f, 2800.0f, crate_size, crate_size, crate_size),

        CRATE(-2800.0f, crate_size * 0.5f, 2600.0f, crate_size, crate_size, crate_size),
        CRATE(-2400.0f, crate_size * 0.5f, 2600.0f, crate_size, crate_size, crate_size),
        CRATE(-2000.0f, crate_size * 0.5f, 2600.0f, crate_size, crate_size, crate_size),

        CRATE(0.0f, 500.0f, 0.0f, crate_size, crate_size, crate_size),
        CRATE(0.0f, 900.0f, 0.0f, crate_size, crate_size, crate_size),

        // Raised chunky platforms
        CRATE(-3000.0f, 150.0f, -3000.0f, 900.0f, 300.0f, 900.0f),
        CRATE( 3000.0f, 150.0f,  3000.0f, 900.0f, 300.0f, 900.0f),
        CRATE(-3000.0f, 300.0f,  3000.0f, 700.0f, 600.0f, 700.0f),
        CRATE( 3000.0f, 300.0f, -3000.0f, 700.0f, 600.0f, 700.0f),
    };

    for (u32 i = 0; i < sizeof(blocks) / sizeof(blocks[0]); i++) {
        u32 block_idx = allocate_static_thing(
            blocks[i].pos,
            blocks[i].siz,
            blocks[i].model,
            DEFAULT_ROT_AXIS,
            0.0f,
            blocks[i].color
        );
        assert(block_idx != IDX_NIL);
    }

    // Minefield generation (dumb but effective)
    {
        f32 spacing = 400.0f;   // distance between mines
        f32 jitter  = 120.0f;   // randomness so it’s not a grid

        for (f32 x = -floor_size * 0.5f + spacing; x < floor_size * 0.5f; x += spacing) {
            for (f32 z = -floor_size * 0.5f + spacing; z < floor_size * 0.5f; z += spacing) {

                // Skip central spawn-ish area so it's not instant death
                if (fabsf(x) < 600.0f && fabsf(z) < 600.0f) continue;

                // Add some caveman randomness
                f32 rx = x + rnd_range(-jitter, jitter);
                f32 rz = z + rnd_range(-jitter, jitter);

                u32 thing_idx = allocate_thing(
                    ThingType::Landmine, // or your LANDMINE type if you have one
                    { rx, 0.0f, rz },
                    { 0 },
                    { landmine_size, landmine_size * 0.25f, landmine_size },
                    DEFAULT_ROT_AXIS,
                    0.0f,
                    &model.landmine,   // assuming you added this
                    WHITE,
                    ThingFlag::Visible
                );
                things[thing_idx].hitbox_offset = { 0.0f, landmine_size * 0.25f * 0.5f, 0.0f };
                assert(thing_idx != IDX_NIL);
            }
        }
    }

    #undef WALL_X
    #undef WALL_Z
    #undef CRATE
}

void loop_sim(f32 delta) {
    #ifdef _DEBUG
    ZoneScoped;
    #endif

    // (Optionally) Enable debug
    if (IsKeyPressed(KEY_F5)) {
        debug_mode = !debug_mode;
    }

    // --- SIM THINGS ---
    for (u32 idx = 1; idx < things_count; idx++) {
        Thing *thing = &things[idx];
        if (thing->type == ThingType::Nil) continue;

        // Create temprorary hit list
        // TODO Wait a little bit with deleting this, but most likely not relevant
        Hit hit_list[MAX_HIT_COUNT];
        u32 hit_list_count = 0;
        
        // --- Handle whatever is unique ---
        switch(thing->type) {
            case ThingType::Player: {
                sim_type_player(delta);
                
                // Handle jump input
                if ((thing->flags & ThingFlag::Grounded) && IsKeyPressed(KEY_SPACE)) {
                    thing->vel.y = JUMP_SPEED;
                }

                break;
            }
        }

        // --- Continue handling everything that's common ---
        
        // Apply gravity
        thing->vel.y -= GRAVITY_ACCEL * delta;
 
        // Resolve all velocities into correct positions
        {
            #ifdef _DEBUG
            ZoneScopedN("resolve_against_static");
            #endif
            
            // RESOLVE X AXIS
            thing->pos.x += thing->vel.x * delta;
            for (u32 col_idx = 1; col_idx < static_things_count; col_idx++) {
                AABB thing_aabb = get_thing_aabb(thing);
                AABB col_aabb = static_things[col_idx].aabb;

                if (aabb_intersects(thing_aabb, col_aabb)) {
                    if (thing->type == ThingType::PortalProjectile) {
                        collision_portal_projectile_on_static(thing, &static_things[col_idx]);
                        break;
                    } else {
                        hit_list_add(col_idx, HitType::StaticThing, ThingType::Nil, hit_list, &hit_list_count);
                    }
                   
                    if (thing->vel.x > 0.0f) {
                        float penetration = thing_aabb.max.x - col_aabb.min.x;
                        thing->pos.x -= penetration;
                    } else if (thing->vel.x < 0.0f) {
                        float penetration = col_aabb.max.x - thing_aabb.min.x;
                        thing->pos.x += penetration;
                    }

                    thing->vel.x = 0.0f;
                    break;
                }
            }
            
            // RESOLVE Z AXIS
            thing->pos.z += thing->vel.z * delta;
            for (u32 col_idx = 1; col_idx < static_things_count; col_idx++) {

                AABB thing_aabb = get_thing_aabb(thing);
                AABB col_aabb = static_things[col_idx].aabb;

                if (aabb_intersects(thing_aabb, col_aabb)) {
                    if (thing->type == ThingType::PortalProjectile) {
                        collision_portal_projectile_on_static(thing, &static_things[col_idx]);
                        break;
                    } else {
                        hit_list_add(col_idx, HitType::StaticThing, ThingType::Nil, hit_list, &hit_list_count);
                    }

                    if (thing->vel.z > 0.0f) {
                        float penetration = thing_aabb.max.z - col_aabb.min.z;
                        thing->pos.z -= penetration;
                    } else if (thing->vel.z < 0.0f) {
                        float penetration = col_aabb.max.z - thing_aabb.min.z;
                        thing->pos.z += penetration;
                    }

                    thing->vel.z = 0.0f;
                    break;
                }
            }
            
            // RESOLVE Y AXIS
            if (thing->flags & ThingFlag::Gravity) {
                thing->pos.y += thing->vel.y * delta;
                for (u32 col_idx = 1; col_idx < static_things_count; col_idx++) {
                    AABB thing_aabb = get_thing_aabb(thing);
                    AABB col_aabb = static_things[col_idx].aabb;

                    if (aabb_intersects(thing_aabb, col_aabb)) {
                        if (thing->type == ThingType::PortalProjectile) {
                            collision_portal_projectile_on_static(thing, &static_things[col_idx]);
                            break;
                        } else {
                            hit_list_add(col_idx, HitType::StaticThing, ThingType::Nil, hit_list, &hit_list_count);
                        }

                        if (thing->vel.y > 0.0f) {
                            // hit ceiling
                            float penetration = thing_aabb.max.y - col_aabb.min.y;
                            thing->pos.y -= penetration;
                        } else if (thing->vel.y < 0.0f) {
                            // landed on floor
                            float penetration = col_aabb.max.y - thing_aabb.min.y;
                            thing->pos.y += penetration;
                            thing->flags |= ThingFlag::Grounded;
                        }

                        thing->vel.y = 0.0f;
                        break;
                    }

                    thing->flags &= ~ThingFlag::Grounded;
                }
            }
        }

        AABB thing_aabb = get_thing_aabb(thing);
        for (u32 col_idx = 1; col_idx < things_count; col_idx++) {
            Thing *col_thing = &things[col_idx];
            if (col_idx == idx) continue;
            if (col_thing->type == ThingType::Nil) continue;
            AABB col_aabb = get_thing_aabb(col_thing);

            if (aabb_intersects(thing_aabb, col_aabb)) {
                switch (col_thing->type) {
                    case ThingType::Portal: {
                        collision_thing_on_portal(thing, col_idx);
                        break;
                    }
                    case ThingType::Landmine: {
                        // For now the only thing that triggers a landmine should be another player
                        if (thing->type != ThingType::Player) break;

                        collision_thing_on_landmine(thing, col_thing);
                        break;
                    }
                }
            }
        }
    }

    // --- SIM SPARKS ---
    for (u32 idx = 0; idx < MAX_SPARKS; idx++) {
        #ifdef _DEBUG
        ZoneScopedN("sim_sparks");
        #endif
        Spark *spark = &sparks[idx];
        if (spark->life < 0.0f) continue;
        spark->life -= delta;
        spark->pos += spark->vel * delta;
    }
}

void loop_draw() {
    #ifdef _DEBUG
    ZoneScoped;
    #endif
    
    BeginDrawing();
    ClearBackground(WHITE);
    BeginMode3D(camera);

    // --- RENDER THINGS ---
    for (u32 idx = 1; idx < things_count; idx++) {
        Thing *thing = &things[idx];

        // Skip invisible shit
        if ((thing->flags & ThingFlag::Visible) == 0) continue;

        Mesh mesh = thing->model->meshes[0];

        Vector3 scale = {1.0f, 1.0f, 1.0f};
        if (mesh.vertexCount > 0) {

            Vector3 min = { mesh.vertices[0], mesh.vertices[1], mesh.vertices[2] };
            Vector3 max = min;

            for (int i = 0; i < mesh.vertexCount; i++) {
                f32 x = mesh.vertices[i * 3 + 0];
                f32 y = mesh.vertices[i * 3 + 1];
                f32 z = mesh.vertices[i * 3 + 2];
                
                if (x < min.x) min.x = x;
                if (y < min.y) min.y = y;
                if (z < min.z) min.z = z;
                
                if (x > max.x) max.x = x;
                if (y > max.y) max.y = y;
                if (z > max.z) max.z = z;
            }
            Vector3 base_size = { max.x - min.x, max.y - min.y, max.z - min.z };
            
            scale = {
                base_size.x <= 0.0f ?  1.0f : thing->siz.x / base_size.x,
                base_size.y <= 0.0f ? 1.0f : thing->siz.y / base_size.y,
                base_size.z <= 0.0f ? 1.0f : thing->siz.z / base_size.z,
            };
        }

        DrawModelEx(*thing->model, thing->pos, thing->rot_axis, thing->rot_deg, scale, thing->tint);
        
        // Debug draw for things
        if (debug_mode) {
            // Portal basis
            draw_debug_vec3(thing->pos, thing->basis_right,   RED);
            draw_debug_vec3(thing->pos, thing->basis_up,      GREEN);
            draw_debug_vec3(thing->pos, thing->basis_forward, BLUE);

            // General hitbox
            AABB aabb = get_thing_aabb(thing);
            Vector3 center = {
                (aabb.min.x + aabb.max.x) * 0.5f,
                (aabb.min.y + aabb.max.y) * 0.5f,
                (aabb.min.z + aabb.max.z) * 0.5f,
            };
            DrawCubeWires(center, aabb.max.x - aabb.min.x, aabb.max.y - aabb.min.y, aabb.max.z - aabb.min.z, RED);
        }
    }

    // --- RENDER STATIC THINGS ---
    for (u32 idx = 1; idx < static_things_count; idx++) {
        StaticThing *static_thing = &static_things[idx];

        Mesh mesh = static_thing->model->meshes[0];
        Vector3 scale = {1.0f, 1.0f, 1.0f};
        if (mesh.vertexCount > 0) {

            Vector3 min = { mesh.vertices[0], mesh.vertices[1], mesh.vertices[2] };
            Vector3 max = min;

            for (int i = 0; i < mesh.vertexCount; i++) {
                f32 x = mesh.vertices[i * 3 + 0];
                f32 y = mesh.vertices[i * 3 + 1];
                f32 z = mesh.vertices[i * 3 + 2];
                
                if (x < min.x) min.x = x;
                if (y < min.y) min.y = y;
                if (z < min.z) min.z = z;
                
                if (x > max.x) max.x = x;
                if (y > max.y) max.y = y;
                if (z > max.z) max.z = z;
            }
            Vector3 base_size = { max.x - min.x, max.y - min.y, max.z - min.z };
            
            scale = {
                base_size.x <= 0.0f ?  1.0f : static_thing->siz.x / base_size.x,
                base_size.y <= 0.0f ? 1.0f : static_thing->siz.y / base_size.y,
                base_size.z <= 0.0f ? 1.0f : static_thing->siz.z / base_size.z,
            };
        }

        DrawModelEx(*static_thing->model, static_thing->pos, static_thing->rot_axis, static_thing->rot_deg, scale, static_thing->tint);
        
        // Debug draw for things
        if (debug_mode) {
            // General hitbox
            Vector3 center = {
                (static_thing->aabb.min.x + static_thing->aabb.max.x) * 0.5f,
                (static_thing->aabb.min.y + static_thing->aabb.max.y) * 0.5f,
                (static_thing->aabb.min.z + static_thing->aabb.max.z) * 0.5f,
            };
            DrawCubeWires(
                center, 
                static_thing->aabb.max.x - static_thing->aabb.min.x, 
                static_thing->aabb.max.y - static_thing->aabb.min.y, 
                static_thing->aabb.max.z - static_thing->aabb.min.z, RED);
        }
    }

    if (debug_mode) {
        draw_debug_vec3(pos_player_feet(), WORLD_RIGHT,   RED);
        draw_debug_vec3(pos_player_feet(), WORLD_UP,      GREEN);
        draw_debug_vec3(pos_player_feet(), WORLD_FORWARD, BLUE);
    }

    // --- RENDER SPARKS ---
    for (u32 idx = 0; idx < MAX_SPARKS; idx++) {
        Spark *spark = &sparks[idx];
        if (spark->life < 0.0f) continue;
        Vector3 tail = spark->pos - Vector3Normalize(spark->vel) * 12.0f;
        DrawLine3D(tail, spark->pos, ORANGE);
    }

    EndMode3D();

    // DRAW SPEEDOMETER
    {
        Color bg_color = { 0, 0, 0, 128 };
        Color tick_color = { 235, 235, 235, 255 };
        Color needle_color = { 235, 95, 45, 255 };
        Color needle_shadow = { 90, 40, 30, 180 };
        Color hub_outer = { 45, 45, 50, 255 };
        Color hub_inner = { 235, 95, 45, 255 };

        f32 speedo_radius = 120.0f;
        Vector2 speedo_c = { 140.0f, SCREEN_HEIGHT - (speedo_radius * 0.8f)};
        f32 speedo_radius_small = speedo_radius * 0.9;
        f32 speedo_arc = 275.0f * DEG2RAD;
        f32 half_arc = speedo_arc * 0.5f;
        DrawCircle(speedo_c.x, speedo_c.y, speedo_radius, bg_color);
        DrawCircleLinesV(speedo_c, speedo_radius * 0.72f, { 70, 70, 75, 255 });

        // DRAW TICKS
        u32 ticks = 20;
        for (u32 i = 0; i <= ticks; i++) {
            f32 t = (f32)i / (f32)ticks; // 0 → 1
            f32 angle = -half_arc + t * speedo_arc; // centered around Y-axis

            // shift so 0 is straight up (negative Y direction)
            angle -= PI * 0.5f;

            Color tick_color_inner = i > ticks * 0.8f ? Color{ 235, 95, 45, 255 } : tick_color;
            f32 tick_width = i > ticks * 0.8f ? 5.0f : (i % 2 == 0 ? 4.0f : 2.0f);

            DrawLineEx(
                { speedo_c.x + f32(speedo_radius * cos(angle)), speedo_c.y + f32(speedo_radius * sin(angle)) },
                { speedo_c.x + f32(speedo_radius_small * cos(angle)), speedo_c.y + f32(speedo_radius_small * sin(angle)) },
                tick_width,
                tick_color_inner
            );
        }        
        // DRAW NEEDLE
        f32 speed_ratio = Vector3Length(player->vel) / MAX_WALK_SPEED;
        speed_ratio = Clamp(speed_ratio, 0.0f, 1.0f);

        // map 0 → left, 1 → right
        f32 needle_angle = -half_arc + speed_ratio * speedo_arc;

        // align with vertical
        needle_angle -= PI * 0.5f;

        // shake at max
        if (speed_ratio > 0.99f) {
            needle_angle += rnd_range(-0.1f, 0.1f);
        }
        Vector2 needle_end_pos = { 
            speedo_c.x + f32(speedo_radius * cos(needle_angle)), 
            speedo_c.y + f32(speedo_radius * sin(needle_angle))
        };

        DrawLineEx(
            { speedo_c.x + 3.0f, speedo_c.y + 3.0f },
            { needle_end_pos.x + 3.0f, needle_end_pos.y + 3.0f },
            7.0f,
            needle_shadow
        );

        DrawLineEx(speedo_c, needle_end_pos, 5.0f, needle_color);

        DrawCircleV(speedo_c, 13.0f, hub_outer);
        DrawCircleV(speedo_c, 7.0f, hub_inner);    
    }
    
    // DRAW CROSSHAIR
    f32 line_len = 10.0f;
    DrawLine(
        (int)floor(SCREEN_WIDTH * 0.5f - line_len * 0.5f), 
        (int)floor(SCREEN_HEIGHT * 0.5f),
        (int)floor(SCREEN_WIDTH * 0.5f + line_len * 0.5f), 
        (int)floor(SCREEN_HEIGHT * 0.5f),
        BLACK
    );
    DrawLine(
        (int)floor(SCREEN_WIDTH * 0.5f), 
        (int)floor(SCREEN_HEIGHT * 0.5f - line_len * 0.5f),
        (int)floor(SCREEN_WIDTH * 0.5f), 
        (int)floor(SCREEN_HEIGHT * 0.5f + line_len * 0.5f),
        BLACK
    );

    if (debug_mode) {
        char buf[64];
        snprintf(buf, 64, "%+7.0f,%+7.0f,%+7.0f", player->pos.x, player->pos.y, player->pos.z);
        DrawRectangle(20, 20, MeasureText(buf, 40) + 10, 40 + 10, { 0, 0, 0, 200 });
        DrawText(buf, 25, 25, 40, WHITE);
    }

    EndDrawing();
}

int main() {
    loop_init();
    while(!WindowShouldClose()) {
        f32 delta = Clamp(GetFrameTime(), 0.0f, 0.33f);

        loop_sim(delta);
        loop_draw();

        #ifdef _DEBUG
        FrameMark;
        #endif
    }
}