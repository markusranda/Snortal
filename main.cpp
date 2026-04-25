#include <stdio.h>
#include <math.h>
#include <cstdint>
#include <assert.h>
#include <stdlib.h>
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"

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

// CHEAT SHEET
// acos(ratio) => angle
//  cos(angle) => ratio

// ======================================= CONSTS ==============================================

#define SCREEN_WIDTH     1600
#define SCREEN_HEIGHT    900
#define TILE_SIZE_F      64.0f
#define MAX_THINGS       1024
#define IDX_NIL          0
#define DEFAULT_ROT_AXIS { 0.0f, 1.0f, 0.0f }
#define GRAVITY_ACCEL    1200.0f
#define JUMP_SPEED       650.0f
#define WALK_ACCEL       2400.0f
#define MAX_WALK_SPEED   650.0f

// ======================================= DATASTRUCTURES ======================================
struct Player {
    Vector3 pos;
    Vector3 vel;
    Vector3 siz;
    f32     camera_sensitivity;
    f32     camera_smoothness;
    f32     camera_yaw;         // Turning left/right around y axis
    f32     camera_pitch;       // Turning up/down around x axis
    f32     camera_render_yaw;
    f32     camera_render_pitch;
};

struct Textures {
    Texture2D floor;
    Texture2D crate;
    Texture2D full_color;
    Texture2D wall;
};

struct Models {
    Model floor;
    Model crate;
    Model portal;
    Model portal_sphere;
    Model wall;
};

enum class ThingType {
    Nil,
    Portal,
    PortalProjectile,
    SomethingElse,
};

enum class BodyType {
    Nil,
    Static,
    Dynamic,
};

enum ThingFlag : u32 {
    Nil     = 1 << 0,
    Gravity = 1 << 1,
    Visible = 1 << 2,
    Left    = 1 << 3,
};

struct Thing {
    ThingType type;
    BodyType  body_type;
    Vector3   pos;
    Vector3   vel;
    Vector3   siz;
    Vector3   dir;
    Vector3   rot_axis;
    f32       rot_deg;
    Model     *model;
    Color     tint;
    u32       flags;
    Vector3   portal_pos;
    Vector3   portal_siz;
};

// ======================================= STATE ===============================================

Camera3D camera = { 0 };
Player   player = { 0 };
Textures tex = { 0 };
Models   model = { 0 };
Thing    things[MAX_THINGS];
u32      things_count = 1; // We treat 0 as IDX_NIL

u32 portal_a = IDX_NIL;
u32 portal_b = IDX_NIL;
u32 portal_projectile_a = IDX_NIL;
u32 portal_projectile_b = IDX_NIL;

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

Thing make_thing(ThingType type, BodyType body_type, Vector3 pos, Vector3 vel, Vector3 siz, Vector3 rot_axis, f32 rot_def, Model *model, Color tint, u32 flags) {
    return { type, body_type, pos, vel, siz, { 0 }, rot_axis, rot_def,  model, tint, flags };
}

u32 allocate_thing(ThingType type, BodyType body_type, Vector3 pos, Vector3 vel, Vector3 siz, Vector3 rot_axis, f32 rot_def, Model *model, Color tint, u32 flags) {
    assert(things_count > 0);
    things[things_count] = make_thing(type, body_type, pos, vel, siz, rot_axis, rot_def, model, tint, flags); 

    // TODO do smarter stuff here in the future

    // Index of thing
    return things_count++;
}

bool cube_intersects(Vector3 a_pos, Vector3 a_size, Vector3 b_pos, Vector3 b_size) {
    return
        fabsf(a_pos.x - b_pos.x) <= (a_size.x + b_size.x) * 0.5f &&
        fabsf(a_pos.y - b_pos.y) <= (a_size.y + b_size.y) * 0.5f &&
        fabsf(a_pos.z - b_pos.z) <= (a_size.z + b_size.z) * 0.5f;
}

void load_texture(const char *path, Texture2D *out) {
    *out = LoadTexture(path);
    SetTextureFilter(*out, TEXTURE_FILTER_POINT);
    SetTextureWrap(*out, TEXTURE_WRAP_REPEAT);
}

void load_model(Mesh *mesh, Texture2D *tex, Model *out) {
    *out = LoadModelFromMesh(*mesh);
    if (tex) out->materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = *tex;
}

Vector3 pos_player_head() {
    return { player.pos.x, player.pos.y + player.siz.y * 0.5f, player.pos.z };
}

Vector3 pos_player_foot() {
    return { player.pos.x, player.pos.y - player.siz.y * 0.5f, player.pos.z };
}

void spawn_portal_projectile(u32 *portal_idx, Vector3 look_forward, bool left) {
    Vector3 vel = look_forward * 2500.0f;
    Vector3 pos = pos_player_head();
    Vector3 size = { 5.0f, 5.0f, 5.0f };
    Color color = left ? BLUE : ORANGE;
    u32 flags = ThingFlag::Visible;
    if (left) {
        flags |= ThingFlag::Left;
    }

    // Either we allocate one if it doens't exist or we ovewrite it
    if (*portal_idx == IDX_NIL) {
        *portal_idx = allocate_thing(ThingType::PortalProjectile, BodyType::Dynamic, pos, vel, size, DEFAULT_ROT_AXIS, 0.0f, &model.portal_sphere, color, flags);
    } else {
        things[*portal_idx] = make_thing(ThingType::PortalProjectile, BodyType::Dynamic, pos, vel, size, DEFAULT_ROT_AXIS, 0.0f, &model.portal_sphere, color, flags);
    }
}

enum class Face {
    Nil,
    Left,
    Right,
    Top,
    Bottom,
    Front,
    Back,
};

struct GeometricFace {
    Face    face;
    f64     dist;
    Vector3 vec;
};

int comp_face_asc(const void *a, const void *b) {
    GeometricFace a_val = *(const GeometricFace *)a;
    GeometricFace b_val = *(const GeometricFace *)b;

    return (a_val.dist > b_val.dist) - (a_val.dist < b_val.dist);
}

void spawn_portal(u32 *portal_idx, Thing col_thing, Vector3 hit_pos, Vector3 projectile_vel, bool left) {
    f32 portal_radius   = 150.0f;
    Vector3 vel         = { 0 };
    Vector3 size        = { portal_radius, portal_radius, 1.0f };
    Color   color       = left ? BLUE : ORANGE;
    Vector3 disc_normal = { 0.0f, 0.0f, 1.0f };
    u32 flags = ThingFlag::Visible;
    if (left) {
        flags |= ThingFlag::Left;
    }

    // Find surface normal
    // I need to find all the faces of the cube I hit
    // Then I need to find the face that is closest.
    Vector3 col_min = col_thing.pos - col_thing.siz * 0.5f;
    Vector3 col_max = col_thing.pos + col_thing.siz * 0.5f;
    GeometricFace faces[6] = {
        { Face::Left,   fabs(col_min.x - hit_pos.x), { -1.0f,  0.0f,  0.0f } }, 
        { Face::Right,  fabs(col_max.x - hit_pos.x), {  1.0f,  0.0f,  0.0f } }, 
        { Face::Bottom, fabs(col_min.y - hit_pos.y), {  0.0f, -1.0f,  0.0f } }, 
        { Face::Top,    fabs(col_max.y - hit_pos.y), {  0.0f,  1.0f,  0.0f } }, 
        { Face::Back,   fabs(col_min.z - hit_pos.z), {  0.0f,  0.0f,  1.0f } }, 
        { Face::Front,  fabs(col_max.z - hit_pos.z), {  0.0f,  0.0f, -1.0f } }, 
    };
    qsort(faces, 6, sizeof(GeometricFace), comp_face_asc);

    Vector3 normalized_projectile_vel = Vector3Normalize(projectile_vel);
    Vector3 outwards = faces[0].vec;

    if (Vector3DotProduct(outwards, normalized_projectile_vel) > 0) {
        outwards = outwards * -1.0f;
    }

    // Move portal slightly outward
    Vector3 pos = hit_pos + outwards * 0.1f; 
    
    // Rotate portal to match surface
    Vector3 rot_axis = Vector3CrossProduct(disc_normal, outwards);
    f32 dir_diff     = Clamp(Vector3DotProduct(disc_normal, outwards), -1, 1);
    f32 cmp_epsilon  = 0.999f;
    if (dir_diff < -cmp_epsilon || dir_diff > cmp_epsilon) {
        rot_axis = { 0.0f, 1.0f, 0.0f };
    }
    f32 rot_degree = acosf(dir_diff) * RAD2DEG;

    // Either we allocate one if it doesn't exist or we ovewrite it
    if (*portal_idx == IDX_NIL) {
        *portal_idx = allocate_thing(ThingType::Portal, BodyType::Static, pos, vel, size, rot_axis, rot_degree, &model.portal, color, flags);
    } else {
        things[*portal_idx] = make_thing(ThingType::Portal, BodyType::Static, pos, vel, size, rot_axis, rot_degree, &model.portal, color, flags);
    }

    Vector3 portal_size = { 0 };
    f32 portal_depth = 25.0f;
    if (outwards.x != 0.0f) portal_size = { portal_depth, portal_radius, portal_radius };
    if (outwards.y != 0.0f) portal_size = { portal_radius, portal_depth, portal_radius };
    if (outwards.z != 0.0f) portal_size = { portal_radius, portal_radius, portal_depth };

    things[*portal_idx].portal_siz = portal_size;
    f32 portal_half_depth =
        fabsf(outwards.x) * portal_size.x * 0.5f +
        fabsf(outwards.y) * portal_size.y * 0.5f +
        fabsf(outwards.z) * portal_size.z * 0.5f;
    things[*portal_idx].portal_pos = pos + outwards * portal_half_depth;
    things[*portal_idx].dir = outwards;
}

// ======================================= MAIN FUNCS ================================================

void loop_init() {
    SetConfigFlags(FLAG_MSAA_4X_HINT);
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "SNORTAL");
    DisableCursor();
    rlSetClipPlanes(10.0f, 10000000.0f);

    camera.position = { 100.0f, 100.0f, 100.0f };
    camera.target = {50.0f, 50.0f, 25.0f};
    camera.up = { 0.0f, 1.0f, 0.0f };
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    player.siz = { 25.0f, 100.0f, 25.0f };
    player.pos = {-100.0f, 1.0f + player.siz.y * 0.5f, -100.0f };
    player.camera_sensitivity = 0.05f;
    player.camera_smoothness = 80.0f;

    // Load textures
    Image img = GenImageColor(1, 1, WHITE);
    load_texture("assets/floor_tile_1.png", &tex.floor);
    load_texture("assets/crate.png", &tex.crate);
    load_texture("assets/wall_1.png", &tex.wall);
    tex.full_color = LoadTextureFromImage(img);

    // Sizes
    f32 floor_size = 8192.0f;
    f32 floor_height = 100.0f;
    f32 crate_size = 4.0f * TILE_SIZE_F;
    f32 portal_size = TILE_SIZE_F;
    f32 wall_height = 6.0f * TILE_SIZE_F;
    f32 wall_thick = 1.0f * TILE_SIZE_F;

    // Create meshes
    Mesh mesh_cube = GenMeshCube(TILE_SIZE_F, TILE_SIZE_F, TILE_SIZE_F);
    Mesh mesh_sphere = GenMeshSphere(TILE_SIZE_F * 0.5f, 16, 16);
    Mesh mesh_portal = make_disc(2.0f * TILE_SIZE_F);
    Mesh mesh_floor = GenMeshCube(floor_size, floor_height, floor_size);

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

    // Create floor
    allocate_thing(
        ThingType::SomethingElse,
        BodyType::Static,
        { 0.0f, -floor_height * 0.5f, 0.0f },
        { 0 },
        { floor_size, floor_height, floor_size },
        DEFAULT_ROT_AXIS,
        0.0f,
        &model.floor,
        WHITE,
        ThingFlag::Visible
    );

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
        allocate_thing(
            ThingType::SomethingElse,
            BodyType::Static,
            blocks[i].pos,
            { 0 },
            blocks[i].siz,
            DEFAULT_ROT_AXIS,
            0.0f,
            blocks[i].model,
            blocks[i].color,
            ThingFlag::Visible
        );
    }

    #undef WALL_X
    #undef WALL_Z
    #undef CRATE
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
    Vector3 look_forward = { 
        sinf(yaw_look_rad) * cosf(pitch_look_rad), 
        sinf(pitch_look_rad), 
        cosf(yaw_look_rad) * cosf(pitch_look_rad)};
    Vector3 move_forward = { sinf(yaw_rad), 0.0f, cosf(yaw_rad) }; 
    Vector3 right        = { -cosf(yaw_rad), 0.0f, sinf(yaw_rad) };

    player.vel.x += x_dir * WALK_ACCEL * delta;
    player.vel.z += z_dir * WALK_ACCEL * delta;

    #define FRICTION 8.0f

    if (x_dir == 0.0f) {
        player.vel.x -= player.vel.x * FRICTION * delta;
    }

    if (z_dir == 0.0f) {
        player.vel.z -= player.vel.z * FRICTION * delta;
    }

    player.vel.x = Clamp(player.vel.x, -MAX_WALK_SPEED, MAX_WALK_SPEED);
    player.vel.z = Clamp(player.vel.z, -MAX_WALK_SPEED, MAX_WALK_SPEED);

    player.pos   += move_forward * (player.vel.z * delta);
    player.pos   += right *        (player.vel.x * delta);
    
    // Gravity
    bool grounded = false;
    for (u32 col_idx = 1; col_idx < things_count; col_idx++) {
        Thing *col = &things[col_idx];
        if (col->type == ThingType::Nil) continue;

        Vector3 min = col->pos - col->siz * 0.5f;
        Vector3 max = col->pos + col->siz * 0.5f;

        // Check horizontal overlap (X/Z)
        if (player.pos.x < min.x || player.pos.x > max.x) continue;
        if (player.pos.z < min.z || player.pos.z > max.z) continue;

        // Check if player is just above the top surface
        f32 top = max.y;
        f32 epsilon = 1.0f; // tweak if needed

        f32 player_foot = pos_player_foot().y;
        if (player_foot >= top && player_foot <= top + epsilon) {
            grounded = true;
            player.vel.y = 0;

            // Snap player to surface (prevents sinking/jitter)
            player_foot = top;
            break;
        }
    }

    if (grounded) {
        if (player.vel.y < 0.0f) {
            player.vel.y = 0.0f;
        }

        if (IsKeyPressed(KEY_SPACE)) {
            player.vel.y = JUMP_SPEED;
        }
    } else {
        player.vel.y -= GRAVITY_ACCEL * delta;
    }

    // Update y based on gravity etc
    player.pos.y += player.vel.y * delta;

    // Update camera
    camera.position = pos_player_head();
    camera.target = camera.position + look_forward;

    // Did the player shoot?
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))  spawn_portal_projectile(&portal_projectile_a, look_forward, true);
    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) spawn_portal_projectile(&portal_projectile_b, look_forward, false);

    // --- SIM THINGS ---
    for (u32 idx = 1; idx < things_count; idx++) {
        Thing *thing = &things[idx];
        
        if (thing->type == ThingType::Nil) continue;

        switch(thing->type) {
            case ThingType::Portal: {
                // Check if player walked into us
                if (cube_intersects(thing->portal_pos, thing->portal_siz, player.pos, player.siz)) {
                    // Select the other portal as the destination (Optionally)                    
                    if (portal_b == IDX_NIL || portal_a == IDX_NIL) break;

                    u32 exit_portal_idx = portal_a;
                    if (exit_portal_idx == idx) exit_portal_idx = portal_b; 
                    
                    Thing exit_portal = things[exit_portal_idx]; 

                    f32 player_half_along_normal =
                        fabsf(exit_portal.dir.x) * player.siz.x * 0.5f +
                        fabsf(exit_portal.dir.y) * player.siz.y * 0.5f +
                        fabsf(exit_portal.dir.z) * player.siz.z * 0.5f;

                    f32 portal_half_depth =
                        fabsf(exit_portal.dir.x) * exit_portal.portal_siz.x * 0.5f +
                        fabsf(exit_portal.dir.y) * exit_portal.portal_siz.y * 0.5f +
                        fabsf(exit_portal.dir.z) * exit_portal.portal_siz.z * 0.5f;

                    f32 exit_padding = 5.0f;

                    player.pos = exit_portal.portal_pos + exit_portal.dir * (portal_half_depth + player_half_along_normal + exit_padding);
                    
                    // Figure out what the new camera target should be using vec3 exit_portal.dir
                    f32 yaw_look_rad   = player.camera_render_yaw   * DEG2RAD;
                    f32 pitch_look_rad = player.camera_render_pitch * DEG2RAD;
                    look_forward = { 
                        sinf(yaw_look_rad) * cosf(pitch_look_rad), 
                        sinf(pitch_look_rad), 
                        cosf(yaw_look_rad) * cosf(pitch_look_rad)
                    };

                    // Update yaw for next frame. This will make it so the camera only changes horizontal direction
                    player.camera_yaw        = atan2f(exit_portal.dir.x, exit_portal.dir.z) * RAD2DEG;
                    player.camera_render_yaw = player.camera_yaw;
                }

                break;
            }
        }

        switch (thing->body_type) {
            case BodyType::Dynamic: {
                thing->pos += thing->vel * delta;

                // Check if this collides with any of the static motherfuckers
                for (u32 col_idx = 1; col_idx < things_count; col_idx++) {
                    Thing *col_thing = &things[col_idx];
                    if (col_idx == idx) continue;
                    if (col_thing->type == ThingType::Nil) continue;

                    if (cube_intersects(thing->pos, thing->siz, col_thing->pos, col_thing->siz)) {
                        // Back it up lorry style
                        while(cube_intersects(thing->pos, thing->siz, col_thing->pos, col_thing->siz)) {
                            thing->pos -= Vector3Normalize(thing->vel) * 0.001f;
                        }

                        switch(thing->type) {
                            case ThingType::PortalProjectile: {
                                if (thing->flags & ThingFlag::Left) {
                                    spawn_portal(&portal_a, *col_thing, thing->pos, thing->vel, true);
                                } else {
                                    spawn_portal(&portal_b, *col_thing, thing->pos, thing->vel, false);
                                }

                                // TODO This is not recommended, when we introduce dealloc use that.
                                *thing = {};
                                
                                break;
                            }
                        }
                    }
                }

                break;
            }
            case BodyType::Static: {
                break;
            }
            default: {
                assert(false); // Should never happen fool
            }
        }
    }
}

void loop_draw() {
    BeginDrawing();
    ClearBackground(WHITE);
    BeginMode3D(camera);

    // Render all things
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
        
        // Debug draw col box for portals
        if (thing->portal_siz.x > 0.0f) {
            DrawCube(thing->portal_pos, thing->portal_siz.x, thing->portal_siz.y, thing->portal_siz.z, {255, 0, 0, 128});
        }
    }

    EndMode3D();
    EndDrawing();
}

int main() {
    loop_init();
    while(!WindowShouldClose()) {
        loop_sim();
        loop_draw();
    }
}