#include <chrono>
#include "base.h"
#include "net.h"

// --- PROFILING ---
#ifdef _DEBUG
#include "tracy/Tracy.hpp"
#endif

// ======================================= FORWARD DECLARATIONS ================================
// ======================================= CONSTS ==============================================

#define TICK_TIME 0.0166666666 // 60FPS

// ======================================= DATASTRUCTURES ======================================

// ======================================= STATE ===============================================

char buffer[1024];
NetSocket net_socket = {};

// ======================================= HELPERS =============================================

void sleep_seconds(double seconds) {
    std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
}

double now_seconds() {
    using clock = std::chrono::steady_clock;
    return std::chrono::duration<double>(clock::now().time_since_epoch()).count();
}

// ======================================= MAIN FUNCS ==========================================


int main() {
    printf("Starting game server\n");
    
    net_init();
    net_socket_open(&net_socket, SNORTAL_PORT);
    net_socket_set_nonblocking(&net_socket);

    double last_time = now_seconds();

    while(true) {
        // Start loop
        double start = now_seconds();
        double delta = start - last_time;
        last_time = start;

        // Read all messages
        while (true) {
            NetAddress from = {};
    
            int bytes = net_receive(&net_socket, &from, buffer, sizeof(buffer));

            if (bytes > 0) {
                printf("I got a pacakge!\n");
            } else {
                break;
            }
        }

        // End loop
        double end = now_seconds();
        double frame_time = end - start;
        double sleep_time = TICK_TIME - frame_time;
        if (sleep_time > 0.0) {
            sleep_seconds(sleep_time);
        }
    }   
}