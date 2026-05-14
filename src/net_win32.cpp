#include "net.h"
#include <stdio.h>

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>

static bool net_initialized = false;

bool net_init() {
    WSADATA data = {};
    int result = WSAStartup(MAKEWORD(2, 2), &data);
    net_initialized = result == 0;
    return net_initialized;
}

void net_shutdown() {
    if (net_initialized) {
        WSACleanup();
        net_initialized = false;
    }
}

NetAddress net_address(u8 a, u8 b, u8 c, u8 d, u16 port) {
    NetAddress address = {};
    address.host =
        ((u32)a << 24) |
        ((u32)b << 16) |
        ((u32)c << 8)  |
        ((u32)d);
    address.port = port;
    return address;
}

void net_address_string(NetAddress address, char *out, u32 out_len) {
    // 256.256.256.256:8080\0 => 21 bytes
    if (out_len < 21) return;

    u32 first_eight_bits = 0xFF;
    snprintf(out, out_len, "%d.%d.%d.%d:%d", 
        (address.host >> 24 & first_eight_bits), 
        (address.host >> 16 & first_eight_bits), 
        (address.host >> 8 & first_eight_bits), 
        (address.host & first_eight_bits), 
        address.port
    );
}

bool net_socket_open(NetSocket *socket, u16 port) {
    socket->handle = (uintptr_t)INVALID_SOCKET;

    SOCKET handle = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (handle == INVALID_SOCKET) return false;

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(handle, (sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(handle);
        return false;
    }

    socket->handle = (uintptr_t)handle;
    return true;
}

void net_socket_close(NetSocket *socket) {
    if (socket->handle != (uintptr_t)INVALID_SOCKET) {
        closesocket((SOCKET)socket->handle);
        socket->handle = (uintptr_t)INVALID_SOCKET;
    }
}

bool net_socket_set_nonblocking(NetSocket *socket) {
    u_long enabled = 1;
    return ioctlsocket((SOCKET)socket->handle, FIONBIO, &enabled) == 0;
}

// If no error occurs, sendto returns the total number of bytes sent, 
// which can be less than the number indicated by len. 
// Otherwise, a value of SOCKET_ERROR is returned, 
// and a specific error code can be retrieved by calling WSAGetLastError.
i32 net_send(NetSocket *socket, NetAddress address, const void *data, i32 size) {
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(address.host);
    addr.sin_port = htons(address.port);

    int sent = sendto(
        (SOCKET)socket->handle,
        (const char *)data,
        size,
        0,
        (sockaddr *)&addr,
        sizeof(addr)
    );

    if (sent == SOCKET_ERROR) {
        int error = WSAGetLastError();
        if (error == WSAEWOULDBLOCK) return 0;
        return -1;
    }

    return sent;
}

// If no error occurs, recvfrom returns the number of bytes received. 
// If the connection has been gracefully closed, the return value is zero. 
// Otherwise, a value of SOCKET_ERROR is returned, 
// and a specific error code can be retrieved by calling WSAGetLastError.
i32 net_receive(NetSocket *socket, NetAddress *from, void *buffer, i32 buffer_size) {
    sockaddr_in addr = {};
    int addr_len = sizeof(addr);

    int received = recvfrom(
        (SOCKET)socket->handle,
        (char *)buffer,
        buffer_size,
        0,
        (sockaddr *)&addr,
        &addr_len
    );

    if (received == SOCKET_ERROR) {
        int error = WSAGetLastError();
        if (error == WSAEWOULDBLOCK) return 0;
        return -1;
    }

    if (received == 0) return 0;

    if (from) {
        from->host = ntohl(addr.sin_addr.s_addr);
        from->port = ntohs(addr.sin_port);
    }

    return received;
}
#else

#error net.cpp currently only implements Windows

#endif