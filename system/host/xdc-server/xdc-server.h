// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/unique_fd.h>
#include <map>
#include <poll.h>
#include <vector>

namespace xdc {

class Client {
public:
    explicit Client(int fd) : fd_(fd) {}

    void SetStreamId(uint32_t stream_id);

    int fd()             const { return fd_.get(); }
    bool registered()    const { return registered_; }
    uint32_t stream_id() const { return stream_id_; }

private:
    fbl::unique_fd fd_;

    bool     registered_ = false;
    uint32_t stream_id_  = 0;
};

class XdcServer {
    // This is required by the XdcServer constructor, to stop clients calling it directly.
    struct ConstructorTag { explicit ConstructorTag() = default; };

public:
    // Create should be called instead. This is public for make_unique.
    XdcServer(ConstructorTag tag) {}

    static std::unique_ptr<XdcServer> Create();
    void Run();

private:
    bool Init();

    // Processes new client connections on the server socket.
    void ClientConnect();

    // Returns whether registration succeeded.
    bool RegisterStream(std::shared_ptr<Client> client);

    // Returns the client registered to the given stream id, or nullptr if none was foumd.
    std::shared_ptr<Client> GetClient(uint32_t stream_id);

    // Server socket we receive client connections on.
    fbl::unique_fd socket_fd_;
    // File lock acquired to ensure we don't unlink the socket of a running xdc server instance.
    fbl::unique_fd socket_lock_fd_;

    // Maps from client socket file descriptor to client.
    std::map<int, std::shared_ptr<Client>> clients_;

    // File descriptors we are currently polling on.
    std::vector<pollfd> poll_fds_;
};

}  // namespace xdc
