// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_DEVICES_BUS_DRIVERS_PCI_PROXY_H_
#define SRC_DEVICES_BUS_DRIVERS_PCI_PROXY_H_

#include <fuchsia/hardware/pci/cpp/banjo.h>
#include <fuchsia/hardware/sysmem/cpp/banjo.h>
#include <lib/zx/channel.h>
#include <stdio.h>
#include <sys/types.h>
#include <zircon/errors.h>
#include <zircon/syscalls.h>

#include <ddktl/device.h>

#include "src/devices/bus/drivers/pci/proxy_rpc.h"

namespace pci {
class PciProxy;
using PciProxyType = ddk::Device<pci::PciProxy, ddk::GetProtocolable>;
class PciProxy : public PciProxyType,
                 public ddk::PciProtocol<pci::PciProxy>,
                 public ddk::SysmemProtocol<pci::PciProxy> {
 public:
  PciProxy(zx_device_t* parent, zx_handle_t rpcch) : PciProxyType(parent), rpcch_(rpcch) {}
  static zx_status_t Create(zx_device_t* parent, zx_handle_t rpcch, const char* name);
  // A helper method to reduce the complexity of each individual PciProtocol method.
  zx_status_t RpcRequest(PciRpcOp op, zx_handle_t* rd_handle, const zx_handle_t* wr_handle,
                         PciRpcMsg* req, PciRpcMsg* resp);
  zx_status_t DdkGetProtocol(uint32_t proto_id, void* out);
  void DdkRelease() { delete this; }

  // ddk::PciProtocol implementations.
  zx_status_t PciGetBar(uint32_t bar_id, pci_bar_t* out_res);
  zx_status_t PciEnableBusMaster(bool enable);
  zx_status_t PciResetDevice();
  zx_status_t PciAckInterrupt();
  zx_status_t PciMapInterrupt(uint32_t which_irq, zx::interrupt* out_handle);
  zx_status_t PciConfigureInterruptMode(uint32_t requested_irq_count, pci_irq_mode_t* mode);
  zx_status_t PciQueryIrqMode(pci_irq_mode_t mode, uint32_t* out_max_irqs);
  void PciGetInterruptModes(pci_interrupt_modes_t* out_modes);
  zx_status_t PciSetInterruptMode(pci_irq_mode_t mode, uint32_t requested_irq_count);
  zx_status_t PciGetDeviceInfo(pcie_device_info_t* out_into);
  zx_status_t PciConfigRead8(uint16_t offset, uint8_t* out_value);
  zx_status_t PciConfigRead16(uint16_t offset, uint16_t* out_value);
  zx_status_t PciConfigRead32(uint16_t offset, uint32_t* out_value);
  zx_status_t PciConfigWrite8(uint16_t offset, uint8_t value);
  zx_status_t PciConfigWrite16(uint16_t offset, uint16_t value);
  zx_status_t PciConfigWrite32(uint16_t offset, uint32_t value);
  zx_status_t PciGetFirstCapability(uint8_t cap_id, uint8_t* out_offset);
  zx_status_t PciGetNextCapability(uint8_t cap_id, uint8_t offset, uint8_t* out_offset);
  zx_status_t PciGetFirstExtendedCapability(uint16_t cap_id, uint16_t* out_offset);
  zx_status_t PciGetNextExtendedCapability(uint16_t cap_id, uint16_t offset, uint16_t* out_offset);
  zx_status_t PciGetBti(uint32_t index, zx::bti* out_bti);

  // ddk::Sysmem stubs
  zx_status_t SysmemConnect(zx::channel allocator_request);
  zx_status_t SysmemRegisterHeap(uint64_t heap, zx::channel heap_connection) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t SysmemRegisterSecureMem(zx::channel secure_mem_connection) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t SysmemUnregisterSecureMem() { return ZX_ERR_NOT_SUPPORTED; }

 private:
  // Helpers to marshal config-based RPC.
  template <typename T>
  zx_status_t PciConfigRead(uint16_t offset, T* out_value);
  template <typename T>
  zx_status_t PciConfigWrite(uint16_t offset, T value);
  zx::channel rpcch_;
};

}  // namespace pci

#endif  // SRC_DEVICES_BUS_DRIVERS_PCI_PROXY_H_
