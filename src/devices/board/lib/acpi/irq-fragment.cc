// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/board/lib/acpi/irq-fragment.h"

#include "ddktl/device.h"
#include "fbl/string_printf.h"
#include "fidl/fuchsia.hardware.interrupt/cpp/wire_messaging.h"
#include "lib/sys/component/cpp/handlers.h"
#include "lib/sys/component/cpp/outgoing_directory.h"
#include "src/devices/board/lib/acpi/device.h"

namespace acpi {

IrqFragment::IrqFragment(async_dispatcher_t* dispatcher, acpi::Device& parent, uint32_t irq_index)
    : IrqFragmentDeviceType(parent.zxdev()),
      device_(parent),
      irq_index_(irq_index),
      dispatcher_(dispatcher),
      outgoing_(component::OutgoingDirectory::Create(dispatcher)) {}

zx::status<> IrqFragment::Create(async_dispatcher_t* dispatcher, acpi::Device& parent,
                                 uint32_t irq_index, uint32_t acpi_device_id) {
  auto device = std::unique_ptr<IrqFragment>(new IrqFragment(dispatcher, parent, irq_index));

  auto result = device->Init(acpi_device_id);
  if (result.is_ok()) {
    // The DDK takes ownership of the device.
    __UNUSED auto unused = device.release();
  }

  return result;
}

zx::status<> IrqFragment::Init(uint32_t device_id) {
  component::ServiceInstanceHandler handler;
  fuchsia_hardware_interrupt::Service::Handler service(&handler);

  auto provider_handler = [this](fidl::ServerEnd<fuchsia_hardware_interrupt::Provider> request) {
    fidl::BindServer(dispatcher_, std::move(request), this);
  };

  auto result = service.add_provider(std::move(provider_handler));
  if (result.is_error()) {
    return result.take_error();
  }

  result = outgoing_.AddService<fuchsia_hardware_interrupt::Service>(std::move(handler));
  if (result.is_error()) {
  }

  auto endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
  if (endpoints.is_error()) {
    return endpoints.take_error();
  }

  result = outgoing_.Serve(std::move(endpoints->server));
  if (result.is_error()) {
    return result.take_error();
  }

  std::array offers = {
      fuchsia_hardware_interrupt::Service::Name,
  };

  // Make sure the properties here stay in sync with
  // the bind rules in device-builder.cc.
  // LINT.IfChange
  std::array properties = {
      zx_device_prop_t{
          .id = BIND_ACPI_ID,
          .value = device_id,
      },
      zx_device_prop_t{
          .id = BIND_PLATFORM_DEV_INTERRUPT_ID,
          .value = irq_index_ + 1,
      },
  };
  // LINT.ThenChange(device-builder.cc)

  auto name = fbl::StringPrintf("%s-irq%03u", device_.name(), irq_index_);
  zx_status_t status = DdkAdd(ddk::DeviceAddArgs(name.data())
                                  .set_flags(DEVICE_ADD_MUST_ISOLATE)
                                  .set_outgoing_dir(endpoints->client.TakeChannel())
                                  .set_fidl_service_offers(offers)
                                  .set_props(properties));

  return zx::make_status(status);
}

void IrqFragment::Get(GetCompleter::Sync& completer) {
  auto result = device_.GetInterrupt(irq_index_);
  if (result.is_error()) {
    completer.ReplyError(result.error_value());
  } else {
    completer.ReplySuccess(std::move(*result));
  }
}

}  // namespace acpi
