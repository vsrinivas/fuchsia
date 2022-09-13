// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-dsp.h"

#include <fidl/fuchsia.hardware.dsp/cpp/markers.h>
#include <fidl/fuchsia.hardware.mailbox/cpp/markers.h>
#include <fuchsia/hardware/platform/device/c/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/device-protocol/platform-device.h>
#include <lib/fdf/cpp/dispatcher.h>
#include <lib/fit/defer.h>
#include <lib/mmio/mmio-buffer.h>
#include <lib/zx/vmar.h>
#include <stdlib.h>
#include <zircon/assert.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/smc.h>

#include "src/devices/dsp/drivers/aml-dsp/aml_dsp_bind.h"

namespace aml_dsp {

zx_status_t AmlDsp::Init() {
  zx_status_t status;

  auto pdev = ddk::PDev::FromFragment(parent());
  if (!pdev.is_valid()) {
    zxlogf(ERROR, "Failed to get ZX_PROTOCOL_PDEV");
    return ZX_ERR_NO_RESOURCES;
  }

  if ((status = pdev.GetSmc(0, &smc_resource_)) != ZX_OK) {
    zxlogf(ERROR, "pdev.GetSmc failed %s", zx_status_get_string(status));
    return status;
  }

  auto endpoints = fidl::CreateEndpoints<fuchsia_hardware_mailbox::Device>();
  if (endpoints.is_error()) {
    zxlogf(ERROR, "Failed to create endpoints");
    return endpoints.status_value();
  }

  status = DdkConnectFragmentFidlProtocol("dsp-mailbox", std::move(endpoints->server));
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to connect fidl protocol: %s", zx_status_get_string(status));
    return status;
  }

  dsp_mailbox_ = fidl::BindSyncClient(std::move(endpoints->client));

  return status;
}

void AmlDsp::DdkRelease() { delete this; }

void AmlDsp::LoadFirmware(LoadFirmwareRequestView request, LoadFirmwareCompleter::Sync& completer) {
}

void AmlDsp::Start(StartCompleter::Sync& completer) {}

void AmlDsp::Stop(StopCompleter::Sync& completer) {}

zx_status_t AmlDsp::Bind() {
  outgoing_dir_.emplace(dispatcher_);
  outgoing_dir_->svc_dir()->AddEntry(
      fidl::DiscoverableProtocolName<FidlDsp::DspDevice>,
      fbl::MakeRefCounted<fs::Service>(
          [device = this](fidl::ServerEnd<FidlDsp::DspDevice> request) mutable {
            fidl::BindServer(device->dispatcher_, std::move(request), device);
            return ZX_OK;
          }));

  auto endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
  if (endpoints.is_error()) {
    return endpoints.status_value();
  }

  auto status = outgoing_dir_->Serve(std::move(endpoints->server));
  if (status != ZX_OK) {
    zxlogf(ERROR, "failed to service the outgoing directory: %s", zx_status_get_string(status));
    return status;
  }

  std::array offers = {
      fidl::DiscoverableProtocolName<FidlDsp::DspDevice>,
  };

  return DdkAdd(ddk::DeviceAddArgs("aml-dsp")
                    .set_flags(DEVICE_ADD_MUST_ISOLATE)
                    .set_fidl_protocol_offers(offers)
                    .set_outgoing_dir(endpoints->client.TakeChannel())
                    .set_proto_id(ZX_PROTOCOL_AML_DSP));
}

zx_status_t AmlDsp::Create(void* ctx, zx_device_t* parent) {
  zx_status_t status;
  fbl::AllocChecker ac;
  auto pdev = ddk::PDev::FromFragment(parent);

  if (!pdev.is_valid()) {
    zxlogf(ERROR, "Failed to get ZX_PROTOCOL_PDEV");
    return ZX_ERR_NO_RESOURCES;
  }

  pdev_device_info_t info;
  if ((status = pdev.GetDeviceInfo(&info)) != ZX_OK) {
    zxlogf(ERROR, "aml_dsp: pdev_get_device_info failed");
    return ZX_ERR_NOT_SUPPORTED;
  }

  std::optional<ddk::MmioBuffer> dsp_addr;
  if ((status = pdev.MapMmio(0, &dsp_addr)) != ZX_OK) {
    zxlogf(ERROR, "pdev.MapMmio dsp_addr failed %s", zx_status_get_string(status));
    return status;
  }

  std::optional<ddk::MmioBuffer> dsp_sram_addr;
  if ((status = pdev.MapMmio(1, &dsp_sram_addr)) != ZX_OK) {
    zxlogf(ERROR, "pdev.MapMmio dsp_sram_addr failed %s", zx_status_get_string(status));
    return status;
  }

  ddk::ClockProtocolClient dsp_clk_sel(parent, "dsp-clk-sel");
  if (!dsp_clk_sel.is_valid()) {
    zxlogf(ERROR, "Find dsp-clk-sel failed");
  }

  ddk::ClockProtocolClient dsp_clk_gate(parent, "dsp-clk-gate");
  if (!dsp_clk_gate.is_valid()) {
    zxlogf(ERROR, "Find dsp-clk-gate failed");
  }

  async_dispatcher_t* dispatcher = fdf::Dispatcher::GetCurrent()->async_dispatcher();
  auto dev =
      fbl::make_unique_checked<AmlDsp>(&ac, parent, *std::move(dsp_addr), *std::move(dsp_sram_addr),
                                       dsp_clk_sel, dsp_clk_gate, dispatcher);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  status = dev->Init();
  if (status != ZX_OK) {
    zxlogf(ERROR, "AmlDsp initialization failed %s", zx_status_get_string(status));
  }

  status = dev->Bind();
  if (status != ZX_OK) {
    zxlogf(ERROR, "Bind failed: %s", zx_status_get_string(status));
    return status;
  }

  __UNUSED auto* dummy = dev.release();
  return ZX_OK;
}

static zx_driver_ops_t dsp_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = AmlDsp::Create,
};

}  // namespace aml_dsp

ZIRCON_DRIVER(aml_dsp, aml_dsp::dsp_driver_ops, "zircon", "0.1");
