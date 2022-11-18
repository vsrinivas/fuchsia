// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/drivers/lib/aml-dsp/dsp.h"

#include <lib/ddk/debug.h>

namespace {
using fuchsia_hardware_mailbox::wire::MboxTx;
}

namespace audio::aml_g12 {

AmlMailboxDevice::AmlMailboxDevice(MailboxClient client) : client_(std::move(client)) {}

zx_status_t AmlMailboxDevice::SendDspCommand(uint8_t* data, uint8_t size, uint8_t cmd) {
  MboxTx txmdata = {.cmd = cmd, .tx_buffer = fidl::VectorView<uint8_t>::FromExternal(data, size)};
  auto send_command_result = client_->SendCommand(kMailboxDsp, txmdata);
  if (!send_command_result.ok()) {
    zxlogf(ERROR, "Dsp send cmd: %d, send data failed", cmd);
    return send_command_result.status();
  }

  auto receive_data_result = client_->ReceiveData(kMailboxDsp, size);
  if (!receive_data_result.ok()) {
    zxlogf(ERROR, "Dsp send cmd: %d, receive data failed", cmd);
    return receive_data_result.status();
  }

  using fuchsia_hardware_mailbox::wire::DeviceReceiveDataResponse;
  DeviceReceiveDataResponse* response = receive_data_result->value();
  memcpy(data, &response->mdata.rx_buffer[0], size);
  return ZX_OK;
}

zx_status_t AmlMailboxDevice::DspCreateProcessingTask(AddrInfo* arg, uint8_t size) {
  zx_status_t status =
      SendDspCommand(reinterpret_cast<uint8_t*>(arg), size, MBX_CMD_DATA_THREAD_CREATE);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Dsp data thread create failed: %s", zx_status_get_string(status));
  }
  return status;
}

zx_status_t AmlMailboxDevice::DspProcessTaskStart() {
  zx_status_t status = SendDspCommand(nullptr, 0, MBX_CMD_DATA_THREAD_START);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Dsp data thread start failed: %s", zx_status_get_string(status));
  }
  return status;
}

zx_status_t AmlMailboxDevice::DspProcessTaskStop() {
  zx_status_t status = SendDspCommand(nullptr, 0, MBX_CMD_DATA_THREAD_STOP);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Dsp data thread stop failed: %s", zx_status_get_string(status));
  }
  return status;
}

zx_status_t AmlMailboxDevice::DspProcessTaskPosition(uint32_t position) {
  zx_status_t status = SendDspCommand(reinterpret_cast<uint8_t*>(&position), sizeof(position),
                                      MBX_CMD_DATA_THREAD_POSITION);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Dsp data thread update position failed: %s", zx_status_get_string(status));
  }
  return status;
}

AmlDspDevice::AmlDspDevice(DspClient client) : client_(std::move(client)) {}

zx_status_t AmlDspDevice::DspHwInit() {
  // Since it is unknown whether the HW DSP has firmware running, execute the Stop command first,
  // then execute the firmware loading command LoadFirmware, and finally execute the firmware
  // startup command Start.
  auto dsp_stop_result = client_->Stop();
  // Load DSP FW.
  auto dsp_load_result =
      client_->LoadFirmware(fidl::StringView::FromExternal(TDM_DSP_FIRMWARE_NAME));
  if (!dsp_load_result.ok()) {
    zxlogf(ERROR, "Failed to dsp load firmware: %s",
           zx_status_get_string(dsp_load_result.status()));
    return dsp_load_result.status();
  }

  // Start DSP.
  auto dsp_start_result = client_->Start();
  if (!dsp_start_result.ok()) {
    zxlogf(ERROR, "Failed to dsp start: %s", zx_status_get_string(dsp_start_result.status()));
    return dsp_start_result.status();
  }
  return ZX_OK;
}

}  // namespace audio::aml_g12
