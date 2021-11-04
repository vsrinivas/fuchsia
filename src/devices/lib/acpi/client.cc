// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/lib/acpi/client.h"

#include <fidl/fuchsia.hardware.acpi/cpp/wire.h>
#include <fuchsia/hardware/acpi/cpp/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/driver.h>
#include <zircon/types.h>

#include "src/devices/lib/acpi/object.h"
#include "util.h"

namespace acpi {
zx::status<Client> Client::Create(zx_device_t* parent) {
  zx::channel local, remote;
  zx_status_t status = zx::channel::create(0, &local, &remote);
  if (status != ZX_OK) {
    return zx::error(status);
  }

  ddk::AcpiProtocolClient client;
  if (device_get_fragment_count(parent) == 0) {
    client = ddk::AcpiProtocolClient(parent);
  } else {
    client = ddk::AcpiProtocolClient(parent, "acpi");
  }
  if (!client.is_valid()) {
    return zx::error(ZX_ERR_NOT_FOUND);
  }

  client.ConnectServer(std::move(remote));

  fidl::ClientEnd<fuchsia_hardware_acpi::Device> end(std::move(local));
  fidl::WireSyncClient<fuchsia_hardware_acpi::Device> wire_client(std::move(end));

  return zx::ok(Client(std::move(wire_client)));
}

Client Client::Create(fidl::WireSyncClient<fuchsia_hardware_acpi::Device> client) {
  return Client(std::move(client));
}

zx::status<Object> Client::CallDsm(Uuid uuid, uint64_t revision, uint64_t func_index,
                                   std::optional<fuchsia_hardware_acpi::wire::Object> params) {
  std::array<fuchsia_hardware_acpi::wire::Object, 4> args;
  auto uuid_buf = fidl::VectorView<uint8_t>::FromExternal(uuid.bytes, kUuidBytes);
  args[0].set_buffer_val(fidl::ObjectView<fidl::VectorView<uint8_t>>::FromExternal(&uuid_buf));
  args[1].set_integer_val(fidl::ObjectView<uint64_t>::FromExternal(&revision));
  args[2].set_integer_val(fidl::ObjectView<uint64_t>::FromExternal(&func_index));

  int argc = 3;
  if (params != std::nullopt) {
    args[3] = params.value();
    argc++;
  }

  auto result = client_->EvaluateObject(
      "_DSM", fuchsia_hardware_acpi::wire::EvaluateObjectMode::kPlainObject,
      fidl::VectorView<fuchsia_hardware_acpi::wire::Object>::FromExternal(args.data(), argc));
  if (!result.ok()) {
    return zx::error(result.status());
  }

  if (result->result.is_err()) {
    return zx::ok(Object(result->result.err()));
  }

  if (!result->result.response().result.is_object()) {
    // We called EvaluateObject with mode == PlainObject, so don't expect anything else back.
    return zx::error(ZX_ERR_INTERNAL);
  }

  return zx::ok(Object(result->result.response().result.object()));
}

}  // namespace acpi
