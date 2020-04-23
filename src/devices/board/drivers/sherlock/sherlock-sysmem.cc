// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/device/sysmem.h>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>
#include <soc/aml-t931/t931-gpio.h>
#include <soc/aml-t931/t931-hw.h>

#include "sherlock.h"

namespace sherlock {

static const pbus_bti_t sysmem_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_SYSMEM,
    },
};

static const sysmem_metadata_t sysmem_metadata = {
    .vid = PDEV_VID_AMLOGIC,
    .pid = PDEV_PID_AMLOGIC_T931,
    // On sherlock there are two protected memory ranges.  The protected_memory_size field
    // configures the size of the non-VDEC range.  In contrast, the VDEC range is configured and
    // allocated via the TEE, and is currently 7.5 MiB (on astro; to be verified on sherlock).  The
    // VDEC range is a fixed location within the overall optee reserved range passed to Zircon
    // during boot - the specific location is obtained by sysmem calling the secmem TA via
    // fuchsia::sysmem::Tee protocol between sysmem and TEE Controller.
    .protected_memory_size = 128 * 1024 * 1024,
    // Support h.264 5.1, which has a max DPB size of 70,778,880 bytes (with NV12), and add some
    // extra size for additional pictures for buffering and several framebuffers (1024*608*4 bytes
    // each), for a total of 100 MiB.
    // Also include enough space for 0.5 seconds of camera streaming at 2720*2176*1.5 bytes @ 30Hz,
    // for a total of 127 MiB.
    // TODO(39523): revisit contiguous memory requirements for camera
    .contiguous_memory_size = (100 + 127) * 1024 * 1024,
};

static const pbus_metadata_t sysmem_metadata_list[] = {
    {
        .type = SYSMEM_METADATA,
        .data_buffer = &sysmem_metadata,
        .data_size = sizeof(sysmem_metadata),
    },
};

static const pbus_dev_t sysmem_dev = [] {
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

zx_status_t Sherlock::SysmemInit() {
  zx_status_t status = pbus_.ProtocolDeviceAdd(ZX_PROTOCOL_SYSMEM, &sysmem_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: ProtocolDeviceAdd failed %d", __func__, status);
    return status;
  }

  return ZX_OK;
}

}  // namespace sherlock
