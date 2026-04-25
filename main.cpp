#include <stdio.h>
#include <math.h>
#include <cstdint>
#include "raylib.h"

using u8  = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;

using i8  = int8_t;
using i16 = int16_t;
using i32 = int32_t;
using i64 = int64_t;

using f32 = float;
using f64 = double;

inline Vector3 operator+(Vector3 a,f32 b){return{a.x+b,a.y+b,a.z+b};}
inline Vector3 operator+(Vector3 a,Vector3 b){return{a.x+b.x,a.y+b.y,a.z+b.z};}
inline Vector3 operator-(Vector3 a,Vector3 b){return{a.x-b.x,a.y-b.y,a.z-b.z};}
inline Vector3 operator-(Vector3 v){return{-v.x,-v.y,-v.z};}
inline Vector3 operator*(Vector3 v,f32 s){return{v.x*s,v.y*s,v.z*s};}
inline Vector3 operator*(f32 s,Vector3 v){return{v.x*s,v.y*s,v.z*s};}
inline Vector3 operator*(Vector3 a,Vector3 b){return{a.x*b.x,a.y*b.y,a.z*b.z};}
inline Vector3 operator/(Vector3 v,f32 s){return{v.x/s,v.y/s,v.z/s};}
inline Vector3& operator+=(Vector3& a,Vector3 b){a.x+=b.x;a.y+=b.y;a.z+=b.z;return a;}
inline Vector3& operator-=(Vector3& a,Vector3 b){a.x-=b.x;a.y-=b.y;a.z-=b.z;return a;}
inline Vector3& operator*=(Vector3& v,f32 s){v.x*=s;v.y*=s;v.z*=s;return v;}
inline Vector3& operator/=(Vector3& v,f32 s){v.x/=s;v.y/=s;v.z/=s;return v;}

// ======================================= CONSTS ==============================================

#define SCREEN_WIDTH  1600
#define SCREEN_HEIGHT 900
#define TILE_SIZE_F   64.0f
#define MAX_THINGS    1024

// ======================================= DATASTRUCTURES ======================================
struct Player {
    Vector3 pos;
    Vector3 vel;
    f32   camera_sensitivity;
    f32   camera_smoothness;
    f32   camera_yaw;         // Turning left/right around y axis
    f32   camera_pitch;       // Turning up/down around x axis
    f32   camera_render_yaw;
    f32   camera_render_pitch;
};

struct Textures {
    Texture2D floor;
    Texture2D crate;
};

struct Models {
    Model floor;
    Model crate;
};

enum class BodyType {
    Nil,
    Static,
    Dynamic,
};

enum class ThingFlag : u32 {
    Nil     = 1 << 0,
    Gravity = 1 << 1,
};

struct Thing {
    BodyType  body_type;
    Vector3   pos;
    Vector3   vel;
    Vector3   siz;
    Model     *model;
    ThingFlag flags;
};

// ======================================= STATE ===============================================

Camera3D camera = { 0 };
Player   player = { 0 };
Textures tex = { 0 };
Models   model = { 0 };
Thing    things[MAX_THINGS];
u32      things_count;

// ======================================= FUNCS ================================================

u32 allocate_thing(BodyType body_type, Vector3 pos, Vector3 vel, Vector3 siz, Model *model, ThingFlag flags) {
    things[things_count++] = {
        body_type,
        pos,
        vel,
        siz,
        model,
        flags
    };

    return things_count;
}

void load_texture(const char *path, Texture2D *out) {
    *out = LoadTexture(path);
    SetTextureFilter(*out, TEXTURE_FILTER_POINT);
    SetTextureWrap(*out, TEXTURE_WRAP_REPEAT);
}

void load_model(Mesh *mesh, Texture2D *tex, Model *out) {
    *out = LoadModelFromMesh(*mesh);
    out->materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = *tex;
}

void init() {
    SetConfigFlags(FLAG_MSAA_4X_HINT);
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "SNORTAL");
    DisableCursor();

    camera.position = { 100.0f, 100.0f, 100.0f };
    camera.target = {50.0f, 50.0f, 25.0f};
    camera.up = { 0.0f, 1.0f, 0.0f };
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    player.pos = {-100.0f, 0.0f, -100.0f };
    player.vel = { 150.0f, 100.0f, 150.0f };
    player.camera_sensitivity = 0.05f;
    player.camera_smoothness = 80.0f;

    // Load textures
    load_texture("assets/floor_tile_1.png", &tex.floor);
    load_texture("assets/crate.png", &tex.crate);

    Mesh mesh_cube = GenMeshCube(TILE_SIZE_F, TILE_SIZE_F, TILE_SIZE_F);
   
    // Create floor model
    f32 floor_size = 2048.0f;
    f32 repeats = floor_size / TILE_SIZE_F;
    Mesh mesh_floor = GenMeshPlane(floor_size, floor_size, 1, 1);
    for (int i = 0; i < mesh_floor.vertexCount * 2; i++) mesh_floor.texcoords[i] *= repeats;
    UpdateMeshBuffer(mesh_floor, 1, mesh_floor.texcoords, mesh_floor.vertexCount * 2 * sizeof(f32), 0);

    load_model(&mesh_floor, &tex.floor, &model.floor);
    load_model(&mesh_cube, &tex.crate, &model.crate);

    // Create things
    f32 crate_size = 4.0f * TILE_SIZE_F;

    allocate_thing(BodyType::Static, { 0 }, { 0 }, { floor_size, 1.0f, floor_size }, &model.floor, ThingFlag::Gravity);
    allocate_thing(BodyType::Static, { 250.0f, crate_size * 0.5f, 250.0f }, { 0 }, {crate_size, crate_size, crate_size}, &model.crate, ThingFlag::Nil);

    allocate_thing(BodyType::Static, { -250.0f, crate_size * 0.5f, -250.0f }, { 0 }, {crate_size, crate_size, crate_size}, &model.crate, ThingFlag::Nil);
}

void loop_sim() {
    f32 delta = GetFrameTime();

    // Update player movement
    f32 x_dir = 0.0f;
    f32 z_dir = 0.0f;
    if (IsKeyDown(KEY_W)) z_dir =  1.0f; 
    if (IsKeyDown(KEY_A)) x_dir = -1.0f;
    if (IsKeyDown(KEY_S)) z_dir = -1.0f;
    if (IsKeyDown(KEY_D)) x_dir =  1.0f;
    
    Vector2 mouseDelta = GetMouseDelta();
    player.camera_yaw   -= mouseDelta.x * player.camera_sensitivity;
    player.camera_pitch -= mouseDelta.y * player.camera_sensitivity;
    if (player.camera_pitch > 89.0f)  player.camera_pitch = 89.0f;
    if (player.camera_pitch < -89.0f) player.camera_pitch = -89.0f;

    f32 smooth = 1.0f - expf(-player.camera_smoothness * delta);
    player.camera_render_yaw   += (player.camera_yaw   - player.camera_render_yaw)   * smooth;
    player.camera_render_pitch += (player.camera_pitch - player.camera_render_pitch) * smooth;

    // Figure out forward and right direction after converting to rads
    f32 yaw_rad        = player.camera_yaw   * DEG2RAD;
    f32 pitch_rad      = player.camera_pitch * DEG2RAD;
    f32 yaw_look_rad   = player.camera_render_yaw   * DEG2RAD;
    f32 pitch_look_rad = player.camera_render_pitch * DEG2RAD;
    Vector3 look_forward = { sinf(yaw_look_rad)*cosf(pitch_look_rad), sinf(pitch_look_rad), cosf(yaw_look_rad)*cosf(pitch_look_rad)};
    Vector3 move_forward = { sinf(yaw_rad), 0.0f, cosf(yaw_rad) }; 
    Vector3 right        = { -cosf(yaw_rad), 0.0f, sinf(yaw_rad) };

    Vector3 old_pos = player.pos;
    player.pos += move_forward * (z_dir * player.vel.z * delta);
    player.pos += right *        (x_dir * player.vel.x * delta);

    // Update camera
    camera.position = player.pos;
    camera.position.y = player.pos.y + 50.0f;
    camera.target = camera.position + look_forward;
}

void loop_draw() {
    BeginDrawing();
    ClearBackground(WHITE);
    BeginMode3D(camera);

    // Render all things
    for (u32 idx = 0; idx < things_count; idx++) {
        Thing *thing = &things[idx];
        Mesh mesh = thing->model->meshes[0];
        Vector3 min = { mesh.vertices[0], mesh.vertices[1], mesh.vertices[2] };
        Vector3 max = min;

        for (int i = 0; i < mesh.vertexCount; i++) {
            float x = mesh.vertices[i * 3 + 0];
            float y = mesh.vertices[i * 3 + 1];
            float z = mesh.vertices[i * 3 + 2];

            if (x < min.x) min.x = x;
            if (y < min.y) min.y = y;
            if (z < min.z) min.z = z;

            if (x > max.x) max.x = x;
            if (y > max.y) max.y = y;
            if (z > max.z) max.z = z;
        }
        Vector3 base_size = { max.x - min.x, max.y - min.y, max.z - min.z };

        Vector3 scale = {
            base_size.x <= 0.0f ? 1.0f : thing->siz.x / base_size.x,
            base_size.y <= 0.0f ? 1.0f : thing->siz.y / base_size.y,
            base_size.z <= 0.0f ? 1.0f : thing->siz.z / base_size.z,
        };

        DrawModelEx(*thing->model, thing->pos, {0}, {0}, scale, WHITE);
    }

    EndMode3D();
    EndDrawing();
}

int main() {
    init();
    while(!WindowShouldClose()) {
        loop_sim();
        loop_draw();
    }
}