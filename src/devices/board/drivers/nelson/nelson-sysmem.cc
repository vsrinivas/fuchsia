// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <fuchsia/sysmem/c/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/platform-defs.h>

#include "nelson.h"

namespace nelson {
namespace fpbus = fuchsia_hardware_platform_bus;

static const std::vector<fpbus::Bti> sysmem_btis{
    {{
        .iommu_index = 0,
        .bti_id = BTI_SYSMEM,
    }},
};

static const sysmem_metadata_t sysmem_metadata = {
    .vid = PDEV_VID_AMLOGIC,
    .pid = PDEV_PID_AMLOGIC_S905D2,
    // On nelson there are two protected memory ranges.  The protected_memory_size field configures
    // the size of the non-VDEC range.  In contrast, the VDEC range is configured and allocated via
    // the TEE, and is currently 7.5 MiB.  The VDEC range is a fixed location within the overall
    // optee reserved range passed to Zircon during boot - the specific location is obtained by
    // sysmem calling the secmem TA via fuchsia::sysmem::Tee protocol between sysmem and TEE
    // Controller.
    .protected_memory_size = 32 * 1024 * 1024,
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

static const std::vector<fpbus::Metadata> sysmem_metadata_list{
    {{
        .type = SYSMEM_METADATA_TYPE,
        .data = std::vector<uint8_t>(
            reinterpret_cast<const uint8_t*>(&sysmem_metadata),
            reinterpret_cast<const uint8_t*>(&sysmem_metadata) + sizeof(sysmem_metadata)),
    }},
};

static const fpbus::Node sysmem_dev = []() {
  fpbus::Node ret = {};
  ret.name() = "sysmem";
  ret.vid() = PDEV_VID_GENERIC;
  ret.pid() = PDEV_PID_GENERIC;
  ret.did() = PDEV_DID_SYSMEM;
  ret.bti() = sysmem_btis;
  ret.metadata() = sysmem_metadata_list;
  return ret;
}();

zx_status_t Nelson::SysmemInit() {
  fidl::Arena<> fidl_arena;
  fdf::Arena arena('SYSM');
  auto result = pbus_.buffer(arena)->NodeAdd(fidl::ToWire(fidl_arena, sysmem_dev));
  if (!result.ok()) {
    zxlogf(ERROR, "%s: NodeAdd Sysmem(sysmem_dev) request failed: %s", __func__,
           result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "%s: NodeAdd Sysmem(sysmem_dev) failed: %s", __func__,
           zx_status_get_string(result->error_value()));
    return result->error_value();
  }

  return ZX_OK;
}

}  // namespace nelson
