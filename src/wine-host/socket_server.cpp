/*
 * Copyright (C) 2026
 * VST3 Bridge - Wine VST3 Host Bridge
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */

/**
 * @file socket_server.cpp
 * @brief WineSocketClient implementation — AF_UNIX via Winsock2
 *
 * Wine supports AF_UNIX domain sockets through Winsock2. This lets the Wine
 * host connect directly to the native side's Unix domain socket without going
 * through TCP, keeping latency low and avoiding port conflicts.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <cstring>

#include "socket_server.h"
#include "wine_socket_client.h"
#include "protocol.h"

// AF_UNIX and sockaddr_un are available in Wine's Winsock2 but may not be
// declared in older cross-compile headers.
#ifndef AF_UNIX
#define AF_UNIX 1
#endif

struct sockaddr_un_wine {
    ADDRESS_FAMILY sun_family;   // AF_UNIX = 1
    char           sun_path[108];
};

namespace vst3bridge {

// ---------------------------------------------------------------------------
// connect / close
// ---------------------------------------------------------------------------

bool WineSocketClient::connect(const std::string& path) {
    WSADATA wsa{};
    WSAStartup(MAKEWORD(2, 2), &wsa);  // safe to call multiple times

    socket_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (socket_ == INVALID_SOCKET) {
        return false;
    }

    sockaddr_un_wine addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

    if (::connect(socket_,
                  reinterpret_cast<const sockaddr*>(&addr),
                  sizeof(addr)) != 0) {
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
        return false;
    }

    return true;
}

void WineSocketClient::close() {
    if (socket_ != INVALID_SOCKET) {
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
    }
}

// ---------------------------------------------------------------------------
// Low-level send / recv helpers
// ---------------------------------------------------------------------------

bool WineSocketClient::sendAll(const void* data, size_t size) noexcept {
    const char* p = static_cast<const char*>(data);
    while (size > 0) {
        int n = ::send(socket_, p, static_cast<int>(size), 0);
        if (n <= 0) return false;
        p    += n;
        size -= static_cast<size_t>(n);
    }
    return true;
}

bool WineSocketClient::recvAll(void* data, size_t size) noexcept {
    char* p = static_cast<char*>(data);
    while (size > 0) {
        int n = ::recv(socket_, p, static_cast<int>(size), 0);
        if (n <= 0) return false;
        p    += n;
        size -= static_cast<size_t>(n);
    }
    return true;
}

// ---------------------------------------------------------------------------
// Message send / receive
// ---------------------------------------------------------------------------

bool WineSocketClient::sendMessage(MsgType type,
                                    const void* payload,
                                    size_t payloadSize) noexcept {
    if (socket_ == INVALID_SOCKET) return false;

    MessageHeader hdr{};
    hdr.magic        = MessageHeader::kMagic;
    hdr.version      = PROTOCOL_VERSION;
    hdr.type         = type;
    hdr.payload_size = static_cast<uint32_t>(payloadSize);
    hdr.timestamp    = 0;

    if (!sendAll(&hdr, sizeof(hdr))) return false;
    if (payloadSize > 0 && payload) {
        return sendAll(payload, payloadSize);
    }
    return true;
}

bool WineSocketClient::receiveMessage(GenericMessage& msg,
                                       int timeoutMs) noexcept {
    if (socket_ == INVALID_SOCKET) return false;

    if (timeoutMs > 0) {
        DWORD tv = static_cast<DWORD>(timeoutMs);
        setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO,
                   reinterpret_cast<const char*>(&tv), sizeof(tv));
    }

    MessageHeader hdr{};
    if (!recvAll(&hdr, sizeof(hdr))) return false;

    if (hdr.magic != MessageHeader::kMagic || hdr.version != PROTOCOL_VERSION) {
        return false;
    }

    msg.header = hdr;

    if (hdr.payload_size > 0) {
        msg.payload.resize(hdr.payload_size);
        if (!recvAll(msg.payload.data(), hdr.payload_size)) return false;
    } else {
        msg.payload.clear();
    }

    return true;
}

bool WineSocketClient::sendRaw(const void* data, size_t size) noexcept {
    return sendAll(data, size);
}

bool WineSocketClient::recvRaw(void* buf, size_t size) noexcept {
    return recvAll(buf, size);
}

} // namespace vst3bridge
