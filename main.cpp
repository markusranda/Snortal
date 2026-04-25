#include <stdio.h>
#include <math.h>
#include "raylib.h"

inline Vector3 operator+(Vector3 a,float b){return{a.x+b,a.y+b,a.z+b};}
inline Vector3 operator+(Vector3 a,Vector3 b){return{a.x+b.x,a.y+b.y,a.z+b.z};}
inline Vector3 operator-(Vector3 a,Vector3 b){return{a.x-b.x,a.y-b.y,a.z-b.z};}
inline Vector3 operator-(Vector3 v){return{-v.x,-v.y,-v.z};}
inline Vector3 operator*(Vector3 v,float s){return{v.x*s,v.y*s,v.z*s};}
inline Vector3 operator*(float s,Vector3 v){return{v.x*s,v.y*s,v.z*s};}
inline Vector3 operator*(Vector3 a,Vector3 b){return{a.x*b.x,a.y*b.y,a.z*b.z};}
inline Vector3 operator/(Vector3 v,float s){return{v.x/s,v.y/s,v.z/s};}
inline Vector3& operator+=(Vector3& a,Vector3 b){a.x+=b.x;a.y+=b.y;a.z+=b.z;return a;}
inline Vector3& operator-=(Vector3& a,Vector3 b){a.x-=b.x;a.y-=b.y;a.z-=b.z;return a;}
inline Vector3& operator*=(Vector3& v,float s){v.x*=s;v.y*=s;v.z*=s;return v;}
inline Vector3& operator/=(Vector3& v,float s){v.x/=s;v.y/=s;v.z/=s;return v;}

#define SCREEN_WIDTH 1024 
#define SCREEN_HEIGHT 640 

struct Player {
    Vector3 pos;
    Vector3 vel;
    float   camera_sensitivity;
    float   camera_smoothness;
    float   camera_yaw;         // Turning left/right around y axis
    float   camera_pitch;       // Turning up/down around x axis
    float   camera_render_yaw;
    float   camera_render_pitch;
};

Camera3D camera = { 0 };
Player player = {};

void init() {
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "SNORTAL");
    DisableCursor();

    camera.position = { 100.0f, 100.0f, 100.0f };
    camera.target = {50.0f, 50.0f, 25.0f};
    camera.up = { 0.0f, 1.0f, 0.0f };
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    player.vel = {100.0f, 100.0f, 100.0f};
    player.camera_sensitivity = 0.05f;
    player.camera_smoothness = 80.0f;
}

void loop_sim() {
    float delta = GetFrameTime();

    // Update player movement
    float x_dir = 0.0f;
    float z_dir = 0.0f;
    if (IsKeyDown(KEY_W)) z_dir =  1.0f; 
    if (IsKeyDown(KEY_A)) x_dir = -1.0f;
    if (IsKeyDown(KEY_S)) z_dir = -1.0f;
    if (IsKeyDown(KEY_D)) x_dir =  1.0f;
    
    Vector2 mouseDelta = GetMouseDelta();
    player.camera_yaw   -= mouseDelta.x * player.camera_sensitivity;
    player.camera_pitch -= mouseDelta.y * player.camera_sensitivity;
    if (player.camera_pitch > 89.0f)  player.camera_pitch = 89.0f;
    if (player.camera_pitch < -89.0f) player.camera_pitch = -89.0f;

    float smooth = 1.0f - expf(-player.camera_smoothness * delta);
    player.camera_render_yaw   += (player.camera_yaw   - player.camera_render_yaw)   * smooth;
    player.camera_render_pitch += (player.camera_pitch - player.camera_render_pitch) * smooth;

    // Figure out forward and right direction after converting to rads
    float yaw_rad        = player.camera_yaw   * DEG2RAD;
    float pitch_rad      = player.camera_pitch * DEG2RAD;
    float yaw_look_rad   = player.camera_render_yaw   * DEG2RAD;
    float pitch_look_rad = player.camera_render_pitch * DEG2RAD;
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
    
    float cube_size = 50.0f;
    DrawCube({50.0f, 50.0f, 25.0f}, cube_size, cube_size, cube_size, RED);

    DrawPlane({0.0f, 0.0f, 0.0f}, {1024.0f, 1024.0f}, GRAY);

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