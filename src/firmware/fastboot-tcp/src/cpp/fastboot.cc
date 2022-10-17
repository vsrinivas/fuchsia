// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "fastboot.h"

#include <lib/fastboot/fastboot.h>
#include <zircon/syscalls.h>

#include "transport.h"

__EXPORT
int fastboot_process(size_t packet_size, int (*read_packet_callback)(void *, size_t, void *),
                     int (*write_packet_callback)(const void *, size_t, void *), void *ctx) {
  static fastboot::Fastboot *fastboot = nullptr;
  if (!fastboot) {
    // Use the total system memory as an optimistic max download size. Actual
    // download buffer is dynamically allocated when executing the download
    // command based on available memory and released after.
    //
    // Since the life time of the object is the same as the component, we simply
    // create it once and don't delete it.
    fastboot = new fastboot::Fastboot(zx_system_get_physmem());
  }

  FastbootTCPTransport transport(ctx, packet_size, read_packet_callback, write_packet_callback);
  zx::result<> ret = fastboot->ProcessPacket(&transport);
  return ret.is_ok() ? 0 : 1;
}
