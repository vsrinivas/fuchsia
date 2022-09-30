// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <fuchsia/sysmem/c/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/platform-defs.h>

#include "x86.h"

namespace x86 {

namespace fpbus = fuchsia_hardware_platform_bus;

// This value is passed to bti_create as a marker; it does not have a particular
// meaning to anything in the system.
#define SYSMEM_BTI_ID 0x12341234UL

static const std::vector<fpbus::Bti> sysmem_btis = {
    {{
        .iommu_index = 0,
        .bti_id = SYSMEM_BTI_ID,
    }},
};

// On x86 not much is known about the display adapter or other hardware.
static const sysmem_metadata_t sysmem_metadata = {
    .vid = PDEV_VID_GENERIC,
    .pid = PDEV_PID_GENERIC,
    // no protected pool
    .protected_memory_size = 0,
    // -5 means 5% of physical RAM
    // we allocate a small amount of contiguous RAM to keep the sysmem tests from flaking,
    // see https://fxbug.dev/67703.
    .contiguous_memory_size = -5,
};

static const std::vector<fpbus::Metadata> sysmem_metadata_list = {
    {{
        .type = SYSMEM_METADATA_TYPE,
        .data = std::vector<uint8_t>(
            reinterpret_cast<const uint8_t*>(&sysmem_metadata),
            reinterpret_cast<const uint8_t*>(&sysmem_metadata) + sizeof(sysmem_metadata)),
    }},
};

static const fpbus::Node sysmem_dev = []() {
  fpbus::Node node;
  node.name() = "sysmem";
  node.vid() = PDEV_VID_GENERIC;
  node.pid() = PDEV_PID_GENERIC;
  node.did() = PDEV_DID_SYSMEM;
  node.bti() = sysmem_btis;
  node.metadata() = sysmem_metadata_list;
  return node;
}();

zx_status_t X86::SysmemInit() {
  fdf::Arena arena('X86S');
  fidl::Arena fidl_arena;
  auto status = pbus_.buffer(arena)->NodeAdd(fidl::ToWire(fidl_arena, sysmem_dev));
  if (!status.ok()) {
    zxlogf(ERROR, "%s: NodeAdd failed %s", __func__, status.FormatDescription().data());
    return status.status();
  }
  if (status->is_error()) {
    return status->error_value();
  }
  return ZX_OK;
}
}  // namespace x86
