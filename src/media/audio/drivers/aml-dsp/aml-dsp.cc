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

#include "src/devices/mailbox/drivers/aml-mailbox/meson_mhu_common.h"
#include "src/media/audio/drivers/aml-dsp/aml_dsp_bind.h"

namespace {

using fuchsia_hardware_mailbox::wire::DeviceReceiveDataResponse;
using fuchsia_hardware_mailbox::wire::MboxTx;

constexpr uint32_t kDspDefaultLoadAddress = 0xfffa0000; /* DSP default load address */
constexpr DspStartMode kStartMode =
    DspStartMode::kSmcStartMode;  /* 0: scpi start mode, 1: smc start mode */
constexpr bool kPmSupport = true; /* Support power management */
constexpr uint32_t kHifiBase = 0xf7030000;
constexpr uint8_t kPmDspa = 10;
constexpr uint8_t kPwrOn = 1;
constexpr uint8_t kPwrOff = 0;
constexpr uint8_t kStrobe = 1;
constexpr uint8_t kScpiCmdHifisuspend = 0x4e;
constexpr uint8_t kScpiCmdHifiresume = 0x4f;
constexpr uint8_t kDspSourceSelect800M = 1; /* DSP clock source selection: 1 800M */
constexpr uint8_t kDspSourceSelect24M = 0;  /* DSP clock source selection: 0 24M */
constexpr uint32_t kStartHifi = 0x82000090;
constexpr uint32_t kDspSecPowerSrt = 0x82000092;
constexpr uint8_t kDefaultTemp = 1;
constexpr uint8_t kVectorOffset = 1;
constexpr uint8_t kStrobeOffset = 2;
constexpr uint8_t kNone = 0;

}  // namespace

namespace aml_dsp {

zx_status_t AmlDsp::DspSmcCall(uint32_t func_id, uint8_t arg1, uint32_t arg2, uint32_t arg3) {
  zx_smc_parameters_t smc_params = {
      .func_id = func_id,
      .arg1 = arg1,
      .arg2 = arg2,
      .arg3 = arg3,
  };

  zx_smc_result_t smc_result;
  zx_status_t status = zx_smc_call(smc_resource_.get(), &smc_params, &smc_result);
  if (status != ZX_OK) {
    zxlogf(ERROR, "zx_smc_call: %x failed: %s", func_id, zx_status_get_string(status));
  }

  return status;
}

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

  dsp_mailbox_ = fidl::WireSyncClient(std::move(endpoints->client));

  return status;
}

zx_status_t AmlDsp::ScpiSendData(uint8_t* data, uint8_t size, uint8_t cmd) {
  MboxTx txmdata = {.cmd = cmd, .tx_buffer = fidl::VectorView<uint8_t>::FromExternal(data, size)};
  auto send_command_result = dsp_mailbox_->SendCommand(kMailboxScpi, txmdata);
  if (!send_command_result.ok()) {
    zxlogf(ERROR, "Scpi send cmd: %d, send data failed", cmd);
    return send_command_result.status();
  }

  auto receive_data_result = dsp_mailbox_->ReceiveData(kMailboxScpi, size);
  if (!receive_data_result.ok()) {
    zxlogf(ERROR, "Scpi send cmd: %d, receive data failed", cmd);
    return receive_data_result.status();
  }

  DeviceReceiveDataResponse* response = receive_data_result->value();
  if (strncmp(reinterpret_cast<char*>(&response->mdata.rx_buffer[0]), reinterpret_cast<char*>(data),
              size) != 0) {
    zxlogf(ERROR, "Dsp response failed");
    return ZX_ERR_IO_DATA_LOSS;
  } else {
    return ZX_OK;
  }
}

void AmlDsp::DspSuspend() {
  const char message[] = "SCPI_CMD_HIFISUSPEND";

  if (ScpiSendData(reinterpret_cast<uint8_t*>(const_cast<char*>(message)), sizeof(message),
                   kScpiCmdHifisuspend) == ZX_OK) {
    dsp_clk_sel_.SetInput(kDspSourceSelect24M); /* Adjust DSP clock to 24MHz */
  } else {
    zxlogf(ERROR, "Dsp suspend failed");
  }
}

void AmlDsp::DspResume() {
  const char message[] = "SCPI_CMD_HIFIRESUME";

  if (ScpiSendData(reinterpret_cast<uint8_t*>(const_cast<char*>(message)), sizeof(message),
                   kScpiCmdHifiresume) == ZX_OK) {
    dsp_clk_sel_.SetInput(kDspSourceSelect800M); /*switch dsp clk to normal*/
  } else {
    zxlogf(ERROR, "Dsp resume failed");
  }
}

void AmlDsp::DdkResume(ddk::ResumeTxn txn) {
  zxlogf(DEBUG, "begin DdkResume() - Requested State: %d", txn.requested_state());
  /* DspResume() will only be executed when the DSP core is powered and needs to be managed with
   * power to the DSP core. */
  if ((dsp_start_) && (kPmSupport)) {
    zxlogf(DEBUG, "AP send resume cmd to dsp.\n");
    DspResume();
  }

  txn.Reply(ZX_OK, DEV_POWER_STATE_D0, txn.requested_state());
}

void AmlDsp::DdkSuspend(ddk::SuspendTxn txn) {
  zxlogf(DEBUG, "begin DdkSuspend() - Suspend Reason: %d", txn.suspend_reason());
  if ((txn.suspend_reason() & DEVICE_MASK_SUSPEND_REASON) != DEVICE_SUSPEND_REASON_MEXEC) {
    txn.Reply(ZX_OK, txn.requested_state());
    return;
  }

  ZX_DEBUG_ASSERT((txn.suspend_reason() & DEVICE_MASK_SUSPEND_REASON) ==
                  DEVICE_SUSPEND_REASON_MEXEC);
  /* DspSuspend() will only be executed when the DSP core is powered and needs to be managed with
   * power to the DSP core.*/
  if ((dsp_start_) && (kPmSupport)) {
    zxlogf(DEBUG, "AP send suspend cmd to dsp.");
    DspSuspend();
  }

  zxlogf(INFO, "end DdkSuspend()");
  txn.Reply(ZX_OK, txn.requested_state());
}

void AmlDsp::DdkUnbind(ddk::UnbindTxn txn) { txn.Reply(); }

void AmlDsp::DdkRelease() { delete this; }

zx_status_t AmlDsp::DspLoadFw(fidl::StringView fw_name) {
  zx::vmo fw_vmo;
  size_t fw_size;
  zx_status_t status =
      load_firmware(parent(), fw_name.data(), fw_vmo.reset_and_get_address(), &fw_size);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Error fetching firmware (err %s)", zx_status_get_string(status));
    return status;
  }

  zx_vaddr_t ptr;
  zx::vmar::root_self()->map(ZX_VM_PERM_READ, 0, fw_vmo, 0, fw_size, &ptr);
  uint32_t* data = reinterpret_cast<uint32_t*>(ptr);

  for (size_t i = 0; i < fw_size; i += sizeof(uint32_t)) {
    dsp_sram_addr_.Write32(*data, i);
    data++;
  }

  firmware_loaded_ = 1; /* firmware loaded successfully */
  return status;
}

zx_status_t AmlDsp::DspStop() {
  if (dsp_start_) {
    zx_status_t status = DspSmcCall(kDspSecPowerSrt, kPmDspa, kPwrOff, kNone);
    if (status != ZX_OK) {
      return status;
    }

    dsp_clk_gate_.Disable();
    dsp_start_ = 0;
  } else {
    zxlogf(WARNING, "DSP is not started and cannot be stopped");
    return ZX_ERR_BAD_STATE;
  }

  return ZX_OK;
}

zx_status_t AmlDsp::DspStart() {
  /* Before starting the DSP, it must be determined whether the firmware is loaded successfully */
  if (firmware_loaded_ == 0) {
    zxlogf(ERROR, "Please load the firmware first");
    return ZX_ERR_BAD_STATE;
  }

  if (dsp_start_) {
    zxlogf(ERROR, "duplicate start dsp");
    return ZX_ERR_BAD_STATE;
  }

  zx_status_t status = DspSmcCall(kDspSecPowerSrt, kPmDspa, kPwrOn, kNone);
  if (status != ZX_OK) {
    return status;
  }

  /* Configure DSP Clock */
  dsp_clk_sel_.SetInput(kDspSourceSelect800M);
  dsp_clk_gate_.Enable();

  uint32_t StatVectorSel = (kHifiBase != kDspDefaultLoadAddress);
  uint32_t tmp = kDefaultTemp | StatVectorSel << kVectorOffset | kStrobe << kStrobeOffset;

  switch (kStartMode) {
    case DspStartMode::kScpiStartMode:
      zxlogf(INFO, "The dsp start mode is SCPI");
      return ZX_ERR_INVALID_ARGS;

    case DspStartMode::kSmcStartMode:
      status = DspSmcCall(kStartHifi, kNone, kHifiBase, tmp);
      if (status != ZX_OK) {
        return status;
      }
      break;

    default:
      zxlogf(ERROR, "The dsp start mode error, Start dsp failed");
      return ZX_ERR_OUT_OF_RANGE;
  }

  dsp_start_ = 1;
  return status;
}

void AmlDsp::LoadFirmware(LoadFirmwareRequestView request, LoadFirmwareCompleter::Sync& completer) {
  fidl::StringView fw_name = request->fw_name;

  zx_status_t status = DspLoadFw(fw_name);
  if (status != ZX_OK) {
    completer.ReplyError(status);
  } else {
    completer.ReplySuccess();
  }
}

void AmlDsp::Start(StartCompleter::Sync& completer) {
  zx_status_t status = DspStart();
  if (status != ZX_OK) {
    completer.ReplyError(status);
  } else {
    completer.ReplySuccess();
  }
}

void AmlDsp::Stop(StopCompleter::Sync& completer) {
  zx_status_t status = DspStop();
  if (status != ZX_OK) {
    completer.ReplyError(status);
  } else {
    completer.ReplySuccess();
  }
}

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
