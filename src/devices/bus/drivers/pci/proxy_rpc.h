// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_DEVICES_BUS_DRIVERS_PCI_PROXY_RPC_H_
#define SRC_DEVICES_BUS_DRIVERS_PCI_PROXY_RPC_H_

#include <fuchsia/hardware/pci/cpp/banjo.h>
#include <stdint.h>
#include <zircon/syscalls/pci.h>

namespace pci {

enum PciRpcOp : uint32_t {
  PCI_OP_INVALID = 0,
  PCI_OP_CONFIG_READ,
  PCI_OP_CONFIG_WRITE,
  PCI_OP_CONNECT_SYSMEM,
  PCI_OP_ENABLE_BUS_MASTER,
  PCI_OP_GET_BAR,
  PCI_OP_GET_BTI,
  PCI_OP_GET_DEVICE_INFO,
  PCI_OP_GET_NEXT_CAPABILITY,
  PCI_OP_MAP_INTERRUPT,
  PCI_OP_GET_INTERRUPT_MODES,
  PCI_OP_SET_INTERRUPT_MODE,
  PCI_OP_RESET_DEVICE,
  PCI_OP_ACK_INTERRUPT,
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
  zx_paddr_t address;
};

// For use with QUERY_IRQ_MODE, SET_IRQ_MODE, and MAP_INTERRUPT
struct PciMsgIrq {
  pci_irq_mode_t mode;
  union {
    uint32_t which_irq;
    uint32_t requested_irqs;
    pci_interrupt_modes_t modes;
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
  PciRpcOp op;
  zx_status_t ret;
  union {
    PciMsgBar bar;
    PciMsgCfg cfg;
    PciMsgIrq irq;
    PciMsgDeviceInfo info;
    PciMsgCapability cap;
    uint32_t bti_index;
    bool enable;
  };
};

}  // namespace pci

#endif  // SRC_DEVICES_BUS_DRIVERS_PCI_PROXY_RPC_H_
