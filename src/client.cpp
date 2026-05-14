#include <stdio.h>
#include <math.h>
#include <cstdint>
#include <assert.h>
#include <stdlib.h>
#include <ctime>
#include "base.h"
#include "rlgl.h"
#include "net.h"

// --- PROFILING ---
#ifdef _DEBUG
#include "tracy/Tracy.hpp"
#endif

/*
--- CHEAT SHEET ---
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
*/

// ======================================= DATASTRUCTURES ======================================

struct Spark {
    Vector3 pos;
    Vector3 vel;
    f32 life;
    f32 max_life;
};

struct ThingList {
    Thing  buf[MAX_THINGS];
    u32    count;
};

// ======================================= STATE ===============================================

// --- Timers ---
#define KEEPALIVE_INTERVAL 2.0f
f32 net_keepalive_timer = KEEPALIVE_INTERVAL;

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
f32 camera_render_yaw;
f32 camera_render_pitch;
f32 camera_sensitivity = 0.05f;
f32 camera_smoothness = 80.0f;

// Network
NetAddress net_server;
NetSocket net_socket;
char net_buffer[MAX_NET_BUFFER_BYTES];

ClientState client = {};
u32         btn_state;
Texture2D   textures[Texture_COUNT];
Model       models[Model_COUNT];
Sound       sounds[Sound_COUNT];
u32         explode_idx;
bool        debug_mode = false;

// Buffers
StaticThing static_things[MAX_STATIC_THINGS];
ThingList   things_a = { .count = 1 };
ThingList   things_b = { .count = 1 };
u32         things_count_a = 1;        // We treat 0 as IDX_NIL
u32         things_count_b = 1;        // We treat 0 as IDX_NIL
ThingList   thing_buffers[] = {things_a, things_b};
u32         thing_buffers_idx = 0;
Spark       sparks[MAX_SPARKS];
u32         static_things_count = 1; // We treat 0 as IDX_NIL
u32         next_spark;

// Portals
u32 portal_idx_a = IDX_NIL;
u32 portal_idx_b = IDX_NIL;
u32 portal_projectile_a = IDX_NIL;
u32 portal_projectile_b = IDX_NIL;

// Explosions
u32 explode_sounds[] = { Sound_Explode1, Sound_Explode2, Sound_Explode3 };
u32 explode_sounds_len = sizeof(explode_sounds) / sizeof(u32);

// ======================================= FORWARD DECLARATIONS ================================

Vector3 cube_surface_normal(Vector3 cube_pos, Vector3 cube_size, Vector3 hit_pos);

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

void assert_static_thing_before_draw(Model *model, StaticThing *static_thing, Vector3 scale) {
    assert(model != 0);
        assert(static_thing != 0);
        assert(model->meshCount > 0);
        assert(model->meshes != 0);
        assert(model->materialCount > 0);
        assert(model->materials != 0);
        assert(model->meshMaterial != 0);
        assert(isfinite(static_thing->pos.x));
        assert(isfinite(static_thing->pos.y));
        assert(isfinite(static_thing->pos.z));
        assert(isfinite(static_thing->rot_axis.x));
        assert(isfinite(static_thing->rot_axis.y));
        assert(isfinite(static_thing->rot_axis.z));
        assert(isfinite(static_thing->rot_deg));
        assert(isfinite(scale.x));
        assert(isfinite(scale.y));
        assert(isfinite(scale.z));
        assert(scale.x != 0.0f);
        assert(scale.y != 0.0f);
        assert(scale.z != 0.0f);
        float axis_len_sq =
            static_thing->rot_axis.x * static_thing->rot_axis.x +
            static_thing->rot_axis.y * static_thing->rot_axis.y +
            static_thing->rot_axis.z * static_thing->rot_axis.z;
        assert(axis_len_sq > 0.000001f);
}

Thing *get_things() {
    // We can't do much more than return nullptr if the buffer_idx is corrupted
    if (thing_buffers_idx > 1 || thing_buffers_idx < 0) return nullptr;

    return thing_buffers[thing_buffers_idx].buf;
}

Thing *get_prev_things() {
    u32 old_thing_idx = thing_buffers_idx ^ 1;
    // We can't do much more than return nullptr if the buffer_idx is corrupted
    if (old_thing_idx > 1 || old_thing_idx < 0) return nullptr;

    return thing_buffers[old_thing_idx].buf;
}

u32 get_things_count() {
    if (thing_buffers_idx > 1 || thing_buffers_idx < 0) return IDX_NIL;

    return thing_buffers[thing_buffers_idx].count;
}

u32 get_prev_things_count() {
    u32 old_thing_idx = thing_buffers_idx ^ 1;
    if (old_thing_idx > 1 || old_thing_idx < 0) return IDX_NIL;

    return thing_buffers[old_thing_idx].count;
}

void set_things_count(u32 count) {
    if (thing_buffers_idx > 1 || thing_buffers_idx < 0) return;

    thing_buffers[thing_buffers_idx].count = count;
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

void draw_debug_vec3(Vector3 origin, Vector3 vec, Color color) {
    Vector3 end = Vector3Add(origin, Vector3Scale(vec, 200.0f));
    DrawLine3D(origin, end, color);
}

void load_texture(unsigned char *arr, u32 len, u32 idx, Image *img) {
    if (img != nullptr) {
        textures[idx] = LoadTextureFromImage(*img);
    } else {
        Image img_from_memory = LoadImageFromMemory(".png", arr, len);
        textures[idx] = LoadTextureFromImage(img_from_memory);
    }

    SetTextureFilter(textures[idx], TEXTURE_FILTER_POINT);
    SetTextureWrap(textures[idx], TEXTURE_WRAP_REPEAT);
}

void load_model(Mesh *mesh, Texture2D *tex, u32 idx) {
    models[idx] = LoadModelFromMesh(*mesh);
    if (tex) models[idx].materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = *tex;
}

void load_sound(unsigned char *arr, u32 len, u32 idx) {
    Wave wave = LoadWaveFromMemory(".wav", arr, len);
    assert(wave.frameCount > 0);
    assert(wave.data != NULL);
    sounds[idx] = LoadSoundFromWave(wave);
    UnloadWave(wave);
    assert(sounds[idx].frameCount > 0);
}

void play_distant_sound(Sound *sound, Vector3 sound_pos) {
    if (client.player_idx == IDX_NIL) return;
    Thing *player = &get_things()[client.player_idx];

    float dist = Vector3Distance(player->pos, sound_pos);
    float max_dist = 2000.0f;
    float volume = 1.0f - Clamp(dist / max_dist, 0.0f, 1.0f);
    volume *= 0.20f; // cap max volume

    SetSoundVolume(*sound, volume);
    PlaySound(*sound);
}

void sim_own_player(f32 delta) {
    if (client.status != ClientStatus::Live) return;
    if (client.player_idx == IDX_NIL) return;
    Thing *player = &get_things()[client.player_idx];

    // Update player movement
    Vector2 mouseDelta = GetMouseDelta();
    client.camera_yaw   -= mouseDelta.x * camera_sensitivity;
    client.camera_pitch -= mouseDelta.y * camera_sensitivity;
    if (client.camera_pitch > 89.0f)  client.camera_pitch = 89.0f;
    if (client.camera_pitch < -89.0f) client.camera_pitch = -89.0f;

    f32 smooth = 1.0f - expf(-camera_smoothness * delta);
    camera_render_yaw   += (client.camera_yaw   - camera_render_yaw)   * smooth;
    camera_render_pitch += (client.camera_pitch - camera_render_pitch) * smooth;

    // Figure out forward and right direction after converting to rads
    f32 yaw_rad          = client.camera_yaw   * DEG2RAD;
    f32 yaw_look_rad     = camera_render_yaw   * DEG2RAD;
    f32 pitch_look_rad   = camera_render_pitch * DEG2RAD;
    Vector3 look_forward = { 
        sinf(yaw_look_rad) * cosf(pitch_look_rad), 
        sinf(pitch_look_rad), 
        cosf(yaw_look_rad) * cosf(pitch_look_rad)};
    Vector3 move_forward = { sinf(yaw_rad), 0.0f, cosf(yaw_rad) }; 

    // Update camera
    camera.position = pos_player_head(player);
    camera.target = camera.position + look_forward;

    static_assert(sizeof(InputButton) == sizeof(u32));

    // Did player move?
    if (IsKeyDown(KEY_W)) {
        btn_state &= ~InputButton_Brake;
        btn_state |= InputButton_Forward;
    } else if (IsKeyDown(KEY_S)) {
        btn_state &= ~InputButton_Forward;
        btn_state |= InputButton_Brake;
    } else {
        btn_state &= ~InputButton_Forward;
        btn_state &= ~InputButton_Brake;
    }

    // Did player jump?
    if (IsKeyPressed(KEY_SPACE)) {
        btn_state |= InputButton_Jump;
    } else {
        btn_state &= ~InputButton_Jump;
    }

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        btn_state |= InputButton_FireA;
    } else if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
        btn_state |= InputButton_FireB;
    } else {
        btn_state &= ~InputButton_FireA;
        btn_state &= ~InputButton_FireB;
    }
}

// // ======================================= MAIN FUNCS ================================================

void loop_init() {
    SetConfigFlags(FLAG_MSAA_4X_HINT);
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "SNORTAL");
    InitAudioDevice();
    DisableCursor();
    rlSetClipPlanes(10.0f, 10000000.0f);
    SetTargetFPS(60);
    srand((unsigned int)time(NULL));

    // Setup camera
    camera.position = { 100.0f, 100.0f, 100.0f };
    camera.target = {50.0f, 50.0f, 25.0f};
    camera.up = { 0.0f, 1.0f, 0.0f };
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    // Load textures
    Image full_color_img = GenImageColor(1, 1, WHITE);
    load_texture(assets_floor_tile_1_png, assets_floor_tile_1_png_len, Texture_Floor, 0);
    load_texture(assets_crate_png, assets_crate_png_len, Texture_Crate, 0);
    load_texture(assets_wall_1_png, assets_wall_1_png_len, Texture_Wall, 0);
    load_texture(assets_landmine_png, assets_landmine_png_len, Texture_Landmine, 0);
    load_texture(0, 0, Texture_FullColor, &full_color_img);

    // Load sounds
    load_sound(assets_explode1_wav, assets_explode1_wav_len, Sound_Explode1);
    load_sound(assets_explode2_wav, assets_explode2_wav_len, Sound_Explode2);
    load_sound(assets_explode3_wav, assets_explode3_wav_len, Sound_Explode3);

    f32 floor_size = 8192.0f;
    f32 floor_height = 100.0f;

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

    load_model(&mesh_floor, &textures[Texture_Floor], Model_Floor);
    load_model(&mesh_cube, &textures[Texture_Crate], Model_Crate);
    load_model(&mesh_cube, &textures[Texture_Wall], Model_Wall);
    load_model(&mesh_sphere, &textures[Texture_FullColor], Model_PortalSphere);
    load_model(&mesh_portal, &textures[Texture_FullColor], Model_Portal);
    load_model(&mesh_short_cyl, &textures[Texture_Landmine], Model_Landmine);
    load_model(&mesh_cube, &textures[Texture_FullColor], Model_Player);
}

void loop_sim(f32 delta) {
    #ifdef _DEBUG
    ZoneScoped;
    #endif

    // (Optionally) Enable debug
    if (IsKeyPressed(KEY_F5)) {
        debug_mode = !debug_mode;
    }

    sim_own_player(delta);

    // --- SIM THINGS ---
    Thing *prev_things = get_prev_things();
    Thing *next_things = get_things();
    for (u32 idx = 1; idx < get_prev_things_count(); idx++) {
        Thing *prev_thing = &prev_things[idx];
        Thing *next_thing = &next_things[idx];

        switch(prev_thing->type) {
            case ThingType::Nil: {
                continue;
            }
            case ThingType::Player: {
                break;
            }
            case ThingType::Landmine: {
                // Did we lose this landmine?
                if (next_thing->type != ThingType::Landmine) {                    
                    Sound *sound = &sounds[explode_sounds[explode_idx++]];
                    if (explode_idx >= explode_sounds_len) explode_idx = 0;
                    
                    play_distant_sound(sound, prev_thing->pos);
                }

                break;
            }
            case ThingType::PortalProjectile: {
                // Did projectile die?
                if ((next_thing->flags & ThingFlag::Dead) == 0) break;

                // Did projectile spawn a portal?
                if (next_thing->associated_thing_idx != IDX_NIL) break;

                for (u32 spark_idx = 0; spark_idx < 20; spark_idx++) {
                    Vector3 dir = { rnd_range(-1.0f, 1.0f), rnd_range(-1.0f, 1.0f), rnd_range(-1.0f, 1.0f) };
                    dir *= 1000.0f; // Gotta go fast
                    f32 life = rnd_range(0.08f, 0.15f);
                    f32 max_life = life;
                    allocate_spark(next_thing->pos, dir, life, max_life);
                }

                break;
            }
        }

        // Need to detect if we lost any landmines
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
    for (u32 idx = 1; idx < get_things_count(); idx++) {
        Thing *thing = &get_things()[idx];

        // Skip invisible shit
        if ((thing->flags & ThingFlag::Visible) == 0) continue;
        if ((thing->flags & ThingFlag::Dead) ==    1) continue;

        Model *model = &models[thing->model_idx];
        Mesh mesh = model->meshes[0];

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

        Color color = WHITE;
        if (thing->type == ThingType::Portal || thing->type == ThingType::PortalProjectile) {
            if (thing->flags & ThingFlag::Left) color = BLUE;
            else                                color = ORANGE;
        } 

        DrawModelEx(*model, thing->pos, thing->rot_axis, thing->rot_deg, scale, color);
        
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

        Model *model = &models[static_thing->model_idx];
        u32 mesh_index = 0;
        Mesh mesh = model->meshes[mesh_index];
        Vector3 scale = {1.0f, 1.0f, 1.0f};

        assert(mesh.vertexCount > 0);
        assert(mesh.triangleCount > 0);
        assert(mesh.vertices != 0);

        int material_index = model->meshMaterial[mesh_index];
        assert(material_index >= 0);
        assert(material_index < model->materialCount);

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

        assert_static_thing_before_draw(model, static_thing, scale);
        DrawModelEx(*model, static_thing->pos, static_thing->rot_axis, static_thing->rot_deg, scale, WHITE);
        
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
        draw_debug_vec3(pos_player_feet(&get_things()[client.player_idx]), WORLD_RIGHT,   RED);
        draw_debug_vec3(pos_player_feet(&get_things()[client.player_idx]), WORLD_UP,      GREEN);
        draw_debug_vec3(pos_player_feet(&get_things()[client.player_idx]), WORLD_FORWARD, BLUE);
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
        f32 speed_ratio = 0.0f;
        if (client.player_idx != IDX_NIL) {
            Thing *player = &get_things()[client.player_idx]; 
            speed_ratio = Vector3Length(player->vel) / MAX_WALK_SPEED;
            speed_ratio = Clamp(speed_ratio, 0.0f, 1.0f);
        }

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

    // if (debug_mode) {
    //     char buf[64];
    //     DrawRectangle(20, 20, MeasureText(buf, 40) + 10, 40 + 10, { 0, 0, 0, 200 });
    //     DrawText(buf, 25, 25, 40, WHITE);
    // }

    EndDrawing();
}

int main() {
    log_print(LOG_INF, "starting client\n");
    
    // Networking
    net_socket = {};
    if (!net_init()) {
        log_print(LOG_ERR, "net_init failed\n");
        return 1;
    }
    if (!net_socket_open(&net_socket, 0)) { // 0 = let OS pick client port
        log_print(LOG_ERR, "net_socket_open failed\n");
        net_shutdown();
        return 1;
    }
    net_socket_set_nonblocking(&net_socket);
    net_server = net_address(127, 0, 0, 1, SNORTAL_PORT);

    loop_init();

    // Try to connect until server let's us in
    while(true) {
        log_print(LOG_INF, "trying to connect to server");
        
        ClientToServerPacket packet_send = { PacketType::Connect };
        if (net_send(&net_socket, net_server, &packet_send, sizeof(packet_send)) < 1) {
            log_print(LOG_ERR, "failed to connect to server");
            sleep_seconds(1.0f);
            continue;
        }

        // Caveman style await
        sleep_seconds(1.0f);

        NetAddress from = {};
        i32 bytes_received = net_receive(&net_socket, &from, net_buffer, sizeof(net_buffer));
        if (bytes_received < 1) {
            log_print(LOG_INF, "Not able to connect");
            continue;
        }

        u32 header_bytes = sizeof(ServerToClientPacket);
        u32 payload_bytes = sizeof(ClientState);
        u32 bytes_expected = header_bytes + payload_bytes;
        if (bytes_received != bytes_expected) {
            log_print(LOG_ERR, "Malformed ClientState packet");
            continue;
        }

        // Read header
        ServerToClientPacket packet = {};
        memcpy(&packet, net_buffer, header_bytes);
        if (packet.type != PacketType::UpdateClientState) {
            log_print(LOG_ERR, "Received wrong packet type on first connect type=%d", packet.type);
            break;
        }

        // Read payload
        memcpy(&client, net_buffer + header_bytes, sizeof(ClientState));
        assert(client.status == ClientStatus::Live);

        log_print(LOG_INF, "Connected to server");
        break;

        // Try again in one seconds
        sleep_seconds(1.0f);
    }

    // Main loop
    while(!WindowShouldClose()) {
        f32 delta = Clamp(GetFrameTime(), 0.0f, 0.33f);

        // Ping pong buffers: 
        // start frame:
        //   current = A

        // receive update 1 -> write B
        // receive update 2 -> overwrite B
        // receive update 3 -> overwrite B

        // end receive:
        //   prev = A
        //   current = B = last received state

        bool got_things_update = false;
        u32 write_idx = thing_buffers_idx ^ 1;

        // Read all messages
        while (true) {
            NetAddress from = {};
            int bytes_received = net_receive(&net_socket, &from, net_buffer, sizeof(net_buffer));
            u32 header_bytes = sizeof(ServerToClientPacket);

            if (bytes_received <= 0) {
                break;
            }

            if (bytes_received < header_bytes) {
                log_print(LOG_WRN, "received packet too small");
                continue;
            }

            ServerToClientPacket packet = {};
            memcpy(&packet, net_buffer, header_bytes);

            switch(packet.type) {
                case PacketType::UpdateStaticThings: {
                    log_print(LOG_INF, "receiving new static things");
                    if (packet.static_things_count > MAX_STATIC_THINGS) {
                        log_print(LOG_WRN, "received too many static things");
                        continue;
                    }
                    u32 static_thing_bytes = packet.static_things_count * sizeof(StaticThing);
                    u32 bytes = header_bytes + static_thing_bytes;
                    if ((u32)bytes_received != bytes) {
                        log_print(LOG_WRN, "received malformed state packet");
                        continue;
                    }
                    memcpy(static_things, net_buffer + header_bytes, static_thing_bytes);
                    static_things_count = packet.static_things_count;
                    break;
                }
                case PacketType::UpdateThings: {
                    if (packet.things_count > MAX_THINGS) {
                        log_print(LOG_WRN, "received too many things");
                        continue;
                    }
                    u32 thing_bytes = packet.things_count * sizeof(Thing);
                    u32 bytes = header_bytes + thing_bytes;
                    if ((u32)bytes_received != bytes) {
                        log_print(LOG_WRN, "received malformed state packet");
                        continue;
                    }
                    memcpy(thing_buffers[write_idx].buf, net_buffer + header_bytes, thing_bytes);
                    thing_buffers[write_idx].count = packet.things_count;
                    got_things_update = true;
                    
                    break;
                }
                case PacketType::UpdateClientState: {
                    u32 client_state_bytes = sizeof(ClientState);
                    u32 bytes = header_bytes + client_state_bytes;
                    if ((u32)bytes_received != bytes) {
                        log_print(LOG_WRN, "received malformed state packet");
                        continue;
                    }
                    memcpy(&client, net_buffer + header_bytes, client_state_bytes);
                    camera_render_yaw = client.camera_yaw;
                    camera_render_pitch = client.camera_pitch;
                    break;
                }
                default: {
                    log_print(LOG_ERR, "unknown packet type received: %i", packet.type);
                }    
            }    
        }
        
        // Send keepalive
        if (net_keepalive_timer < 0.0f) {
            net_keepalive_timer = KEEPALIVE_INTERVAL;
            
            ClientToServerPacket packet = {
                PacketType::KeepAlive,
            };
            int sent = net_send(&net_socket, net_server, &packet, sizeof(packet));
        } else {
            net_keepalive_timer -= delta;
        }

        // Send client state
        {
            ClientToServerPacket packet = {
                .type = PacketType::UpdateClientState,
                .client_idx = client.client_idx,
                .camera_yaw = client.camera_yaw,
                .camera_pitch = client.camera_pitch,
                .btn_state = btn_state,
            };
            i32 bytes_sent = net_send(&net_socket, net_server, &packet, sizeof(packet));
            if (bytes_sent < 1) {
                log_print(LOG_ERR, "failed to update server: %i", packet.type);
            }
        }

        // Do the buffer swap before we sim
        if (got_things_update) {
            thing_buffers_idx = write_idx;
        }

        loop_sim(delta);
        loop_draw();

        char title[256];
        snprintf(title, sizeof(title), "Snortal | FPS: %d", GetFPS());
        SetWindowTitle(title);

        #ifdef _DEBUG
        FrameMark;
        #endif
    }

    net_socket_close(&net_socket);
    net_shutdown();
}