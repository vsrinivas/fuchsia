// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>
#include <zircon/device/sysmem.h>

#include "astro.h"

namespace astro {

static const pbus_bti_t sysmem_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_SYSMEM,
    },
};

static const sysmem_metadata_t sysmem_metadata = {
    .vid = PDEV_VID_AMLOGIC,
    .pid = PDEV_PID_AMLOGIC_S905D2,
    .protected_memory_size = 16 * 1024 * 1024,
    // Support h.264 5.1, which has a max DPB size of 70,778,880 bytes (with NV12), and add some
    // extra size for additional pictures for buffering and several framebuffers (1024*608*4 bytes
    // each).
    //
    // For now, if we were to support 16 VP9 frames at 4096x2176 (* 3 / 2 for NV12), we'd need 204
    // MiB, plus more for several framebuffers (1024*608*4 bytes each), for a total of ~256 MiB.
    //
    // TODO(dustingreen): Plumb actual frame counts in the VP9 and h.264 decoders, so that the
    // decoder doesn't demand so much RAM.  For the moment, avoid increasing the reserved contig RAM
    // beyond 100 MiB, which means we won't be able to decode larger VP9 decode conformance streams
    // yet, but that's ok for now.
    .contiguous_memory_size = 100 * 1024 * 1024,
};

static const pbus_metadata_t sysmem_metadata_list[] = {{
    .type = SYSMEM_METADATA,
    .data_buffer = &sysmem_metadata,
    .data_size = sizeof(sysmem_metadata),
}};

static const pbus_dev_t sysmem_dev = []() {
  pbus_dev_t ret = {};
  ret.name = "sysmem";
  ret.vid = PDEV_VID_GENERIC;
  ret.pid = PDEV_PID_GENERIC;
  ret.did = PDEV_DID_SYSMEM;
  ret.bti_list = sysmem_btis;
  ret.bti_count = countof(sysmem_btis);
  ret.metadata_list = sysmem_metadata_list;
  ret.metadata_count = countof(sysmem_metadata_list);
  return ret;
}();

zx_status_t Astro::SysmemInit() {
  zx_status_t status = pbus_.ProtocolDeviceAdd(ZX_PROTOCOL_SYSMEM, &sysmem_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: ProtocolDeviceAdd failed %d\n", __func__, status);
    return status;
  }

  return ZX_OK;
}

}  // namespace astro
