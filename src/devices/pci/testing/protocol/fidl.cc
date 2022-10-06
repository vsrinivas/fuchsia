// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/pci/testing/pci_protocol_fake.h"

namespace pci {

void FakePciProtocol::GetBar(GetBarRequestView request, GetBarCompleter::Sync& completer) {
  pci_bar_t bar;
  zx_status_t status = PciGetBar(request->bar_id, &bar);
  if (status != ZX_OK) {
    completer.ReplyError(status);
    return;
  }

  if (bar.type == PCI_BAR_TYPE_IO) {
    fidl::Arena arena;
    completer.ReplySuccess({.bar_id = request->bar_id,
                            .size = bar.size,
                            .result = fuchsia_hardware_pci::wire::BarResult::WithIo(
                                arena, fuchsia_hardware_pci::wire::IoBar{
                                           .address = bar.result.io.address,
                                           .resource = zx::resource(bar.result.io.resource)})});
  } else {
    completer.ReplySuccess(
        {.bar_id = request->bar_id,
         .size = bar.size,
         .result = fuchsia_hardware_pci::wire::BarResult::WithVmo(zx::vmo(bar.result.vmo))});
  }
}

void FakePciProtocol::GetDeviceInfo(GetDeviceInfoCompleter::Sync& completer) {
  pci_device_info_t out_info;
  zx_status_t status = PciGetDeviceInfo(&out_info);
  if (status == ZX_OK) {
    completer.Reply(fuchsia_hardware_pci::wire::DeviceInfo{
        .vendor_id = out_info.vendor_id,
        .device_id = out_info.device_id,
        .base_class = out_info.base_class,
        .sub_class = out_info.sub_class,
        .program_interface = out_info.program_interface,
        .revision_id = out_info.revision_id,
        .bus_id = out_info.bus_id,
        .dev_id = out_info.dev_id,
        .func_id = out_info.func_id,
    });

  } else {
    completer.Close(status);
  }
}

void FakePciProtocol::GetBti(GetBtiRequestView request, GetBtiCompleter::Sync& completer) {
  zx::bti out_bti;
  zx_status_t status = PciGetBti(request->index, &out_bti);
  if (status == ZX_OK) {
    completer.ReplySuccess(std::move(out_bti));
  } else {
    completer.ReplyError(status);
  }
}

void FakePciProtocol::WriteConfig8(WriteConfig8RequestView request,
                                   WriteConfig8Completer::Sync& completer) {
  zx_status_t status = PciWriteConfig8(request->offset, request->value);
  if (status == ZX_OK) {
    completer.ReplySuccess();
  } else {
    completer.ReplyError(status);
  }
}

void FakePciProtocol::WriteConfig16(WriteConfig16RequestView request,
                                    WriteConfig16Completer::Sync& completer) {
  zx_status_t status = PciWriteConfig16(request->offset, request->value);
  if (status == ZX_OK) {
    completer.ReplySuccess();
  } else {
    completer.ReplyError(status);
  }
}

void FakePciProtocol::WriteConfig32(WriteConfig32RequestView request,
                                    WriteConfig32Completer::Sync& completer) {
  zx_status_t status = PciWriteConfig32(request->offset, request->value);
  if (status == ZX_OK) {
    completer.ReplySuccess();
  } else {
    completer.ReplyError(status);
  }
}

void FakePciProtocol::ReadConfig8(ReadConfig8RequestView request,
                                  ReadConfig8Completer::Sync& completer) {
  uint8_t out_value;
  zx_status_t status = PciReadConfig8(request->offset, &out_value);
  if (status == ZX_OK) {
    completer.ReplySuccess(out_value);
  } else {
    completer.ReplyError(status);
  }
}

void FakePciProtocol::ReadConfig16(ReadConfig16RequestView request,
                                   ReadConfig16Completer::Sync& completer) {
  uint16_t out_value;
  zx_status_t status = PciReadConfig16(request->offset, &out_value);
  if (status == ZX_OK) {
    completer.ReplySuccess(out_value);
  } else {
    completer.ReplyError(status);
  }
}

void FakePciProtocol::ReadConfig32(ReadConfig32RequestView request,
                                   ReadConfig32Completer::Sync& completer) {
  uint32_t out_value;
  zx_status_t status = PciReadConfig32(request->offset, &out_value);
  if (status == ZX_OK) {
    completer.ReplySuccess(out_value);
  } else {
    completer.ReplyError(status);
  }
}

void FakePciProtocol::GetInterruptModes(GetInterruptModesCompleter::Sync& completer) {
  pci_interrupt_modes_t out_modes;
  PciGetInterruptModes(&out_modes);
  completer.Reply(fuchsia_hardware_pci::wire::InterruptModes{
      .has_legacy = out_modes.has_legacy,
      .msi_count = out_modes.msi_count,
      .msix_count = out_modes.msix_count,
  });
}

void FakePciProtocol::SetInterruptMode(SetInterruptModeRequestView request,
                                       SetInterruptModeCompleter::Sync& completer) {
  zx_status_t status =
      PciSetInterruptMode(fidl::ToUnderlying(request->mode), request->requested_irq_count);
  if (status == ZX_OK) {
    completer.ReplySuccess();
  } else {
    completer.ReplyError(status);
  }
}

void FakePciProtocol::MapInterrupt(MapInterruptRequestView request,
                                   MapInterruptCompleter::Sync& completer) {
  zx::interrupt out_interrupt;
  zx_status_t status = PciMapInterrupt(request->which_irq, &out_interrupt);
  if (status == ZX_OK) {
    completer.ReplySuccess(std::move(out_interrupt));
  } else {
    completer.ReplyError(status);
  }
}

void FakePciProtocol::GetCapabilities(GetCapabilitiesRequestView request,
                                      GetCapabilitiesCompleter::Sync& completer) {
  std::vector<uint8_t> capabilities;
  uint8_t offset;
  zx_status_t status = PciGetFirstCapability(fidl::ToUnderlying(request->id), &offset);
  if (status != ZX_OK) {
    completer.Close(status);
    return;
  }
  capabilities.push_back(offset);

  uint8_t out_offset;
  while (true) {
    status = PciGetNextCapability(fidl::ToUnderlying(request->id), offset, &out_offset);
    if (status == ZX_ERR_NOT_FOUND) {
      break;
    } else if (status != ZX_OK) {
      completer.Close(status);
      return;
    }

    capabilities.push_back(out_offset);
    offset = out_offset;
  }
  completer.Reply(fidl::VectorView<uint8_t>::FromExternal(capabilities));
}

void FakePciProtocol::SetBusMastering(SetBusMasteringRequestView request,
                                      SetBusMasteringCompleter::Sync& completer) {
  zx_status_t status = PciSetBusMastering(request->enabled);
  if (status == ZX_OK) {
    completer.ReplySuccess();
  } else {
    completer.ReplyError(status);
  }
}

void FakePciProtocol::ResetDevice(ResetDeviceCompleter::Sync& completer) {
  zx_status_t status = PciResetDevice();
  if (status == ZX_OK) {
    completer.ReplySuccess();
  } else {
    completer.ReplyError(status);
  }
}

void FakePciProtocol::AckInterrupt(AckInterruptCompleter::Sync& completer) {
  zx_status_t status = PciAckInterrupt();
  if (status == ZX_OK) {
    completer.ReplySuccess();
  } else {
    completer.ReplyError(status);
  }
}

ddk::Pci FakePciProtocol::SetUpFidlServer(async::Loop& loop) {
  auto endpoints = fidl::CreateEndpoints<fuchsia_hardware_pci::Device>();
  ZX_ASSERT(endpoints.is_ok());

  auto fake_pci = std::make_unique<FakePciProtocol>();
  std::optional<fidl::ServerBindingRef<fuchsia_hardware_pci::Device>> binding = fidl::BindServer(
      loop.dispatcher(),
      fidl::ServerEnd<fuchsia_hardware_pci::Device>(endpoints->server.TakeChannel()), this);
  ZX_ASSERT(binding.has_value());

  ddk::Pci pci = ddk::Pci(std::move(endpoints->client));
  ZX_ASSERT(pci.is_valid());
  return pci;
}

}  // namespace pci
