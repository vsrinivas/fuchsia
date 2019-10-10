// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/virtualconsole/c/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/zx/channel.h>
#include <lib/zx/time.h>
#include <stdio.h>
#include <sys/types.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <zxtest/zxtest.h>

static zx_status_t connect_to_service(const char* service, zx::channel* channel_out) {
  zx::channel channel_local, channel_remote;
  zx_status_t status = zx::channel::create(0, &channel_local, &channel_remote);
  if (status != ZX_OK) {
    fprintf(stderr, "vc-intergration-test: failed to create channel: %s\n",
            zx_status_get_string(status));
    return status;
  }

  status = fdio_service_connect(service, channel_remote.release());
  if (status != ZX_OK) {
    fprintf(stderr, "vc-integration-test: failed to connect to service: %s\n",
            zx_status_get_string(status));
    return status;
  }

  *channel_out = std::move(channel_local);
  return ZX_OK;
}

TEST(VirtualConsole, HasPrimaryConnected) {
  int fd = open("/dev/class/display-controller/000", O_RDWR);
  if (fd < 0) {
    fprintf(stderr, "Skipping test because device has no display controller driver\n");
    return;
  }
  close(fd);
  zx::channel session_manager;
  EXPECT_OK(connect_to_service("/svc/fuchsia.virtualconsole.SessionManager", &session_manager));

  constexpr uint32_t kRetryCount = 50;
  constexpr uint32_t kRetryDelaySeconds = 1;
  bool connected = false;
  for (uint32_t i = 0; i < kRetryCount; i++) {
    // Retry because it might take some time for the display to appear on
    // startup.
    EXPECT_OK(fuchsia_virtualconsole_SessionManagerHasPrimaryConnected(session_manager.get(),
                                                                       &connected));
    if (connected)
      break;
    zx::nanosleep(zx::deadline_after(zx::sec(kRetryDelaySeconds)));
  }

  // If this fails, either there's a bug in virtcon/the display driver, or there
  // isn't actually a display connected.
  EXPECT_TRUE(connected);
}
