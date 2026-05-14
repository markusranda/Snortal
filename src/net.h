#pragma once
#include "base_num.h"

#define SNORTAL_PORT 52777

struct NetAddress {
    u32 host; // IPv4, host byte order
    u16 port; // Host byte order
};

struct NetSocket {
    uintptr_t handle;
};

bool net_init();
void net_shutdown();

NetAddress net_address(u8 a, u8 b, u8 c, u8 d, u16 port);
void net_address_string(NetAddress address, char *out, u32 out_len);

bool net_socket_open(NetSocket *socket, u16 port);
void net_socket_close(NetSocket *socket);

bool net_socket_set_nonblocking(NetSocket *socket);

i32 net_send(NetSocket *socket, NetAddress address, const void *data, i32 size);
i32 net_receive(NetSocket *socket, NetAddress *from, void *buffer, i32 buffer_size);