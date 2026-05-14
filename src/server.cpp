#include "base.h"
#include "net.h"

// --- PROFILING ---
#ifdef _DEBUG
#include "tracy/Tracy.hpp"
#endif

// ======================================= FORWARD DECLARATIONS ================================

Vector3 cube_surface_normal(Vector3 cube_pos, Vector3 cube_size, Vector3 hit_pos);
u32 allocate_static_thing(u32 model_idx, Vector3 pos, Vector3 siz, Vector3 rot_axis, f32 rot_deg);
u32 allocate_thing(ThingType type, u32 model_idx, u32 client_idx, Vector3 pos, Vector3 vel, Vector3 siz, Vector3 rot_axis, f32 rot_deg, u32 flags);
void deallocate_thing(u32 thing_idx);

// ======================================= CONSTS ==============================================

#define TICK_TIME 0.0166666666 // 60FPS
    
// ======================================= DATASTRUCTURES ======================================



// ======================================= STATE ===============================================

// --- Network ---
char net_buffer[MAX_NET_BUFFER_BYTES]; // TODO This needs to be big enough to send everything in one go. Attack with with smart static_asserts and shit
NetSocket net_socket = {};

// --- Game ---
StaticThing static_things[MAX_STATIC_THINGS];
Thing       things[MAX_THINGS];
ClientState clients[MAX_CLIENTS];
u32         static_things_count = 1; // We treat 0 as IDX_NIL
u32         things_count = 1;        // We treat 0 as IDX_NIL
u32         clients_count = 1;
u32         things_free_head = IDX_NIL;
u32         clients_free_head = IDX_NIL;
u32         frame_count = 0;

// ======================================= MAKERS ==============================================

Thing make_thing(ThingType type, u32 model_idx, u32 thing_idx, u32 client_idx, Vector3 pos, Vector3 vel, Vector3 siz, Vector3 rot_axis, f32 rot_deg, u32 flags) {
    Thing thing = {
        .type = type,
        .thing_idx = thing_idx,
        .model_idx = model_idx,
        .client_idx = client_idx,
        .pos = pos,
        .vel = vel,
        .siz = siz,
        .rot_axis = rot_axis,
        .rot_deg = rot_deg,
        .flags = flags,
    };
    
    // Default hitbox is entire thing with not offset
    thing.hitbox_offset = { };
    thing.hitbox_siz = { siz };

    return thing;
}

void make_portal_projectile(u32 client_idx, Vector3 pos, Vector3 look_forward, bool left) {
    Vector3 vel = look_forward * 5000.0f;
    Vector3 size = { 5.0f, 5.0f, 5.0f };
    u32 flags = ThingFlag::Visible;
    if (left) {
        flags |= ThingFlag::Left;
    }

    // We need to figure out the client already has an active projectile and dealloc that one before creating a new
    u32 thing_idx = IDX_NIL;
    
    ClientState *client = &clients[client_idx];
    u32 *portal_idx = left ? &client->portal_projectile_idx_a : &client->portal_projectile_idx_b;
    if (*portal_idx != IDX_NIL && things[*portal_idx].thing_idx != IDX_NIL) {
        thing_idx = *portal_idx;
    }

    // (Optionally) Dealloc the thing before we create a new one
    if (thing_idx != IDX_NIL) {
        deallocate_thing(thing_idx);
    }
    // Allocate new projectile. Let's go
    thing_idx = allocate_thing(ThingType::PortalProjectile, Model_PortalSphere, client_idx, pos, vel, size, DEFAULT_ROT_AXIS, 0.0f, flags);
    assert(thing_idx != IDX_NIL && thing_idx < MAX_THINGS);
    
    // Persist idx in client
    *portal_idx = thing_idx;
}

i32 make_portal(u32 client_idx, StaticThing col_thing, Vector3 hit_pos, Vector3 projectile_vel, bool left) {
    u32 thing_idx = IDX_NIL;
    ClientState *client = &clients[client_idx];
    u32 *portal_idx = left ? &client->portal_idx_a : &client->portal_idx_b;
    if (*portal_idx != IDX_NIL && things[*portal_idx].thing_idx != IDX_NIL) {
        thing_idx = *portal_idx;
    }

    if (thing_idx != IDX_NIL) {
        deallocate_thing(thing_idx);
    }
    
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

    if (fabsf(point_local_x) + portal_radius > face_half_width) return -1;
    if (fabsf(point_local_y) + portal_radius > face_half_height) return -1;

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

    // Create the portal
    *portal_idx = allocate_thing(ThingType::Portal, Model_Portal, client_idx, pos, vel, size, rot_axis, rot_degree, flags);
    
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

    return *portal_idx;
}

// ======================================= HELPERS =============================================

u32 allocate_static_thing(u32 model_idx, Vector3 pos, Vector3 siz, Vector3 rot_axis, f32 rot_deg) {
    if (static_things_count >= MAX_STATIC_THINGS) return IDX_NIL;
    
    Vector3 half_size = { siz.x * 0.5f, siz.y * 0.5f, siz.z * 0.5f };
    AABB aabb = { 
        .min = { pos.x - half_size.x, pos.y - half_size.y, pos.z - half_size.z },
        .max = { pos.x + half_size.x, pos.y + half_size.y, pos.z + half_size.z },
    };

    static_things[static_things_count] = StaticThing{
        .model_idx = model_idx,
        .aabb = aabb,
        .pos = pos,
        .siz = siz,
        .rot_axis = rot_axis,
        .rot_deg = rot_deg,
    };

    return static_things_count++;
}

u32 allocate_thing(ThingType type, u32 model_idx, u32 client_idx, Vector3 pos, Vector3 vel, Vector3 siz, Vector3 rot_axis, f32 rot_deg, u32 flags) {
    u32 thing_idx = IDX_NIL;
    if (things_free_head != IDX_NIL) {
        thing_idx = things_free_head;
        things_free_head = things[thing_idx].client_idx; // next free index stored here
    } else {
        if (things_count >= MAX_THINGS) return IDX_NIL;
        thing_idx = things_count++;
    }

    // Create the thing
    things[thing_idx] = make_thing(type, model_idx, thing_idx, client_idx, pos, vel, siz, rot_axis, rot_deg, flags); 

    return thing_idx;
}

void deallocate_thing(u32 thing_idx) {
    if (thing_idx == IDX_NIL) return;
    if (thing_idx >= things_count) return;
    if (things[thing_idx].type == ThingType::Nil) return;

    // Reset slot
    things[thing_idx] = {};
    
    // Store next freelist index inside the dead slot.
    things[thing_idx].client_idx = things_free_head;
    things_free_head = thing_idx;
}

u32 allocate_client(NetAddress from) {
    u32 client_idx = IDX_NIL;
    if (clients_free_head != IDX_NIL) {
        client_idx = clients_free_head;
        clients_free_head = clients[client_idx].client_idx; // next free index stored here
    } else {
        if (clients_count >= MAX_THINGS) return IDX_NIL;
        client_idx = clients_count++;
    }
    
    // Create the thing
    clients[client_idx] = { 
        .status = ClientStatus::Live,
        .client_idx = client_idx,
        .player_idx = allocate_thing(
            ThingType::Player, Model_Player, 
            client_idx, {}, {}, 
            {50.0f, 150.0f, 50.0f}, 
            WORLD_UP, 0.0f, (ThingFlag::Visible | ThingFlag::Gravity)),
        .address = from, 
        .last_seen = now_millis(),
    };

    return client_idx;
}

void deallocate_client(u32 client_idx) {
    if (client_idx == IDX_NIL) return;
    if (client_idx >= clients_count) return;

    // Reset slot
    clients[client_idx] = {};

    // Store next freelist index inside the dead slot.
    clients[client_idx].client_idx = clients_free_head;
    clients_free_head = client_idx;
}

bool send_server_packet(NetAddress to, ServerToClientPacket header, void *payload, u32 payload_bytes) {
    ZoneScoped;

    u32 header_bytes = sizeof(ServerToClientPacket);
    u32 total_bytes = header_bytes + payload_bytes;

    if (payload == 0 && payload_bytes != 0) {
        log_print(LOG_ERR, "send_server_packet: payload is null but payload_bytes=%u", payload_bytes);
        return false;
    }
    if (total_bytes < header_bytes) {
        log_print(LOG_ERR, "send_server_packet: packet size overflow header=%u payload=%u", header_bytes, payload_bytes);
        return false;
    }
    if (total_bytes > MAX_NET_BUFFER_BYTES) {
        log_print(LOG_ERR, "send_server_packet: packet too large for net_buffer bytes=%u max=%u", total_bytes, MAX_NET_BUFFER_BYTES);
        return false;
    }
    if (total_bytes > MAX_UDP_PACKET_BYTES) {
        log_print(LOG_ERR, "send_server_packet: packet too large for udp bytes=%u max=%u", total_bytes, MAX_UDP_PACKET_BYTES);
        return false;
    }

    memcpy(net_buffer, &header, header_bytes);
    if (payload_bytes > 0) {
        memcpy(net_buffer + header_bytes, payload, payload_bytes);
    }

    int sent = net_send(&net_socket, to, net_buffer, total_bytes);
    if (sent < 0) {
        log_print(LOG_ERR, "send_server_packet: net_send failed type=%u bytes=%u", header.type, total_bytes);
        return false;
    }
    if (sent != (int)total_bytes) {
        log_print(LOG_ERR, "send_server_packet: partial send type=%u sent=%i expected=%u", header.type, sent, total_bytes);
        return false;
    }

    return true;
}

int comp_face_asc(const void *a, const void *b) {
    GeometricFace a_val = *(const GeometricFace *)a;
    GeometricFace b_val = *(const GeometricFace *)b;

    return (a_val.dist > b_val.dist) - (a_val.dist < b_val.dist);
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

void sim_type_player(Thing *player, f32 delta) {
    ClientState *client_state = &clients[player->client_idx]; 

    // Figure out forward and right direction after converting to rads
    f32 yaw_rad          = client_state->camera_yaw    * DEG2RAD;
    f32 pitch_rad   = client_state->camera_pitch * DEG2RAD;
    
    Vector3 look_forward = {
    sinf(yaw_rad) * cosf(pitch_rad),
    sinf(pitch_rad),
    cosf(yaw_rad) * cosf(pitch_rad),
};
    // Vector3 look_forward = { sinf(yaw_rad), sinf(pitch_rad), cosf(yaw_rad)};
    Vector3 move_forward = { sinf(yaw_rad), 0.0f, cosf(yaw_rad) }; 

    // Handle forward movement attempt
    if (client_state->btn_state & InputButton_Forward) {
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
    else if (client_state->btn_state & InputButton_Brake) {
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

    // Did the player shoot?
    Vector3 player_head_pos = pos_player_head(player);
    if (client_state->btn_pressed & InputButton_FireA) {
        make_portal_projectile(player->client_idx, player_head_pos, look_forward, true);
        client_state->btn_pressed &= ~InputButton_FireA;
    }
    if (client_state->btn_pressed & InputButton_FireB) {
        make_portal_projectile(player->client_idx, player_head_pos, look_forward, false);
        client_state->btn_pressed &= ~InputButton_FireB;
    }
}

void collision_portal_projectile_on_static(Thing projectile_cpy, StaticThing *col_thing, u32 idx) {
    assert((projectile_cpy.flags & ThingFlag::Dead) == 0);
    AABB    projectile_aabb = get_thing_aabb(&projectile_cpy);
    AABB    col_aabb = col_thing->aabb;
    
    // Back it up lorry style
    while (aabb_intersects(get_thing_aabb(&projectile_cpy), col_aabb)) {
        projectile_cpy.pos -= Vector3Normalize(projectile_cpy.vel) * 0.001f;
    }

    bool left = projectile_cpy.flags & ThingFlag::Left;
    i32 portal_idx = make_portal(projectile_cpy.client_idx, *col_thing, projectile_cpy.pos, projectile_cpy.vel, left);

    // Update projectile
    Thing *projectile = &things[idx];
    projectile->flags |= ThingFlag::Dead;
    projectile->at_frame_count = frame_count;
    projectile->vel = {};
    if (portal_idx > IDX_NIL) {
        projectile->associated_thing_idx = portal_idx;
    }
}

void collision_thing_on_landmine(Thing *thing, Thing *landmine) {
    deallocate_thing(landmine->thing_idx);
}

void collision_thing_on_portal(Thing *player, Thing *entry_portal) {
    ClientState *client = &clients[player->client_idx]; 

    // We need to find the other portal
    u32 exit_portal_idx = IDX_NIL;
    for (u32 thing_idx = 0; thing_idx < things_count; thing_idx++) {
        if (things[thing_idx].type != ThingType::Portal) continue;
        if (thing_idx == entry_portal->thing_idx) continue;
        if (things[thing_idx].client_idx != entry_portal->client_idx) continue;

        exit_portal_idx = thing_idx;
    }
    if (exit_portal_idx == IDX_NIL) return;
    Thing exit_portal = things[exit_portal_idx]; 
    
    f32 thing_half_along_normal =
        fabsf(exit_portal.basis_forward.x) * player->siz.x * 0.5f +
        fabsf(exit_portal.basis_forward.y) * player->siz.y * 0.5f +
        fabsf(exit_portal.basis_forward.z) * player->siz.z * 0.5f;

    f32 portal_half_depth =
        fabsf(exit_portal.basis_forward.x) * exit_portal.hitbox_siz.x * 0.5f +
        fabsf(exit_portal.basis_forward.y) * exit_portal.hitbox_siz.y * 0.5f +
        fabsf(exit_portal.basis_forward.z) * exit_portal.hitbox_siz.z * 0.5f;

    f32 exit_padding = 50.0f;
    
    // Figure out what the new camera target should be using vec3 exit_portal.basis_forward
    f32 yaw_look_rad   = client->camera_yaw   * DEG2RAD;
    f32 pitch_look_rad = client->camera_pitch * DEG2RAD;
    Vector3 look_forward = { 
        sinf(yaw_look_rad) * cosf(pitch_look_rad), 
        sinf(pitch_look_rad), 
        cosf(yaw_look_rad) * cosf(pitch_look_rad),
    };

    // We need to flip forward since the basis of entry portal is toward us
    look_forward *= -1.0f;

    // Find relative looking direction from the portal we entered
    f32 camera_local_entry_forward = Vector3DotProduct(look_forward, entry_portal->basis_forward);
    f32 camera_local_entry_up      = Vector3DotProduct(look_forward, entry_portal->basis_up);
    f32 camera_local_entry_right   = Vector3DotProduct(look_forward, entry_portal->basis_right);
    Vector3 camera_local_exit = 
        exit_portal.basis_forward * camera_local_entry_forward +
        exit_portal.basis_up * camera_local_entry_up +
        exit_portal.basis_right * camera_local_entry_right;
        
    // Find the entry-portal-local components of thing velocity
    Vector3 move_forward = player->vel * -1.0f;
    f32 thing_local_forward = Vector3DotProduct(move_forward, entry_portal->basis_forward);
    f32 thing_local_up      = Vector3DotProduct(move_forward, entry_portal->basis_up);
    f32 thing_local_right   = Vector3DotProduct(move_forward, entry_portal->basis_right);
        
    // Set new camera direction
    client->camera_yaw = atan2f(camera_local_exit.x, camera_local_exit.z) * RAD2DEG;
    ServerToClientPacket packet = { PacketType::UpdateClientState };
    send_server_packet(client->address, packet, client, sizeof(*client));

    // Set new position
    player->pos = exit_portal.portal_spawn_pos + exit_portal.basis_forward * (portal_half_depth + thing_half_along_normal + exit_padding);

    // Keep the momentum in the portals direction
    player->vel =
        (exit_portal.basis_right   * thing_local_right) + 
        (exit_portal.basis_up      * thing_local_up) + 
        (exit_portal.basis_forward * thing_local_forward);
}

// ======================================= MAIN FUNCS ==========================================

void loop_init() {
    srand((unsigned int)time(NULL));

    // Sizes
    f32 floor_size = 8192.0f;
    f32 floor_height = 100.0f;
    f32 crate_size = 4.0f * TILE_SIZE_F;
    f32 landmine_size = TILE_SIZE_F;
    f32 portal_size = TILE_SIZE_F;
    f32 wall_height = 6.0f * TILE_SIZE_F;
    f32 wall_thick = 1.0f * TILE_SIZE_F;

    // Create floor
    u32 floor_idx = allocate_static_thing(
        Model_Floor,
        { 0.0f, -floor_height * 0.5f, 0.0f },
        { floor_size, floor_height, floor_size },
        DEFAULT_ROT_AXIS,
        0.0f
    );
    assert(floor_idx != IDX_NIL);

    // Create roof
    u32 roof_idx = allocate_static_thing(
        Model_Floor,
        { 0.0f, 4096.0f, 0.0f },
        { floor_size, floor_height, floor_size },
        DEFAULT_ROT_AXIS,
        0.0f
    );
    assert(roof_idx != IDX_NIL);

    struct BlockSpec {
        u32 model_idx;
        Vector3 pos;
        Vector3 siz;
        Model* model;
        Color color;
    };

    #define WALL_X(x, z, sx) { Model_Wall, { (x), wall_height * 0.5f, (z) }, { (sx), wall_height, wall_thick } }
    #define WALL_Z(x, z, sz) { Model_Wall, { (x), wall_height * 0.5f, (z) }, { wall_thick, wall_height, (sz) } }
    #define CRATE(x, y, z, sx, sy, sz) { Model_Crate, { (x), (y), (z) }, { (sx), (sy), (sz) } }

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
            blocks[i].model_idx,
            blocks[i].pos,
            blocks[i].siz,
            DEFAULT_ROT_AXIS,
            0.0f
        );
        assert(block_idx != IDX_NIL);
    }

    // Minefield generation (dumb but effective)
    {
        f32 spacing = 800.0f;   // distance between mines
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
                    Model_Landmine,
                    0,
                    { rx, 0.0f, rz },
                    { 0 },
                    { landmine_size, landmine_size * 0.25f, landmine_size },
                    DEFAULT_ROT_AXIS,
                    0.0f,
                    ThingFlag::Visible | ThingFlag::Gravity
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

    // --- SIM THINGS ---
    for (u32 idx = 1; idx < things_count; idx++) {
        Thing *thing = &things[idx];
        if (thing->type == ThingType::Nil) continue;

        // Dead fools tell no tale
        if ((thing->flags & ThingFlag::Dead) && (frame_count - thing->at_frame_count) >= 10) {
            deallocate_thing(idx);
            break;
        }

        // --- Handle whatever is unique ---
        switch(thing->type) {
            case ThingType::Player: {
                sim_type_player(thing, delta);
                
                // Handle jump input
                ClientState *client_state = &clients[thing->client_idx]; 
                if ((thing->flags & ThingFlag::Grounded) && (client_state->btn_state & InputButton_Jump)) {
                    thing->vel.y = JUMP_SPEED;
                }

                break;
            }
        }

        // ------ Continue handling everything that's common ------
        
        // Apply gravity
        if (thing->flags & ThingFlag::Gravity) {
            thing->vel.y -= GRAVITY_ACCEL * delta;
        }
 
        // Resolve all velocities into correct positions
        {
            #ifdef _DEBUG
            ZoneScopedN("resolve_against_static");
            #endif
            
            // RESOLVE X AXIS
            if ((thing->flags & ThingFlag::Dead) == 0) {
                thing->pos.x += thing->vel.x * delta;
                for (u32 col_idx = 1; col_idx < static_things_count; col_idx++) {
                    AABB thing_aabb = get_thing_aabb(thing);
                    AABB col_aabb = static_things[col_idx].aabb;

                    if (aabb_intersects(thing_aabb, col_aabb)) {
                        if (thing->type == ThingType::PortalProjectile) {
                            collision_portal_projectile_on_static(*thing, &static_things[col_idx], idx);
                            break;
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
            }
            
            // RESOLVE Z AXIS
            if ((thing->flags & ThingFlag::Dead) == 0) {
                thing->pos.z += thing->vel.z * delta;
                for (u32 col_idx = 1; col_idx < static_things_count; col_idx++) {

                    AABB thing_aabb = get_thing_aabb(thing);
                    AABB col_aabb = static_things[col_idx].aabb;

                    if (aabb_intersects(thing_aabb, col_aabb)) {
                        if (thing->type == ThingType::PortalProjectile) {
                            collision_portal_projectile_on_static(*thing, &static_things[col_idx], idx);
                            break;
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
            }
            
            // RESOLVE Y AXIS
            if ((thing->flags & ThingFlag::Dead) == 0) {
                thing->pos.y += thing->vel.y * delta;
                for (u32 col_idx = 1; col_idx < static_things_count; col_idx++) {
                    AABB thing_aabb = get_thing_aabb(thing);
                    AABB col_aabb = static_things[col_idx].aabb;

                    if (aabb_intersects(thing_aabb, col_aabb)) {
                        if (thing->type == ThingType::PortalProjectile) {
                            collision_portal_projectile_on_static(*thing, &static_things[col_idx], idx);
                            break;
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

        // Collision check dynamic against dynamic
        if ((thing->flags & ThingFlag::Dead) == 0) {
            AABB thing_aabb = get_thing_aabb(thing);
            for (u32 col_idx = 1; col_idx < things_count; col_idx++) {
                Thing *col_thing = &things[col_idx];
                if (col_idx == idx) continue;
                if (col_thing->type == ThingType::Nil) continue;
                if (thing->flags & ThingFlag::Dead) continue;
                AABB col_aabb = get_thing_aabb(col_thing);

                if (aabb_intersects(thing_aabb, col_aabb)) {
                    switch (col_thing->type) {
                        case ThingType::Portal: {
                            collision_thing_on_portal(thing, col_thing);
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
    }
}

void loop_read_messages(f32 delta) {    
    // Read all messages
    while (true) {
        NetAddress from = {};

        int bytes_received = net_receive(&net_socket, &from, net_buffer, sizeof(net_buffer));            
        if (bytes_received > 0) {                
            ClientToServerPacket packet = {};
            memcpy(&packet, net_buffer, sizeof(packet));
            
            switch (packet.type) {
                case PacketType::Nil: {
                    log_print(LOG_WRN, "client %i:%i is sending nil packets, something is wrong.", from.host, from.port);
                    break;
                }
                case PacketType::Connect: {
                    bool already_exists = false;
                    for (u32 i = 1; i < clients_count; i++) {
                        if (clients[i].address.host == from.host && clients[i].address.port == from.port) {
                            already_exists = true;
                            break;
                        }
                    }
                    if (already_exists) {
                        log_print(LOG_ERR, "tried to add client %i:%i twice!", from.host, from.port);
                        break;
                    }
                    
                    // --- SEND ACCEPT ---
                    {
                        ServerToClientPacket packet = { PacketType::UpdateClientState };
                        u32 client_id = allocate_client(from);
                        u32 bytes_header = sizeof(ServerToClientPacket);
                        u32 bytes_payload = sizeof(ClientState);
                        u32 bytes_to_send = bytes_header + bytes_payload;

                        memcpy(net_buffer, &packet, bytes_header);
                        memcpy(net_buffer + bytes_header, &clients[client_id], bytes_payload);

                        if (net_send(&net_socket, from, &net_buffer, bytes_to_send) < 0) {
                            log_print(LOG_ERR, "tried to accept client %i:%i but failed.", from.host, from.port);
                            deallocate_client(client_id);
                            break;
                        }
                        
                        log_print(LOG_INF, "Added client %i:%i!", from.host, from.port);

                    }

                    // --- SEND STATIC DATA ---
                    ServerToClientPacket packet = {
                        .type = PacketType::UpdateStaticThings,
                        .things_count = 0,
                        .static_things_count = static_things_count,
                    };
                    u32 header_bytes = sizeof(ServerToClientPacket);
                    u32 static_thing_bytes = static_things_count * sizeof(StaticThing);
                    u32 bytes = header_bytes + static_thing_bytes;

                    // Copy in packet
                    memcpy(net_buffer, &packet, header_bytes);

                    // Copy in static data
                    if (static_things_count > 0) {
                        memcpy(net_buffer + header_bytes, static_things, static_thing_bytes);
                    }

                    int sent = net_send(&net_socket, from, net_buffer, bytes);
                    if (sent != (int)bytes) {
                        log_print(LOG_ERR, "failed to send static data packet: sent=%i expected=%u", sent, bytes);
                    }

                    break;
                }
                case PacketType::Disconnect: {
                    u32 foundAt = IDX_NIL;
                    for (u32 i = 1; i < clients_count; i++) {
                        if (clients[i].address.host == from.host && clients[i].address.port == from.port) {
                            foundAt = true;
                            break;
                        }
                    }
                    if (foundAt == IDX_NIL) {
                        log_print(LOG_ERR, "tried remove client %i:%i but failed to find in clients", from.host, from.port);
                        break;
                    }

                    clients[foundAt].status = ClientStatus::Nil;
                    // TODO Also reuse clients, and cleanup state from player.
                    log_print(LOG_INF, "Disconnected client %i:%i!", from.host, from.port);

                    break;
                }
                case PacketType::KeepAlive: {
                    bool found = false;
                    for (u32 i = 1; i < clients_count; i++) {
                        if (clients[i].address.host == from.host && clients[i].address.port == from.port) {
                            clients[i].last_seen = now_millis();
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        log_print(LOG_ERR, "tried to update keepalive %i:%i but failed to find in clients", from.host, from.port);
                    }

                    break;
                }
                case PacketType::UpdateClientState: {
                    u32 header_bytes = sizeof(ClientToServerPacket);
                    u32 bytes_expected = header_bytes;
                    if (bytes_received != bytes_expected) {
                        log_print(LOG_ERR, "Malformed ClientState packet");
                        continue;
                    }

                    // Read header
                    ClientToServerPacket packet = {};
                    memcpy(&packet, net_buffer, header_bytes);
                    if (packet.type != PacketType::UpdateClientState) {
                        log_print(LOG_ERR, "Received wrong packet type on first connect type=%d", packet.type);
                        break;
                    }

                    if (packet.client_idx == 0) {
                        log_print(LOG_ERR, "Received packet from nil client", packet.type);
                        break;
                    }

                    // Update values
                    clients[packet.client_idx].camera_yaw = packet.camera_yaw;
                    clients[packet.client_idx].camera_pitch = packet.camera_pitch;

                    // Figure out the new btn state
                    u32 old_btn_state = clients[packet.client_idx].btn_state;
                    u32 new_btn_state = packet.btn_state;

                    // Stigende flanksteik
                    clients[packet.client_idx].btn_pressed |= new_btn_state & ~old_btn_state;
                    clients[packet.client_idx].btn_state = new_btn_state;

                    if (clients[packet.client_idx].btn_state & InputButton_FireA) {
                        log_print(LOG_INF, "fired A from client %d", packet.client_idx);
                    }

                    break;
                }
            }
        } else {
            break;
        }
    }
}

void loop_send_messages(f32 delta) {
    // Send game state to clients
    for (u32 i = 1; i < clients_count; i++) {
        ClientState *client = &clients[i];
        if (client->status != ClientStatus::Live) continue;

        // TODO Check if any of the clients have been idle for too long

        u32 header_bytes = sizeof(ServerToClientPacket);
        u32 thing_bytes = things_count * sizeof(Thing);
        u32 bytes = header_bytes + thing_bytes;

        // Validation
        if (things_count > MAX_THINGS) {
            log_print(LOG_ERR, "things_count exceeds MAX_THINGS: %u > %u", things_count, MAX_THINGS);
            continue;
        }
        if (things_count > (MAX_NET_BUFFER_BYTES - header_bytes) / sizeof(Thing)) {
            log_print(LOG_ERR, "state packet too large for things");
            continue;
        }
        if (bytes >= MAX_UDP_PACKET_BYTES - 256) {
            log_print(LOG_ERR, "state packet too large for one udp packet");
            continue;
        }

        ServerToClientPacket packet = {
            PacketType::UpdateThings,
            things_count,
        };

        // Copy the actual data
        memcpy(net_buffer, &packet, header_bytes);
        if (things_count > 0) memcpy(net_buffer + header_bytes, things, thing_bytes);

        // Send it!
        int sent = net_send(&net_socket, client->address, net_buffer, bytes);
        if (sent != (int)bytes) {
            log_print(LOG_ERR, "failed to send state packet: sent=%i expected=%u", sent, bytes);
        }
    }
}

int main() {
    printf("starting game server\n");
    
    net_init();
    net_socket_open(&net_socket, SNORTAL_PORT);
    net_socket_set_nonblocking(&net_socket);
    loop_init();

    double last_time = now_seconds();
    while(true) {
        double start = now_seconds();
        double delta = start - last_time;
        last_time = start;

        loop_read_messages(delta);
        loop_sim(delta);
        loop_send_messages(delta);

        // Here we simply try to use up the remaining time either by busy waiting or sleeping
        double target_end = start + TICK_TIME;
        while (true) {
            double now = now_seconds();
            double sleep_time = target_end - now;

            if (sleep_time <= 0.0) break;

            if (sleep_time > 0.020) {
                sleep_seconds(sleep_time - 0.010);
            }
        }

        double end = now_seconds();
        double frame_time = end - start;

        if (frame_time > TICK_TIME * 1.25) {
            log_print(LOG_WRN, "frame_time is getting too high: %f", frame_time);
        }

        // Advance frame count
        frame_count++;

        #ifdef _DEBUG
        FrameMark;
        #endif
    }
}