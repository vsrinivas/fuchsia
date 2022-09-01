// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.pci/cpp/wire.h>
#include <fuchsia/hardware/pci/cpp/banjo.h>
#include <fuchsia/hardware/pciroot/cpp/banjo.h>
#include <fuchsia/hardware/platform/device/cpp/banjo.h>
#include <fuchsia/hardware/sysmem/cpp/banjo.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/ddk/platform-defs.h>
#include <lib/sys/component/cpp/outgoing_directory.h>

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
  pci_device_info_t info;
  char name[ZX_DEVICE_NAME_MAX];
};

namespace pci {

class KernelPci;
using KernelPciType = ddk::Device<pci::KernelPci, ddk::GetProtocolable>;
class KernelPci : public KernelPciType, public ddk::PciProtocol<pci::KernelPci> {
 public:
  zx_status_t DdkGetProtocol(uint32_t proto_id, void* out);
  void DdkRelease();

  static zx_status_t CreateComposite(zx_device_t* parent, kpci_device device, bool uses_acpi);
  // Pci Protocol
  zx_status_t PciGetBar(uint32_t bar_id, pci_bar_t* out_res);
  zx_status_t PciSetBusMastering(bool enable);
  zx_status_t PciResetDevice();
  zx_status_t PciAckInterrupt();
  zx_status_t PciMapInterrupt(uint32_t which_irq, zx::interrupt* out_handle);
  void PciGetInterruptModes(pci_interrupt_modes_t* out_modes);
  zx_status_t PciSetInterruptMode(pci_interrupt_mode_t mode, uint32_t requested_irq_count);
  zx_status_t PciGetDeviceInfo(pci_device_info_t* out_into);
  zx_status_t PciReadConfig8(uint16_t offset, uint8_t* out_value);
  zx_status_t PciReadConfig16(uint16_t offset, uint16_t* out_value);
  zx_status_t PciReadConfig32(uint16_t offset, uint32_t* out_value);
  zx_status_t PciWriteConfig8(uint16_t offset, uint8_t value);
  zx_status_t PciWriteConfig16(uint16_t offset, uint16_t value);
  zx_status_t PciWriteConfig32(uint16_t offset, uint32_t value);
  zx_status_t PciGetFirstCapability(uint8_t cap_id, uint8_t* out_offset);
  zx_status_t PciGetNextCapability(uint8_t cap_id, uint8_t offset, uint8_t* out_offset);
  zx_status_t PciGetFirstExtendedCapability(uint16_t cap_id, uint16_t* out_offset);
  zx_status_t PciGetNextExtendedCapability(uint16_t cap_id, uint16_t offset, uint16_t* out_offset);
  zx_status_t PciGetBti(uint32_t index, zx::bti* out_bti);

 private:
  KernelPci(zx_device_t* parent, kpci_device device) : KernelPciType(parent), device_(device) {}
  kpci_device device_;
};

// A counterpart of the KernelPci device that uses FIDL instead of Banjo. Both
// devices use the same underlying PCI implementation.
class KernelPciFidl;
using KernelPciFidlType = ddk::Device<pci::KernelPciFidl>;
class KernelPciFidl : public KernelPciFidlType,
                      public fidl::WireServer<fuchsia_hardware_pci::Device> {
 public:
  KernelPciFidl(zx_device_t* parent, kpci_device device, async_dispatcher_t* dispatcher);

  void DdkRelease();

  static zx_status_t CreateComposite(zx_device_t* parent, kpci_device device, bool uses_acpi);
  // Pci Protocol
  void GetBar(GetBarRequestView request, GetBarCompleter::Sync& completer) override;
  void SetBusMastering(SetBusMasteringRequestView request,
                       SetBusMasteringCompleter::Sync& completer) override;
  void ResetDevice(ResetDeviceCompleter::Sync& completer) override;
  void AckInterrupt(AckInterruptCompleter::Sync& completer) override;
  void MapInterrupt(MapInterruptRequestView request,
                    MapInterruptCompleter::Sync& completer) override;
  void GetInterruptModes(GetInterruptModesCompleter::Sync& completer) override;
  void SetInterruptMode(SetInterruptModeRequestView request,
                        SetInterruptModeCompleter::Sync& completer) override;
  void GetDeviceInfo(GetDeviceInfoCompleter::Sync& completer) override;
  void ReadConfig8(ReadConfig8RequestView request, ReadConfig8Completer::Sync& completer) override;
  void ReadConfig16(ReadConfig16RequestView request,
                    ReadConfig16Completer::Sync& completer) override;
  void ReadConfig32(ReadConfig32RequestView request,
                    ReadConfig32Completer::Sync& completer) override;
  void WriteConfig8(WriteConfig8RequestView request,
                    WriteConfig8Completer::Sync& completer) override;
  void WriteConfig16(WriteConfig16RequestView request,
                     WriteConfig16Completer::Sync& completer) override;
  void WriteConfig32(WriteConfig32RequestView request,
                     WriteConfig32Completer::Sync& completer) override;
  void GetCapabilities(GetCapabilitiesRequestView request,
                       GetCapabilitiesCompleter::Sync& completer) override;
  void GetExtendedCapabilities(GetExtendedCapabilitiesRequestView request,
                               GetExtendedCapabilitiesCompleter::Sync& completer) override;
  void GetBti(GetBtiRequestView request, GetBtiCompleter::Sync& completer) override;

  zx_status_t SetUpOutgoingDirectory(fidl::ServerEnd<fuchsia_io::Directory> sever_end);

 private:
  kpci_device device_;
  component::OutgoingDirectory outgoing_;
};

}  // namespace pci

#endif  // SRC_DEVICES_BUS_DRIVERS_PCI_KPCI_H_
