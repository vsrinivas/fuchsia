// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include <xdc-host-utils/client.h>
#include <xdc-host-utils/conn.h>

namespace xdc {

zx_status_t GetStream(uint32_t stream_id, fbl::unique_fd& out_fd) {
    fbl::unique_fd fd(socket(AF_UNIX, SOCK_STREAM, 0));
    if (!fd) {
        fprintf(stderr, "Could not create socket, err: %s\n", strerror(errno));
        return ZX_ERR_IO;
    }
    // Connect to the host xdc server.
    struct sockaddr_un server = {};
    server.sun_family = AF_UNIX;
    strncpy(server.sun_path, XDC_SOCKET_PATH, sizeof(server.sun_path));
    if (connect(fd.get(), (struct sockaddr *)&server, sizeof(server)) < 0) {
        fprintf(stderr, "Could not connect to server: %s, err: %s\n",
                XDC_SOCKET_PATH, strerror(errno));
        return ZX_ERR_IO;
    }
    // Register the stream id.
    ssize_t n = send(fd.get(), &stream_id, sizeof(stream_id), MSG_WAITALL);
    if (n != sizeof(stream_id)) {
        fprintf(stderr, "Write failed, expected %lu written, got %ld, err: %s\n",
                sizeof(stream_id), n, strerror(errno));
        return ZX_ERR_IO;
    }
    // Wait for the server registration response.
    RegisterStreamResponse connected_resp;
    n = recv(fd.get(), &connected_resp, sizeof(connected_resp), MSG_WAITALL);
    if (n != sizeof(connected_resp)) {
        fprintf(stderr, "Read failed, expected %lu read, got %ld, err: %s\n",
                sizeof(connected_resp), n, strerror(errno));
        return ZX_ERR_IO;
    }
    if (!connected_resp) {
        fprintf(stderr, "Stream id %u was already taken, exiting\n", stream_id);
        return ZX_ERR_ALREADY_BOUND;
    }
    out_fd = fbl::move(fd);
    return ZX_OK;
}

}  // namespace xdc
