// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/mcu/drivers/chromiumos-ec-lpc/chromiumos_ec_lpc.h"

#include <fidl/fuchsia.hardware.acpi/cpp/wire.h>
#include <fidl/fuchsia.hardware.google.ec/cpp/wire_types.h>
#include <fidl/fuchsia.io/cpp/markers.h>
#include <fuchsia/hardware/acpi/cpp/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/driver.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/fidl/llcpp/connect_service.h>
#include <lib/fidl/llcpp/server.h>

#include <chromiumos-platform-ec/ec_commands.h>

#include "src/devices/mcu/drivers/chromiumos-ec-lpc/chromiumos_ec_lpc_bind.h"
#include "src/lib/storage/vfs/cpp/service.h"

namespace chromiumos_ec_lpc {
namespace fcrosec = fuchsia_hardware_google_ec;

zx_status_t ChromiumosEcLpc::Bind(void* ctx, zx_device_t* dev) {
  auto device = std::make_unique<ChromiumosEcLpc>(dev);

  zx_status_t status = device->Bind();
  if (status == ZX_OK) {
    // Release ownership of the device to the DDK.
    __UNUSED auto unused = device.release();
  }

  return status;
}

zx_status_t ChromiumosEcLpc::Bind() {
  // Enable access to the ranges of IO ports required for communication with the EC.
  //
  // This list is not available via ACPI, so we need to hard-code it.
  // TODO(fxbug.dev/89226): patch in required resources to the ACPI table, instead of using
  // get_root_resource().
  struct PortRange {
    uint16_t base;
    uint16_t size;
  };
  for (const auto& region : (PortRange[]){
           {EC_HOST_CMD_REGION0, EC_HOST_CMD_REGION_SIZE},
           {EC_HOST_CMD_REGION1, EC_HOST_CMD_REGION_SIZE},
           {EC_LPC_ADDR_ACPI_DATA, 4},
           {EC_LPC_ADDR_ACPI_CMD, 4},
           {EC_LPC_ADDR_HOST_DATA, 4},
           {EC_LPC_ADDR_HOST_CMD, 4},
           {EC_LPC_ADDR_MEMMAP, EC_MEMMAP_SIZE},
       }) {
    // Please do not use get_root_resource() in new code. See fxbug.dev/31358.
    zx_status_t status = zx_ioports_request(get_root_resource(), region.base, region.size);
    if (status != ZX_OK) {
      zxlogf(ERROR, "ioports request for range 0x%x-0x%x failed: %s", region.base,
             region.base + region.size - 1, zx_status_get_string(status));
      return status;
    }
  }

  // Ensure we have a supported EC.
  if (!CrOsEc::IsLpc3Supported()) {
    zxlogf(ERROR, "EC does not support LPC protocol v3?");
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t status = loop_.StartThread("chromiumos-ec-lpc-fidl");
  if (status != ZX_OK) {
    return status;
  }

  outgoing_.emplace(loop_.dispatcher());
  outgoing_->svc_dir()->AddEntry(
      fidl::DiscoverableProtocolName<fcrosec::Device>,
      fbl::MakeRefCounted<fs::Service>([this](fidl::ServerEnd<fcrosec::Device> request) mutable {
        return fidl::BindSingleInFlightOnly(loop_.dispatcher(), std::move(request), this);
      }));

  outgoing_->svc_dir()->AddEntry(
      fidl::DiscoverableProtocolName<fuchsia_hardware_acpi::Device>,
      fbl::MakeRefCounted<fs::Service>(
          [this](fidl::ServerEnd<fuchsia_hardware_acpi::Device> request) mutable {
            ddk::AcpiProtocolClient client(parent(), "acpi");
            client.ConnectServer(request.TakeChannel());
            return ZX_OK;
          }));

  auto endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
  if (endpoints.is_error()) {
    return endpoints.status_value();
  }

  auto st = outgoing_->Serve(std::move(endpoints->server));
  if (st != ZX_OK) {
    zxlogf(ERROR, "Failed to serve the outgoing directory: %s", zx_status_get_string(st));
    return st;
  }

  std::array offers = {
      fidl::DiscoverableProtocolName<fcrosec::Device>,
  };

  return DdkAdd(ddk::DeviceAddArgs("chromiumos_ec_lpc")
                    .set_flags(DEVICE_ADD_MUST_ISOLATE)
                    .set_inspect_vmo(inspect_.DuplicateVmo())
                    .set_fidl_protocol_offers(offers)
                    .set_outgoing_dir(endpoints->client.TakeChannel()));
}

void ChromiumosEcLpc::DdkInit(ddk::InitTxn txn) { txn.Reply(ZX_OK); }

void ChromiumosEcLpc::DdkUnbind(ddk::UnbindTxn txn) {
  loop_.Shutdown();
  txn.Reply();
}

void ChromiumosEcLpc::DdkRelease() { delete this; }

void ChromiumosEcLpc::RunCommand(RunCommandRequestView request,
                                 RunCommandCompleter::Sync& completer) {
  std::array<uint8_t, fcrosec::wire::kMaxCommandSize> response;

  size_t actual_size;
  uint16_t result;
  zx_status_t status = CrOsEc::CommandLpc3(request->command, request->command_version, &result,
                                           request->request.data(), request->request.count(),
                                           response.data(), response.size(), &actual_size);
  if (status != ZX_OK) {
    completer.ReplyError(status);
  } else {
    completer.ReplySuccess(fcrosec::wire::EcStatus(result),
                           fidl::VectorView<uint8_t>::FromExternal(response.data(), actual_size));
  }
}

static zx_driver_ops_t kDriverOps = {
    .version = DRIVER_OPS_VERSION,
    .bind = ChromiumosEcLpc::Bind,
};

}  // namespace chromiumos_ec_lpc

// clang-format off
ZIRCON_DRIVER(chromiumos-ec-lpc, chromiumos_ec_lpc::kDriverOps, "zircon", "0.1");
