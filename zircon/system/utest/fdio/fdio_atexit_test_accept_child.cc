// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/net/c/fidl.h>
#include <lib/fdio/fd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <threads.h>
#include <unistd.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

typedef struct context {
    int fd;
} context_t;

static int block_in_accept(void* ptr) {
    context_t* ctx = (context_t*)ptr;
    struct sockaddr_storage addr;
    memset(&addr, 0, sizeof(addr));
    socklen_t len = sizeof(addr);
    int rv = accept(ctx->fd, (struct sockaddr*)&addr, &len);

    // We should be blocked here. The FD table should have an entry reserved
    // for the socket we are accepting.
    printf("failed to block in accept\n");
    return rv;
}

static zx_handle_t g_server;

static zx_status_t server_reply(fidl_txn_t* txn, const fidl_msg_t* msg) {
    return zx_socket_write(g_server, ZX_SOCKET_CONTROL,
                           msg->bytes, msg->num_bytes, NULL);
}

// This is a test executable to demonstrate that we can tear down a process
// cleanly even with background threads blocked in |accept|.
int main(int argc, char** argv) {
    zx_handle_t client = ZX_HANDLE_INVALID;
    zx_status_t status = zx_socket_create(
        ZX_SOCKET_STREAM | ZX_SOCKET_HAS_CONTROL | ZX_SOCKET_HAS_ACCEPT,
        &client, &g_server);
    if (status != ZX_OK) {
        printf("failed to create socket: %d (%s)\n", status,
               zx_status_get_string(status));
        return 1;
    }

    int fd = -1;
    status = fdio_fd_create(client, &fd);
    if (status != ZX_OK) {
        printf("failed to create file descriptor: %d (%s)\n",
               status, zx_status_get_string(status));
        return 1;
    }

    context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.fd = fd;
    thrd_t child;
    memset(&child, 0, sizeof(child));
    thrd_create(&child, block_in_accept, &ctx);

    // At this point, the child thread should spin up and get blocked in accept
    // waiting for the fake netstack to provide a socket. We need to simulate
    // enough of the netstack to leave that thread blocked in accept and also
    // unwind this process cleanly.

    // The first thing we do is service the fuchsia.net.SocketControl/Accept
    // method.
    status = zx_object_wait_one(g_server, ZX_SOCKET_CONTROL_READABLE,
                                ZX_TIME_INFINITE, NULL);
    if (status != ZX_OK) {
        printf("failed to wait for ZX_SOCKET_CONTROL_READABLE: %d (%s)\n",
               status, zx_status_get_string(status));
        return 1;
    }

    // We read out the fuchsia.net.SocketControl/Accept request and ignore it.
    char buffer[1024];
    status = zx_socket_read(g_server, ZX_SOCKET_CONTROL, buffer, sizeof(buffer),
                            NULL);
    if (status != ZX_OK) {
        printf("failed to read accept control message: %d (%s)\n",
               status, zx_status_get_string(status));
        return 1;
    }

    fidl_txn_t txn = {
        .reply = server_reply,
    };

    // Next, we write a reply to that message in the control plane that causes
    // child thread to sit waiting for the ZX_SOCKET_ACCEPT signal on the
    // client end of the socket. That signal will never be asserted, so the
    // thread will happily sit there forever.
    status = fuchsia_net_SocketControlAccept_reply(&txn, 0);
    if (status != ZX_OK) {
        printf("failed to write accept reply: %d (%s)\n",
               status, zx_status_get_string(status));
        return 1;
    }

    // At this point, we have the process in the state we want, with a reserved
    // entry in the FD table. We now want to unwind the process to prove that
    // we can cleanly unwind a process with a reserved entry in its FD table.
    //
    // Unfortunately, unwinding the process will generate a
    // fuchsia.net.SocketControl/Close on the open file descriptor. We need to
    // keep the file descriptor alive so that the child thread will continue
    // to sit waiting for ZX_SOCKET_ACCEPT.
    //
    // To unwind cleanly, we buffer a reply to the Close message in the control
    // plane of the socket. This will cause the atexit logic to unwind the
    // process correctly without blocking.

    // Before we can buffer the control message, we need to wait for the child
    // thread to read out the previous control message.
    status = zx_object_wait_one(g_server, ZX_SOCKET_CONTROL_WRITABLE,
                                ZX_TIME_INFINITE, NULL);
    if (status != ZX_OK) {
        printf("failed to wait for ZX_SOCKET_CONTROL_WRITABLE: %d (%s)\n",
               status, zx_status_get_string(status));
        return 1;
    }

    status = fuchsia_net_SocketControlClose_reply(&txn, 0);
    if (status != ZX_OK) {
        printf("failed to write close reply: %d (%s)\n",
               status, zx_status_get_string(status));
        return 1;
    }

    // Now we try to unwind the process cleanly while the child thread is
    // blocked in accept. The test passes if we do not crash while exiting
    // the process.

    return 0;
}
