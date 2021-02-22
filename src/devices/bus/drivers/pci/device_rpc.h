// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_DEVICES_BUS_DRIVERS_PCI_DEVICE_RPC_H_
#define SRC_DEVICES_BUS_DRIVERS_PCI_DEVICE_RPC_H_

#include <stdint.h>
#include <zircon/syscalls/pci.h>

namespace pci {

enum PciRpcOp : uint32_t {
  PCI_OP_INVALID = 0,
  PCI_OP_CONFIG_READ,
  PCI_OP_CONFIG_WRITE,
  PCI_OP_CONFIGURE_IRQ_MODE,
  PCI_OP_CONNECT_SYSMEM,
  PCI_OP_ENABLE_BUS_MASTER,
  PCI_OP_GET_BAR,
  PCI_OP_GET_BTI,
  PCI_OP_GET_DEVICE_INFO,
  PCI_OP_GET_NEXT_CAPABILITY,
  PCI_OP_MAP_INTERRUPT,
  PCI_OP_QUERY_IRQ_MODE,
  PCI_OP_RESET_DEVICE,
  PCI_OP_SET_IRQ_MODE,
  PCI_OP_MAX,
};

// TODO(fxbug.dev/32978): When the kernel driver is removed we should consolidate the pci banjo
// definitions and these rpc messages to avoid duplication.
struct PciMsgCfg {
  uint16_t offset;
  uint16_t width;
  uint32_t value;
};

struct PciMsgBar {
  uint32_t id;
  bool is_mmio;
  size_t size;
  zx_paddr_t io_addr;
};

// For use with QUERY_IRQ_MODE, SET_IRQ_MODE, and MAP_INTERRUPT
struct PciMsgIrq {
  pci_irq_mode_t mode;
  union {
    uint32_t which_irq;
    uint32_t max_irqs;
    uint32_t requested_irqs;
  };
};

struct PciMsgCapability {
  uint16_t id;
  uint16_t offset;
  bool is_first;
  bool is_extended;
};

// The max value for each int type is an invalid capability offset we
// can use to provide a value to allow GetNextCapaility and GetFirstCapability
// to be served by the same impl on the other end of RPC.
const uint16_t kPciCapOffsetFirst = UINT8_MAX;
const uint16_t kPciExtCapOffsetFirst = UINT16_MAX;

// TODO(fxbug.dev/33713): port this to non-zx_pcie structures
using PciMsgDeviceInfo = pcie_device_info_t;

struct PciRpcMsg {
  zx_txid_t txid;  // handled by zx_channel_call
  uint32_t op;
  zx_status_t ret;
  // Subtract the size of the preceeding 6 uint32_ts to keep
  // the structure inside a single page.
  union {
    bool enable;
    PciMsgCfg cfg;
    PciMsgIrq irq;
    PciMsgBar bar;
    PciMsgDeviceInfo info;
    PciMsgCapability cap;
    uint8_t data[ZX_PAGE_SIZE - 24u];
    uint32_t bti_index;
    zx_handle_t handle;
  };
};
static_assert(sizeof(PciRpcMsg) <= ZX_PAGE_SIZE);

}  // namespace pci

#endif  // SRC_DEVICES_BUS_DRIVERS_PCI_DEVICE_RPC_H_
