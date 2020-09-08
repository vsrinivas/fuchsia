// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/driver/binding.h>

#include <array>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/platform-defs.h>

#include "src/devices/board/drivers/x86/x86.h"

#define PCI_VID_GOLDFISH_ADDRESS_SPACE 0x607D
#define PCI_DID_GOLDFISH_ADDRESS_SPACE 0xF153

namespace x86 {

static const zx_bind_inst_t root_match[] = {
    BI_MATCH(),
};

static const zx_bind_inst_t goldfish_address_space_pci_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PCI),
    BI_ABORT_IF(NE, BIND_PCI_VID, PCI_VID_GOLDFISH_ADDRESS_SPACE),
    BI_MATCH_IF(EQ, BIND_PCI_DID, PCI_DID_GOLDFISH_ADDRESS_SPACE),
};

static const zx_bind_inst_t goldfish_pipe_match[] = {
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_GOLDFISH_PIPE),
};

static const zx_bind_inst_t goldfish_address_space_match[] = {
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_GOLDFISH_ADDRESS_SPACE),
};

static const device_fragment_part_t goldfish_pipe_fragment[] = {
    {std::size(root_match), root_match},
    {std::size(goldfish_pipe_match), goldfish_pipe_match},
};

static const device_fragment_part_t goldfish_address_space_fragment[] = {
    {std::size(root_match), root_match},
    {std::size(goldfish_address_space_pci_match), goldfish_address_space_pci_match},
    {std::size(goldfish_address_space_match), goldfish_address_space_match},
};

static const device_fragment_t goldfish_control_fragments[] = {
    {std::size(goldfish_pipe_fragment), goldfish_pipe_fragment},
    {std::size(goldfish_address_space_fragment), goldfish_address_space_fragment},
};

constexpr zx_device_prop_t props[] = {
    {BIND_PLATFORM_DEV_VID, 0, PDEV_VID_GOOGLE},
    {BIND_PLATFORM_DEV_PID, 0, PDEV_PID_GOLDFISH},
    {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_GOLDFISH_CONTROL},
};

static const composite_device_desc_t comp_desc = {
    .props = props,
    .props_count = std::size(props),
    .fragments = goldfish_control_fragments,
    .fragments_count = std::size(goldfish_control_fragments),
    .coresident_device_index = UINT32_MAX,
};

zx_status_t X86::GoldfishControlInit() {
  zx_status_t status = DdkAddComposite("goldfish-control-2", &comp_desc);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s(goldfish-control-2): DdkAddComposite failed: %d", __func__, status);
    return status;
  }
  return status;
}

}  // namespace x86
