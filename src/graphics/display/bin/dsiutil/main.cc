// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/hardware/dsi/llcpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fidl/llcpp/buffer_then_heap_allocator.h>
#include <lib/fidl/llcpp/memory.h>
#include <lib/fidl/llcpp/vector_view.h>
#include <lib/mipi-dsi/mipi-dsi.h>
#include <lib/zx/channel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/fidl.h>
#include <zircon/types.h>

#include <cstdint>
#include <cstdlib>

namespace {

void usage(char* argv[]) {
  printf("\n%s [on/off][path]\n\n", argv[0]);
  printf("Options: on: Turns LCD on using DCS Command 0x29 (default)\n");
  printf("         off: Turns LCD off using DCS Command 0x28\n");
  printf("         path: Path to dsi-base interface (/dev/class/dsi-base/000)\n\n");
}

namespace fidl_dsi = ::llcpp::fuchsia::hardware::dsi;

}  // namespace
int main(int argc, char* argv[]) {
  const char* path = "/dev/class/dsi-base/000";
  const char* on_off = "on";
  zx::channel local, remote;
  if (argc == 1 || argc > 3) {
    usage(argv);
    return -1;
  }

  on_off = argv[1];
  if (strcmp(argv[1], "on") != 0 && strcmp(argv[1], "off") != 0) {
    usage(argv);
    return -1;
  }

  if (argc == 3) {
    path = argv[2];
  }

  zx_status_t status = zx::channel::create(0, &local, &remote);
  if (status != ZX_OK) {
    printf("Could not create channel (%d)\n", status);
    return -1;
  }

  status = fdio_service_connect(path, remote.release());
  if (status != ZX_OK) {
    printf("Failed to connect to dsi-base %d\n", status);
    return -1;
  }
  fidl_dsi::DsiBase::SyncClient client(std::move(local));

  uint8_t tbuf[1];
  if (strcmp(argv[1], "off") == 0) {
    tbuf[0] = 0x28;
  } else {
    tbuf[0] = 0x29;
  }
  fidl::BufferThenHeapAllocator<2048> allocator;
  auto res = mipi_dsi::MipiDsi::CreateCommandFidl(sizeof(tbuf), 0, true, &allocator);

  auto response = client.SendCmd(
      std::move(res.value()), fidl::VectorView<uint8_t>{fidl::unowned_ptr(tbuf), std::size(tbuf)});

  if (!response.ok()) {
    printf("Could not send command to DSI (%s)\n", response.error());
    return -1;
  }

  if (response.value().result.is_err()) {
    printf("Invalid Command Sent (%d)\n", response.value().result.err());
    return -1;
  }

  return 0;
}
