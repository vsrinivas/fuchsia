// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <fidl/fuchsia.hardware.pci/cpp/common_types.h>
#include <fidl/fuchsia.hardware.pci/cpp/wire_types.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/driver.h>
#include <zircon/errors.h>

#include <bind/fuchsia/acpi/cpp/fidl.h>
#include <bind/fuchsia/pci/cpp/fidl.h>

#include "src/devices/bus/drivers/pci/device.h"

namespace fpci = ::fuchsia_hardware_pci;
namespace pci {

void FidlDevice::Bind(fidl::ServerEnd<fuchsia_hardware_pci::Device> request) {
  fidl::BindServer<fidl::WireServer<fuchsia_hardware_pci::Device>>(
      fdf::Dispatcher::GetCurrent()->async_dispatcher(), std::move(request), this);
}

zx_status_t FidlDevice::Create(zx_device_t* parent, pci::Device* device) {
  fbl::AllocChecker ac;
  std::unique_ptr<FidlDevice> fidl_dev(new (&ac) FidlDevice(parent, device));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  auto endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
  if (endpoints.is_error()) {
    return endpoints.status_value();
  }

  auto pci_bind_topo = static_cast<uint32_t>(
      BIND_PCI_TOPO_PACK(device->bus_id(), device->dev_id(), device->func_id()));
  // clang-format off
zx_device_prop_t pci_device_props[] = {
    {BIND_FIDL_PROTOCOL, 0, ZX_FIDL_PROTOCOL_PCI},
    {BIND_PCI_VID, 0, device->vendor_id()},
    {BIND_PCI_DID, 0, device->device_id()},
    {BIND_PCI_CLASS, 0, device->class_id()},
    {BIND_PCI_SUBCLASS, 0, device->subclass()},
    {BIND_PCI_INTERFACE, 0, device->prog_if()},
    {BIND_PCI_REVISION, 0, device->rev_id()},
    {BIND_PCI_TOPO, 0, pci_bind_topo},
};
  // clang-format on
  std::array offers = {
      fidl::DiscoverableProtocolName<fuchsia_hardware_pci::Device>,
  };

  fidl_dev->outgoing_dir().svc_dir()->AddEntry(
      fidl::DiscoverableProtocolName<fuchsia_hardware_pci::Device>,
      fbl::MakeRefCounted<fs::Service>(
          [device = fidl_dev.get()](fidl::ServerEnd<fuchsia_hardware_pci::Device> request) mutable {
            fidl::BindServer(fdf::Dispatcher::GetCurrent()->async_dispatcher(), std::move(request),
                             device);
            zxlogf(TRACE, "[%s] received FIDL connection", device->device()->config()->addr());
            return ZX_OK;
          }));

  zx_status_t status = fidl_dev->outgoing_dir().Serve(std::move(endpoints->server));
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to service the outoing directory");
    return status;
  }

  // Create an isolated devhost to load the proxy pci driver containing the PciProxy
  // instance which will talk to this device.
  const auto name = std::string(device->config()->addr()) + "_";
  status = fidl_dev->DdkAdd(ddk::DeviceAddArgs(name.c_str())
                                .set_props(pci_device_props)
                                .set_flags(DEVICE_ADD_MUST_ISOLATE)
                                .set_outgoing_dir(endpoints->client.TakeChannel())
                                .set_fidl_protocol_offers(offers));
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to create pci fidl fragment %s: %s", device->config()->addr(),
           zx_status_get_string(status));
    return status;
  }

  auto fidl_dev_unowned = fidl_dev.release();
  // TODO(fxbug.dev/93333): Remove this once DFv2 is stabilised.
  bool is_dfv2 = device_is_dfv2(fidl_dev_unowned->zxdev_ptr());
  if (is_dfv2) {
    return ZX_OK;
  }

  const zx_bind_inst_t pci_fragment_match[] = {
      BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PCI),
      BI_ABORT_IF(NE, BIND_PCI_VID, device->vendor_id()),
      BI_ABORT_IF(NE, BIND_PCI_DID, device->device_id()),
      BI_ABORT_IF(NE, BIND_PCI_CLASS, device->class_id()),
      BI_ABORT_IF(NE, BIND_PCI_SUBCLASS, device->subclass()),
      BI_ABORT_IF(NE, BIND_PCI_INTERFACE, device->prog_if()),
      BI_ABORT_IF(NE, BIND_PCI_REVISION, device->rev_id()),
      BI_ABORT_IF(EQ, BIND_COMPOSITE, 1),
      BI_MATCH_IF(EQ, BIND_PCI_TOPO, pci_bind_topo),
  };

  const device_fragment_part_t pci_fragment[] = {
      {std::size(pci_fragment_match), pci_fragment_match},
  };

  const zx_bind_inst_t sysmem_match[] = {
      BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_SYSMEM),
  };

  const device_fragment_part_t sysmem_fragment[] = {
      {std::size(sysmem_match), sysmem_match},
  };

  const zx_bind_inst_t acpi_fragment_match[] = {
      BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_ACPI),
      BI_ABORT_IF(NE, BIND_ACPI_BUS_TYPE, bind::fuchsia::acpi::BIND_ACPI_BUS_TYPE_PCI),
      BI_MATCH_IF(EQ, BIND_PCI_TOPO, pci_bind_topo),
  };

  const device_fragment_part_t acpi_fragment[] = {
      {std::size(acpi_fragment_match), acpi_fragment_match},
  };

  // These are laid out so that ACPI can be optionally included via the number
  // of fragments specified.
  const device_fragment_t fragments[] = {
      {"pci", std::size(pci_fragment), pci_fragment},
      {"sysmem", std::size(sysmem_fragment), sysmem_fragment},
      {"acpi", std::size(acpi_fragment), acpi_fragment},
  };

  composite_device_desc_t composite_desc = {
      .props = pci_device_props,
      .props_count = std::size(pci_device_props),
      .fragments = fragments,
      .fragments_count = (device->has_acpi()) ? std::size(fragments) : std::size(fragments) - 1,
      .primary_fragment = "pci",
      .spawn_colocated = false,
  };

  char composite_name[ZX_DEVICE_NAME_MAX];
  snprintf(composite_name, sizeof(composite_name), "pci-%s", device->config()->addr());
  status = fidl_dev_unowned->DdkAddComposite(composite_name, &composite_desc);
  if (status != ZX_OK) {
    zxlogf(ERROR, "[%s] Failed to create pci fidl composite: %s", device->config()->addr(),
           zx_status_get_string(status));
  }
  return status;
}

void FidlDevice::GetDeviceInfo(GetDeviceInfoRequestView request,
                               GetDeviceInfoCompleter::Sync& completer) {
  completer.Reply({.vendor_id = device_->vendor_id(),
                   .device_id = device_->device_id(),
                   .base_class = device_->class_id(),
                   .sub_class = device_->subclass(),
                   .program_interface = device_->prog_if(),
                   .revision_id = device_->rev_id(),
                   .bus_id = device_->bus_id(),
                   .dev_id = device_->dev_id(),
                   .func_id = device_->func_id()});
}

void FidlDevice::GetBar(GetBarRequestView request, GetBarCompleter::Sync& completer) {
  if (request->bar_id >= fpci::wire::kMaxBarCount) {
    completer.ReplyError(ZX_ERR_INVALID_ARGS);
    return;
  }

  fbl::AutoLock dev_lock(device_->dev_lock());
  auto& bar = device_->bars()[request->bar_id];
  size_t bar_size = bar.size;
  if (bar_size == 0) {
    completer.ReplyError(ZX_ERR_NOT_FOUND);
    return;
  }

#ifdef ENABLE_MSIX
  // If this device shares BAR data with either of the MSI-X tables then we need
  // to determine what portions of the BAR the driver can be permitted to
  // access. If the MSI-X bar exists in the only page present in the BAR then we
  // need to deny all access to the BAR.
  if (device_->capabilities().msix) {
    zx::status<size_t> result = device_->capabilities().msix->GetBarDataSize(bar);
    if (!result.is_ok()) {
      completer.ReplyError(result.status_value());
      return;
    }
    bar_size = result.value();
  }
#endif

  zx_status_t status = ZX_OK;
  if (bar.is_mmio) {
    zx::status<zx::vmo> result = bar.allocation->CreateVmo();
    if (result.is_ok()) {
      completer.ReplySuccess({.bar_id = request->bar_id,
                              .size = bar_size,
                              .result = fpci::wire::BarResult::WithVmo(std::move(result.value()))});
      return;
    }
    completer.ReplyError(result.status_value());
  } else {
    zx::status<zx::resource> result = bar.allocation->CreateResource();
    if (status == ZX_OK) {
      fidl::Arena arena;
      completer.ReplySuccess(
          {.bar_id = request->bar_id,
           .size = bar_size,
           .result = fpci::wire::BarResult::WithIo(
               arena, fuchsia_hardware_pci::wire::IoBar{.address = bar.address,
                                                        .resource = std::move(result.value())})});
      return;
    }
    completer.ReplyError(result.status_value());
  }
}

void FidlDevice::SetBusMastering(SetBusMasteringRequestView request,
                                 SetBusMasteringCompleter::Sync& completer) {
  fbl::AutoLock dev_lock(device_->dev_lock());
  zx_status_t status = device_->SetBusMastering(request->enabled);
  if (status != ZX_OK) {
    completer.ReplyError(status);
    return;
  }
  completer.ReplySuccess();
}

void FidlDevice::ResetDevice(ResetDeviceRequestView request,
                             ResetDeviceCompleter::Sync& completer) {
  completer.Reply({});
}

void FidlDevice::AckInterrupt(AckInterruptRequestView request,
                              AckInterruptCompleter::Sync& completer) {
  fbl::AutoLock dev_lock(device_->dev_lock());
  zx_status_t status = device_->AckLegacyIrq();
  if (status != ZX_OK) {
    completer.ReplyError(status);
    return;
  }
  completer.ReplySuccess();
}

void FidlDevice::MapInterrupt(MapInterruptRequestView request,
                              MapInterruptCompleter::Sync& completer) {
  zx::status<zx::interrupt> result = device_->MapInterrupt(request->which_irq);
  if (!result.is_ok()) {
    completer.ReplyError(result.status_value());
    return;
  }
  completer.ReplySuccess(std::move(result.value()));
}

void FidlDevice::SetInterruptMode(SetInterruptModeRequestView request,
                                  SetInterruptModeCompleter::Sync& completer) {
  zx_status_t status = device_->SetIrqMode(request->mode, request->requested_irq_count);
  if (status != ZX_OK) {
    completer.ReplyError(status);
    return;
  }
  completer.ReplySuccess();
}

void FidlDevice::GetInterruptModes(GetInterruptModesRequestView request,
                                   GetInterruptModesCompleter::Sync& completer) {
  pci_interrupt_modes_t modes = device_->GetInterruptModes();
  completer.Reply({.has_legacy = modes.has_legacy,
                   .msi_count = modes.msi_count,
                   .msix_count = modes.msix_count});
}

void FidlDevice::ReadConfig8(ReadConfig8RequestView request,
                             ReadConfig8Completer::Sync& completer) {
  auto result = device_->ReadConfig<uint8_t, PciReg8>(request->offset);
  if (!result.is_ok()) {
    completer.ReplyError(result.status_value());
    return;
  }
  completer.ReplySuccess(result.value());
}

void FidlDevice::ReadConfig16(ReadConfig16RequestView request,
                              ReadConfig16Completer::Sync& completer) {
  auto result = device_->ReadConfig<uint16_t, PciReg16>(request->offset);
  if (!result.is_ok()) {
    completer.ReplyError(result.status_value());
    return;
  }
  completer.ReplySuccess(result.value());
}

void FidlDevice::ReadConfig32(ReadConfig32RequestView request,
                              ReadConfig32Completer::Sync& completer) {
  auto result = device_->ReadConfig<uint32_t, PciReg32>(request->offset);
  if (!result.is_ok()) {
    completer.ReplyError(result.status_value());
    return;
  }
  completer.ReplySuccess(result.value());
}

void FidlDevice::WriteConfig8(WriteConfig8RequestView request,
                              WriteConfig8Completer::Sync& completer) {
  zx_status_t status = device_->WriteConfig<uint8_t, PciReg8>(request->offset, request->value);
  if (status != ZX_OK) {
    completer.ReplyError(status);
    return;
  }
  completer.ReplySuccess();
}

void FidlDevice::WriteConfig16(WriteConfig16RequestView request,
                               WriteConfig16Completer::Sync& completer) {
  zx_status_t status = device_->WriteConfig<uint16_t, PciReg16>(request->offset, request->value);
  if (status != ZX_OK) {
    completer.ReplyError(status);
    return;
  }
  completer.ReplySuccess();
}

void FidlDevice::WriteConfig32(WriteConfig32RequestView request,
                               WriteConfig32Completer::Sync& completer) {
  zx_status_t status = device_->WriteConfig<uint32_t, PciReg32>(request->offset, request->value);
  if (status != ZX_OK) {
    completer.ReplyError(status);
    return;
  }
  completer.ReplySuccess();
}

void FidlDevice::GetCapabilities(GetCapabilitiesRequestView request,
                                 GetCapabilitiesCompleter::Sync& completer) {
  std::vector<uint8_t> capabilities;
  {
    fbl::AutoLock dev_lock(device_->dev_lock());
    for (auto& capability : device_->capabilities().list) {
      if (capability.id() == request->id) {
        capabilities.push_back(capability.base());
      }
    }
  }

  completer.Reply(::fidl::VectorView<uint8_t>::FromExternal(capabilities));
}

void FidlDevice::GetExtendedCapabilities(GetExtendedCapabilitiesRequestView request,
                                         GetExtendedCapabilitiesCompleter::Sync& completer) {
  std::vector<uint16_t> ext_capabilities;
  {
    fbl::AutoLock dev_lock(device_->dev_lock());
    for (auto& ext_capability : device_->capabilities().ext_list) {
      if (ext_capability.id() == request->id) {
        ext_capabilities.push_back(ext_capability.base());
      }
    }
  }

  completer.Reply(::fidl::VectorView<uint16_t>::FromExternal(ext_capabilities));
}

void FidlDevice::GetBti(GetBtiRequestView request, GetBtiCompleter::Sync& completer) {
  fbl::AutoLock dev_lock(device_->dev_lock());
  zx::bti bti;
  zx_status_t status = device_->bdi()->GetBti(device_, request->index, &bti);
  if (status != ZX_OK) {
    completer.ReplyError(status);
    return;
  }
  completer.ReplySuccess(std::move(bti));
}

}  // namespace pci
