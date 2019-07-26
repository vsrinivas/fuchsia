// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <unistd.h>

#include <fuchsia/kernel/c/fidl.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/directory.h>
#include <lib/zx/channel.h>
#include <unittest/unittest.h>
#include <zircon/syscalls.h>

namespace {

zx_status_t connect_to_service(const char* service, zx_handle_t* channel) {
  zx_handle_t channel_local, channel_remote;
  zx_status_t status = zx_channel_create(0, &channel_local, &channel_remote);
  if (status != ZX_OK) {
    fprintf(stderr, "failed to create channel: %d\n", status);
    return ZX_ERR_INTERNAL;
  }

  status = fdio_service_connect(service, channel_remote);
  if (status != ZX_OK) {
    zx_handle_close(channel_local);
    fprintf(stderr, "failed to connect to service: %d\n", status);
    return ZX_ERR_INTERNAL;
  }

  *channel = channel_local;
  return ZX_OK;
}

// Ask the kernel to run its unit tests.
bool run_kernel_unittests() {
  BEGIN_TEST;

  constexpr char command[] = "ut all";

  zx_handle_t channel;
  zx_status_t status = connect_to_service("/svc/fuchsia.kernel.DebugBroker", &channel);
  ASSERT_EQ(status, ZX_OK);

  zx_status_t call_status;
  status =
      fuchsia_kernel_DebugBrokerSendDebugCommand(channel, command, strlen(command), &call_status);
  zx_handle_close(channel);
  ASSERT_EQ(status, ZX_OK);
  ASSERT_EQ(call_status, ZX_OK);

  END_TEST;
}

}  // namespace

BEGIN_TEST_CASE(kernel_unittests)
RUN_TEST(run_kernel_unittests)
END_TEST_CASE(kernel_unittests)
