
// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/pci/cpp/banjo.h>
#include <fuchsia/hardware/pciroot/cpp/banjo.h>
#include <fuchsia/hardware/platform/device/cpp/banjo.h>
#include <fuchsia/hardware/sysmem/cpp/banjo.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/ddk/platform-defs.h>

#include <ddktl/device.h>

#include "src/devices/bus/drivers/pci/proxy_rpc.h"

#ifndef SRC_DEVICES_BUS_DRIVERS_PCI_KPCI_H_
#define SRC_DEVICES_BUS_DRIVERS_PCI_KPCI_H_
struct kpci_device {
  zx_device_t* zxdev;
  // only set for non-proxy devices
  pciroot_protocol_t pciroot;
  pdev_protocol_t pdev;
  // only set for proxy devices
  zx_handle_t pciroot_rpcch;
  // kernel pci handle, only set for shadow devices
  zx_handle_t handle;
  // nth device index
  uint32_t index;
  zx_pcie_device_info_t info;
};

namespace pci {

class KernelPci;
using KernelPciType = ddk::Device<pci::KernelPci, ddk::Rxrpcable, ddk::GetProtocolable>;
class KernelPci : public KernelPciType,
                  public ddk::PciProtocol<pci::KernelPci>,
                  ddk::SysmemProtocol<pci::KernelPci> {
 public:
  zx_status_t DdkGetProtocol(uint32_t proto_id, void* out);
  zx_status_t DdkRxrpc(zx_handle_t ch);
  void DdkRelease();

  static zx_status_t Create(zx_device_t* parent, kpci_device device);
  zx_status_t RpcReply(zx_handle_t ch, zx_status_t status, zx_handle_t* handle, PciRpcMsg* req,
                       PciRpcMsg* resp);

  // Pci Protocol
  zx_status_t PciGetBar(uint32_t bar_id, pci_bar_t* out_res);
  zx_status_t PciEnableBusMaster(bool enable);
  zx_status_t PciResetDevice();
  zx_status_t PciAckInterrupt();
  zx_status_t PciMapInterrupt(uint32_t which_irq, zx::interrupt* out_handle);
  zx_status_t PciConfigureIrqMode(uint32_t requested_irq_count, pci_irq_mode_t* mode);
  zx_status_t PciQueryIrqMode(pci_irq_mode_t mode, uint32_t* out_max_irqs);
  zx_status_t PciSetIrqMode(pci_irq_mode_t mode, uint32_t requested_irq_count);
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

  // Sysmem Protocol
  zx_status_t SysmemConnect(zx::channel allocator_request);
  zx_status_t SysmemRegisterHeap(uint64_t heap, zx::channel heap_connection) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t SysmemRegisterSecureMem(zx::channel secure_mem_connection) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t SysmemUnregisterSecureMem() { return ZX_ERR_NOT_SUPPORTED; }

 private:
  KernelPci(zx_device_t* parent, kpci_device device) : KernelPciType(parent), device_(device) {}

  // RPC methods for PciProxy
  zx_status_t RpcEnableBusMaster(zx_handle_t ch, PciRpcMsg* req, PciRpcMsg* resp);
  zx_status_t RpcResetDevice(zx_handle_t ch, PciRpcMsg* req, PciRpcMsg* resp);
  zx_status_t RpcConfigRead(zx_handle_t ch, PciRpcMsg* req, PciRpcMsg* resp);
  zx_status_t RpcConfigWrite(zx_handle_t ch, PciRpcMsg* req, PciRpcMsg* resp);
  zx_status_t RpcSetIrqMode(zx_handle_t ch, PciRpcMsg* req, PciRpcMsg* resp);
  zx_status_t RpcQueryIrqMode(zx_handle_t ch, PciRpcMsg* req, PciRpcMsg* resp);
  zx_status_t RpcConfigureIrqMode(zx_handle_t ch, PciRpcMsg* req, PciRpcMsg* resp);
  zx_status_t RpcMapInterrupt(zx_handle_t ch, PciRpcMsg* req, PciRpcMsg* resp);
  zx_status_t RpcAckInterrupt(zx_handle_t ch, PciRpcMsg* req, PciRpcMsg* resp);
  zx_status_t RpcGetBar(zx_handle_t ch, PciRpcMsg* req, PciRpcMsg* resp);
  zx_status_t RpcGetBti(zx_handle_t ch, PciRpcMsg* req, PciRpcMsg* resp);
  zx_status_t RpcGetDeviceInfo(zx_handle_t ch, PciRpcMsg* req, PciRpcMsg* resp);
  zx_status_t RpcGetNextCapability(zx_handle_t ch, PciRpcMsg* req, PciRpcMsg* resp);
  zx_status_t RpcConnectSysmem(zx_handle_t ch, zx_handle_t handle, PciRpcMsg* req, PciRpcMsg* resp);

  kpci_device device_;
};

}  // namespace pci

#endif  // SRC_DEVICES_BUS_DRIVERS_PCI_KPCI_H_
