// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/device/debug.h>

#include <errno.h>
#include <stdio.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <poll.h>
#include <unistd.h>

#include "xdc-server.h"

namespace xdc {

static constexpr uint32_t MAX_PENDING_CONN_BACKLOG = 128;
// The UNIX domain socket address for the host xdc server.
static const char* XDC_SOCKET_PATH = "/tmp/xdc";
static const char* XDC_LOCK_PATH   = "/tmp/xdc.lock";

//  Messages from / to clients registering a host stream id.
using RegisterStreamRequest  = uint32_t;
using RegisterStreamResponse = bool;

void Client::SetStreamId(uint32_t stream_id) {
    registered_ = true;
    stream_id_ = stream_id;
}

// static
std::unique_ptr<XdcServer> XdcServer::Create() {
    auto conn = std::make_unique<XdcServer>(ConstructorTag{});
    if (!conn->Init()) {
        return nullptr;
    }
    return conn;
}

bool XdcServer::Init() {
    socket_fd_.reset(socket(AF_UNIX, SOCK_STREAM, 0));
    if (!socket_fd_) {
        fprintf(stderr, "failed to create socket, err: %s\n", strerror(errno));
        return false;
    }
    sockaddr_un addr = {};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, XDC_SOCKET_PATH, sizeof(addr.sun_path));

    // Check if another instance of the xdc server is running.
    socket_lock_fd_.reset(open(XDC_LOCK_PATH, O_CREAT | O_RDONLY, 0666));
    if (!socket_lock_fd_) {
        return false;
    }
    int res = flock(socket_lock_fd_.get(), LOCK_EX | LOCK_NB);
    if (res != 0) {
        fprintf(stderr, "Failed to acquire socket lock, err: %s.\n", strerror(errno));
        return false;
    }
    // Remove the socket file if it exists.
    unlink(XDC_SOCKET_PATH);
    if (bind(socket_fd_.get(), (sockaddr *)&addr, sizeof(addr)) != 0) {
        fprintf(stderr, "Could not bind socket with pathname: %s, err: %s\n",
                XDC_SOCKET_PATH, strerror(errno));
        return false;
    }
    if (listen(socket_fd_.get(), MAX_PENDING_CONN_BACKLOG) < 0) {
        fprintf(stderr, "Could not listen on socket fd: %d, err: %s\n",
                socket_fd_.get(), strerror(errno));
        return false;
    }
    return true;
}

void XdcServer::Run() {
    printf("Waiting for connections on: %s\n", XDC_SOCKET_PATH);

    // Listen for new client connections.
    poll_fds_.push_back(pollfd{ socket_fd_.get(), POLLIN, 0 });

    // TODO(jocelyndang): listen for libusb events.

    for (;;) {
        // poll expects an array of pollfds.
        int num = poll(&poll_fds_[0], poll_fds_.size(), -1 /* timeout */);
        if (num < 0) {
            fprintf(stderr, "poll failed, err: %s\n", strerror(errno));
            break;
        }
        // Not using an iterator for poll_fds_ as we might add/remove elements.
        int num_sockets = poll_fds_.size();
        int i = 0;
        while (i < num_sockets) {
            if (poll_fds_[i].fd == socket_fd_.get()) {
                // A new client is trying to connect.
                if (poll_fds_[i].revents & POLLIN) {
                    ClientConnect();
                    // Don't need to increment num_sockets as there aren't poll events for it yet.
                }
            } else {
                auto iter = clients_.find(poll_fds_[i].fd);
                if (iter == clients_.end()) {
                    fprintf(stderr, "poll returned an unknown fd: %d\n", poll_fds_[i].fd);
                    poll_fds_.erase(poll_fds_.begin() + i);
                    --num_sockets;
                    continue;
                }

                std::shared_ptr<Client> client = iter->second;
                // Received client disconnect signal.
                bool delete_client = poll_fds_[i].revents & POLLHUP;
                // The client sent us some data.
                if (!delete_client && (poll_fds_[i].revents & POLLIN)) {
                    if (!client->registered()) {
                        // Delete the client if registering the stream failed.
                        delete_client = !RegisterStream(client);
                    }
                }
                if (delete_client) {
                    poll_fds_.erase(poll_fds_.begin() + i);
                    --num_sockets;
                    printf("fd %d stream %u disconnected\n", client->fd(), client->stream_id());
                    clients_.erase(iter);
                    continue;
                }
                // TODO(jocelyndang): handle client reads / writes.
            }
            ++i;
        }
    }
}

void XdcServer::ClientConnect() {
    struct sockaddr_un addr;
    socklen_t len = sizeof(addr);
    // Most of the time we want non-blocking transfers, so we can handle other clients / libusb.
    int client_fd = accept4(socket_fd_.get(), (struct sockaddr *)&addr, &len, SOCK_NONBLOCK);
    if (client_fd < 0) {
        fprintf(stderr, "Socket accept failed, err: %s\n", strerror(errno));
        return;
    }
    if (clients_.count(client_fd) > 0) {
        fprintf(stderr, "Client already connected, socket fd: %d\n", client_fd);
        return;
    }
    printf("Client connected, socket fd: %d\n", client_fd);
    clients_[client_fd] = std::make_shared<Client>(client_fd);
    poll_fds_.push_back(pollfd{ client_fd, POLLIN, 0 });
}

bool XdcServer::RegisterStream(std::shared_ptr<Client> client) {
    RegisterStreamRequest stream_id;
    ssize_t n = recv(client->fd(), &stream_id, sizeof(stream_id), MSG_WAITALL);
    if (n != sizeof(stream_id)) {
       fprintf(stderr, "failed to read stream id from client fd: %d, got len: %ld, got err: %s\n",
               client->fd(), n, strerror(errno));
       return false;
    }
    // Client has disconnected. This will be handled in the main poll thread.
    if (n == 0) {
        return false;
    }
    RegisterStreamResponse resp = false;
    if (stream_id == DEBUG_STREAM_ID_RESERVED) {
        fprintf(stderr, "cannot register stream id %u\n", DEBUG_STREAM_ID_RESERVED);
    } else if (GetClient(stream_id)) {
        fprintf(stderr, "stream id %u was already registered\n", stream_id);
    } else {
        client->SetStreamId(stream_id);
        printf("registered stream id %u\n", stream_id);
        resp = true;
    }

    ssize_t res = send(client->fd(), &resp, sizeof(resp), MSG_WAITALL);
    if (res != sizeof(resp)) {
        // Failed to send reply, disconnect the client.
        return false;
    }
    return resp;
}

std::shared_ptr<Client> XdcServer::GetClient(uint32_t stream_id) {
    auto is_client = [stream_id](auto& pair) -> bool {
        return pair.second->stream_id() == stream_id;
    };
    auto iter = std::find_if(clients_.begin(), clients_.end(), is_client);
    return iter == clients_.end() ? nullptr : iter->second;
}

}  // namespace xdc

int main(int argc, char** argv) {
    printf("Starting XHCI Debug Capability server...\n");
    std::unique_ptr<xdc::XdcServer> xdc_server = xdc::XdcServer::Create();
    if (!xdc_server) {
        return -1;
    }
    xdc_server->Run();
    return 0;
}
