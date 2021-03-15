// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <lib/fdio/fd.h>
#include <lib/zx/channel.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/status.h>

#include <thread>

// This is a test executable to demonstrate that we can tear down a process
// cleanly even with background threads blocked in |accept|.
int main(int argc, char** argv) {
  zx::channel handle(zx_take_startup_handle(PA_HND(PA_USER0, 0)));
  if (!handle.is_valid()) {
    printf("failed to take startup handle\n");
    return 1;
  }

  int fd;
  zx_status_t status = fdio_fd_create(handle.get(), &fd);
  if (status != ZX_OK) {
    printf("failed to create file descriptor: %s\n", zx_status_get_string(status));
    return 1;
  }

  std::thread child([fd]() {
    struct sockaddr_storage addr;
    socklen_t len = sizeof(addr);
    int rv = accept(fd, reinterpret_cast<struct sockaddr*>(&addr), &len);

    // We should be blocked here. The FD table should have an entry reserved
    // for the socket we are accepting.
    printf("failed to block in accept: %s (rv=%d)\n", strerror(errno), rv);
    return rv;
  });

  // At this point, the child thread should spin up and get blocked in accept
  // waiting for the fake netstack to provide a socket. We need to simulate
  // enough of the netstack to leave that thread blocked in accept and also
  // unwind this process cleanly. This machinery is in the server.

  // We need to wait for the accept call to come in.
  status = handle.wait_one(ZX_USER_SIGNAL_0, zx::time::infinite(), nullptr);
  if (status != ZX_OK) {
    printf("failed to wait for accept call: %s\n", zx_status_get_string(status));
    return 1;
  }

  status = handle.signal_peer(0, ZX_USER_SIGNAL_0);
  if (status != ZX_OK) {
    printf("failed to signal peer: %s\n", zx_status_get_string(status));
    return 1;
  }

  child.detach();

  // At this point, we have the process in the state we want, with a reserved
  // entry in the FD table. We now want to unwind the process to prove that
  // we can cleanly unwind a process with a reserved entry in its FD table.
  //
  // To unwind cleanly, we implement Close on the server, which will be called
  // by the atexit logic, which would otherwise block.
  //
  // Now we try to unwind the process cleanly while the child thread is
  // blocked in accept. The test passes if we do not crash while exiting
  // the process.

  return 0;
}
