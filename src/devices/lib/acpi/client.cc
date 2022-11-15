// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/lib/acpi/client.h"

#include <fidl/fuchsia.hardware.acpi/cpp/wire.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <zircon/types.h>

#include <ddktl/device.h>

#include "src/devices/lib/acpi/object.h"
#include "util.h"

namespace acpi {
zx::result<fidl::ClientEnd<fuchsia_hardware_acpi::Device>> Client::Connect(zx_device_t* parent) {
  zx::result<fidl::ClientEnd<fuchsia_hardware_acpi::Device>> result;
  if (device_get_fragment_count(parent) == 0) {
    result =
        ddk::Device<void>::DdkConnectFidlProtocol<fuchsia_hardware_acpi::Service::Device>(parent);
  } else {
    result =
        ddk::Device<void>::DdkConnectFragmentFidlProtocol<fuchsia_hardware_acpi::Service::Device>(
            parent, "acpi");
  }
  return result;
}

zx::result<Client> Client::Create(zx_device_t* parent) {
  auto end = Connect(parent);
  if (end.is_error()) {
    return end.take_error();
  }
  fidl::WireSyncClient<fuchsia_hardware_acpi::Device> wire_client(std::move(end.value()));

  return zx::ok(Client(std::move(wire_client)));
}

Client Client::Create(fidl::WireSyncClient<fuchsia_hardware_acpi::Device> client) {
  return Client(std::move(client));
}

zx::result<Object> Client::CallDsm(Uuid uuid, uint64_t revision, uint64_t func_index,
                                   std::optional<fuchsia_hardware_acpi::wire::Object> params) {
  std::array<fuchsia_hardware_acpi::wire::Object, 4> args;
  fidl::Arena arena;
  auto uuid_buf = fidl::VectorView<uint8_t>::FromExternal(uuid.bytes, kUuidBytes);
  args[0] = fuchsia_hardware_acpi::wire::Object::WithBufferVal(
      fidl::ObjectView<fidl::VectorView<uint8_t>>::FromExternal(&uuid_buf));
  args[1] = fuchsia_hardware_acpi::wire::Object::WithIntegerVal(
      fidl::ObjectView<uint64_t>::FromExternal(&revision));
  args[2] = fuchsia_hardware_acpi::wire::Object::WithIntegerVal(
      fidl::ObjectView<uint64_t>::FromExternal(&func_index));

  if (params != std::nullopt) {
    args[3] = params.value();
  } else {
    args[3] = fuchsia_hardware_acpi::wire::Object::WithPackageVal(
        arena, fuchsia_hardware_acpi::wire::ObjectList());
  }

  auto result = client_->EvaluateObject(
      "_DSM", fuchsia_hardware_acpi::wire::EvaluateObjectMode::kPlainObject,
      fidl::VectorView<fuchsia_hardware_acpi::wire::Object>::FromExternal(args));
  if (!result.ok()) {
    return zx::error(result.status());
  }

  if (result->is_error()) {
    return zx::ok(Object(result->error_value()));
  }

  fidl::WireOptional<fuchsia_hardware_acpi::wire::EncodedObject>& maybe_encoded =
      result->value()->result;
  if (!maybe_encoded.has_value() || !maybe_encoded->is_object()) {
    // We called EvaluateObject with mode == PlainObject, so don't expect anything else back.
    return zx::error(ZX_ERR_INTERNAL);
  }

  return zx::ok(Object(maybe_encoded->object()));
}

}  // namespace acpi
