// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ot_radio.h"

#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/driver.h>
#include <lib/ddk/hw/reg.h>
#include <lib/ddk/metadata.h>
#include <lib/driver-unit-test/utils.h>
#include <lib/fidl/llcpp/server.h>
#include <lib/fit/defer.h>
#include <lib/zx/clock.h>
#include <lib/zx/time.h>
#include <stdio.h>
#include <sys/types.h>
#include <zircon/compiler.h>
#include <zircon/status.h>

#include <iterator>

#include <ddktl/fidl.h>
#include <fbl/auto_lock.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>

#include "src/connectivity/openthread/drivers/ot-radio/ot_radio_bind.h"
#include "src/connectivity/openthread/drivers/ot-radio/ot_radio_bootloader.h"

namespace ot {
namespace lowpan_spinel_fidl = fuchsia_lowpan_spinel;

constexpr uint32_t kRcpBusyPollingDelayMs = 10;
constexpr uint8_t kRcpHardResetPinAssertTimeMs = 100;
constexpr zx::duration kRcpHardResetRetryWaitTime = zx::msec(700);

OtRadioDevice::LowpanSpinelDeviceFidlImpl::LowpanSpinelDeviceFidlImpl(OtRadioDevice& ot_radio)
    : ot_radio_obj_(ot_radio) {}

void OtRadioDevice::LowpanSpinelDeviceFidlImpl::Bind(
    async_dispatcher_t* dispatcher, fidl::ServerEnd<lowpan_spinel_fidl::Device> channel) {
  fidl::OnUnboundFn<LowpanSpinelDeviceFidlImpl> on_unbound =
      [](LowpanSpinelDeviceFidlImpl* server, fidl::UnbindInfo /*unused*/,
         fidl::ServerEnd<lowpan_spinel_fidl::Device> /*unused*/) {
        server->ot_radio_obj_.fidl_impl_obj_.release();
      };
  ot_radio_obj_.fidl_binding_ =
      fidl::BindServer(dispatcher, std::move(channel), this, std::move(on_unbound));
}

void OtRadioDevice::LowpanSpinelDeviceFidlImpl::Open(OpenRequestView request,
                                                     OpenCompleter::Sync& completer) {
  zx_status_t res = ot_radio_obj_.Reset();
  if (res == ZX_OK) {
    zxlogf(DEBUG, "open succeed, returning");
    ot_radio_obj_.power_status_ = OT_SPINEL_DEVICE_ON;
    (*ot_radio_obj_.fidl_binding_)->OnReadyForSendFrames(kOutboundAllowanceInit);
    ot_radio_obj_.inbound_allowance_ = 0;
    ot_radio_obj_.outbound_allowance_ = kOutboundAllowanceInit;
    ot_radio_obj_.inbound_cnt_ = 0;
    ot_radio_obj_.outbound_cnt_ = 0;
    completer.ReplySuccess();
  } else {
    zxlogf(ERROR, "Error in handling FIDL close req: %s, power status: %u",
           zx_status_get_string(res), ot_radio_obj_.power_status_);
    completer.ReplyError(lowpan_spinel_fidl::wire::Error::kUnspecified);
  }
}

void OtRadioDevice::LowpanSpinelDeviceFidlImpl::Close(CloseRequestView request,
                                                      CloseCompleter::Sync& completer) {
  zx_status_t res = ot_radio_obj_.AssertResetPin();
  if (res == ZX_OK) {
    ot_radio_obj_.power_status_ = OT_SPINEL_DEVICE_OFF;
    completer.ReplySuccess();
  } else {
    zxlogf(ERROR, "Error in handling FIDL close req: %s, power status: %u",
           zx_status_get_string(res), ot_radio_obj_.power_status_);
    completer.ReplyError(lowpan_spinel_fidl::wire::Error::kUnspecified);
  }
}

void OtRadioDevice::LowpanSpinelDeviceFidlImpl::GetMaxFrameSize(
    GetMaxFrameSizeRequestView request, GetMaxFrameSizeCompleter::Sync& completer) {
  completer.Reply(kMaxFrameSize);
}

void OtRadioDevice::LowpanSpinelDeviceFidlImpl::SendFrame(SendFrameRequestView request,
                                                          SendFrameCompleter::Sync& completer) {
  if (ot_radio_obj_.power_status_ == OT_SPINEL_DEVICE_OFF) {
    (*ot_radio_obj_.fidl_binding_)->OnError(lowpan_spinel_fidl::wire::Error::kClosed, false);
  } else if (request->data.count() > kMaxFrameSize) {
    (*ot_radio_obj_.fidl_binding_)
        ->OnError(lowpan_spinel_fidl::wire::Error::kOutboundFrameTooLarge, false);
  } else if (ot_radio_obj_.outbound_allowance_ == 0) {
    // Client violates the protocol, close FIDL channel and device. Will not send OnError event.
    ot_radio_obj_.power_status_ = OT_SPINEL_DEVICE_OFF;
    ot_radio_obj_.AssertResetPin();
    ot_radio_obj_.fidl_binding_->Close(ZX_ERR_IO_OVERRUN);
    completer.Close(ZX_ERR_IO_OVERRUN);
  } else {
    // All good, send out the frame.
    zx_status_t res = ot_radio_obj_.RadioPacketTx(request->data.begin(), request->data.count());
    if (res != ZX_OK) {
      zxlogf(ERROR, "Error in handling send frame req: %s", zx_status_get_string(res));
    } else {
      ot_radio_obj_.outbound_allowance_--;
      ot_radio_obj_.outbound_cnt_++;
      zxlogf(DEBUG, "Successfully Txed pkt, total tx pkt %lu", ot_radio_obj_.outbound_cnt_);
      if ((ot_radio_obj_.outbound_cnt_ & 1) == 0) {
        (*ot_radio_obj_.fidl_binding_)->OnReadyForSendFrames(kOutboundAllowanceInc);
        ot_radio_obj_.outbound_allowance_ += kOutboundAllowanceInc;
      }
    }
  }
}

void OtRadioDevice::LowpanSpinelDeviceFidlImpl::ReadyToReceiveFrames(
    ReadyToReceiveFramesRequestView request, ReadyToReceiveFramesCompleter::Sync& completer) {
  zxlogf(DEBUG, "ot-radio: allow to receive %u frame", request->number_of_frames);
  ot_radio_obj_.inbound_allowance_ += request->number_of_frames;
  if (ot_radio_obj_.inbound_allowance_ > 0 && ot_radio_obj_.spinel_framer_.get()) {
    ot_radio_obj_.spinel_framer_->SetInboundAllowanceStatus(true);
    if (ot_radio_obj_.interrupt_is_asserted_) {
      // signal the event loop thread to handle interrupt
      ot_radio_obj_.InvokeInterruptHandler();
    }
    ot_radio_obj_.ReadRadioPacket();
  }
}

OtRadioDevice::OtRadioDevice(zx_device_t* device)
    : DeviceType(device), loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {}

void OtRadioDevice::SetChannel(SetChannelRequestView request,
                               SetChannelCompleter::Sync& completer) {
  if (fidl_impl_obj_ != nullptr) {
    zxlogf(ERROR, "ot-radio: channel already set");
    completer.ReplyError(ZX_ERR_ALREADY_BOUND);
    return;
  }
  if (!request->req.is_valid()) {
    completer.ReplyError(ZX_ERR_BAD_HANDLE);
    return;
  }
  fidl_impl_obj_ = std::make_unique<LowpanSpinelDeviceFidlImpl>(*this);
  fidl_impl_obj_->Bind(loop_.dispatcher(), std::move(request->req));
  completer.ReplySuccess();
}

zx_status_t OtRadioDevice::StartLoopThread() {
  zxlogf(DEBUG, "Start loop thread");
  zx_status_t status = loop_.StartThread("ot-stack-loop");
  if (status == ZX_OK) {
    thrd_status_.loop_thrd_running = true;
  }
  return status;
}

bool OtRadioDevice::RunUnitTests(void* ctx, zx_device_t* parent, zx_handle_t channel) {
  return driver_unit_test::RunZxTests("OtRadioTests", parent, channel);
}

zx_status_t OtRadioDevice::Init() {
  spi_ = ddk::SpiProtocolClient(parent(), "spi");
  if (!spi_.is_valid()) {
    zxlogf(ERROR, "ot-radio %s: failed to acquire spi", __func__);
    return ZX_ERR_NOT_SUPPORTED;
  }

  gpio_[OT_RADIO_INT_PIN] = ddk::GpioProtocolClient(parent(), "gpio-int");
  if (!gpio_[OT_RADIO_INT_PIN].is_valid()) {
    zxlogf(ERROR, "ot-radio %s: failed to acquire interrupt gpio", __func__);
    return ZX_ERR_NO_RESOURCES;
  }

  zx_status_t status = gpio_[OT_RADIO_INT_PIN].ConfigIn(GPIO_NO_PULL);

  if (status != ZX_OK) {
    zxlogf(ERROR, "ot-radio %s: failed to configure interrupt gpio", __func__);
    return status;
  }

  status = gpio_[OT_RADIO_INT_PIN].GetInterrupt(ZX_INTERRUPT_MODE_EDGE_LOW, &interrupt_);

  if (status != ZX_OK) {
    zxlogf(ERROR, "ot-radio %s: failed to get interrupt", __func__);
    return status;
  }

  gpio_[OT_RADIO_RESET_PIN] = ddk::GpioProtocolClient(parent(), "gpio-reset");
  if (!gpio_[OT_RADIO_RESET_PIN].is_valid()) {
    zxlogf(ERROR, "ot-radio %s: failed to acquire reset gpio", __func__);
    return ZX_ERR_NO_RESOURCES;
  }

  status = gpio_[OT_RADIO_RESET_PIN].ConfigOut(1);

  if (status != ZX_OK) {
    zxlogf(ERROR, "ot-radio %s: failed to configure rst gpio, status = %d", __func__, status);
    return status;
  }

  gpio_[OT_RADIO_BOOTLOADER_PIN] = ddk::GpioProtocolClient(parent(), "gpio-bootloader");
  if (!gpio_[OT_RADIO_BOOTLOADER_PIN].is_valid()) {
    zxlogf(ERROR, "ot-radio %s: failed to acquire radio bootloader pin", __func__);
    return ZX_ERR_NO_RESOURCES;
  }

  status = gpio_[OT_RADIO_BOOTLOADER_PIN].ConfigOut(1);

  if (status != ZX_OK) {
    zxlogf(ERROR, "ot-radio %s: failed to configure bootloader gpio, status = %d", __func__,
           status);
    return status;
  }

  size_t actual;
  uint32_t device_id;
  status = device_get_fragment_metadata(parent(), "pdev", DEVICE_METADATA_PRIVATE, &device_id,
                                        sizeof(device_id), &actual);
  if (status != ZX_OK || sizeof(device_id) != actual) {
    zxlogf(ERROR, "ot-radio: failed to read metadata");
    return status == ZX_OK ? ZX_ERR_INTERNAL : status;
  }

  spinel_framer_ = std::make_unique<ot::SpinelFramer>();
  spinel_framer_->Init(spi_);

  return ZX_OK;
}

zx_status_t OtRadioDevice::ReadRadioPacket() {
  if ((inbound_allowance_ > 0) && (spinel_framer_->IsPacketPresent())) {
    spinel_framer_->ReceivePacketFromRadio(spi_rx_buffer_, &spi_rx_buffer_len_);
    if (spi_rx_buffer_len_ > 0) {
      if (thrd_status_.loop_thrd_running) {
        async::PostTask(loop_.dispatcher(), [this, pkt = std::move(spi_rx_buffer_),
                                             len = std::move(spi_rx_buffer_len_)]() {
          this->HandleRadioRxFrame(pkt, len);
        });
      } else {
        // Loop thread is not running, this is one off case to be handled
        // Results either when running test or getting NCP version
        inbound_allowance_ = 0;
        spinel_framer_->SetInboundAllowanceStatus(false);
      }

      // Signal to driver test, waiting for a response
      sync_completion_signal(&spi_rx_complete_);
    }
    inbound_frame_available_ = true;
  } else {
    inbound_frame_available_ = false;
  }
  return ZX_OK;
}

zx_status_t OtRadioDevice::HandleRadioRxFrame(uint8_t* frameBuffer, uint16_t length) {
  zxlogf(DEBUG, "ot-radio: received frame of len:%d", length);
  if (power_status_ == OT_SPINEL_DEVICE_ON) {
    auto data = fidl::VectorView<uint8_t>::FromExternal(frameBuffer, length);
    fidl::Result result = (*fidl_binding_)->OnReceiveFrame(std::move(data));
    if (!result.ok()) {
      zxlogf(ERROR, "ot-radio: failed to send OnReceive() event due to %s",
             result.FormatDescription().c_str());
    }
    inbound_allowance_--;
    inbound_cnt_++;
    if ((inbound_allowance_ == 0) && spinel_framer_.get()) {
      spinel_framer_->SetInboundAllowanceStatus(false);
    }
  } else {
    zxlogf(ERROR, "OtRadioDevice::HandleRadioRxFrame(): Radio is off");
  }
  return ZX_OK;
}

zx_status_t OtRadioDevice::RadioPacketTx(uint8_t* frameBuffer, uint16_t length) {
  zxlogf(DEBUG, "ot-radio: RadioPacketTx");
  zx_port_packet packet = {PORT_KEY_TX_TO_RADIO, ZX_PKT_TYPE_USER, ZX_OK, {}};
  if (!port_.is_valid()) {
    return ZX_ERR_BAD_STATE;
  }
  memcpy(spi_tx_buffer_, frameBuffer, length);
  spi_tx_buffer_len_ = length;
  return port_.queue(&packet);
}

zx_status_t OtRadioDevice::InvokeInterruptHandler() {
  zxlogf(DEBUG, "ot-radio: InvokeInterruptHandler");
  zx_port_packet packet = {PORT_KEY_RADIO_IRQ, ZX_PKT_TYPE_USER, ZX_OK, {}};
  if (!port_.is_valid()) {
    return ZX_ERR_BAD_STATE;
  }
  return port_.queue(&packet);
}

bool OtRadioDevice::IsInterruptAsserted() {
  uint8_t pin_level = 0;
  gpio_[OT_RADIO_INT_PIN].Read(&pin_level);
  return pin_level == 0;
}

zx_status_t OtRadioDevice::DriverUnitTestGetNCPVersion() { return GetNCPVersion(); }

void OtRadioDevice::SetMaxInboundAllowance() {
  spinel_framer_->SetInboundAllowanceStatus(true);
  inbound_allowance_ = kOutboundAllowanceInit;
}

zx_status_t OtRadioDevice::GetNCPVersion() {
  SetMaxInboundAllowance();
  uint8_t get_ncp_version_cmd[] = {0x80, 0x02, 0x02};  // HEADER, CMD ID, PROPERTY ID
  // populate TID (lower 4 bits in header)
  get_ncp_version_cmd[0] = (get_ncp_version_cmd[0] & 0xf0) | (kGetNcpVersionTID & 0x0f);
  return RadioPacketTx(get_ncp_version_cmd, sizeof(get_ncp_version_cmd));
}

zx_status_t OtRadioDevice::DriverUnitTestGetResetEvent() {
  SetMaxInboundAllowance();
  return Reset();
}

zx_status_t OtRadioDevice::AssertResetPin() {
  zx_status_t status = ZX_OK;
  zxlogf(DEBUG, "ot-radio: assert reset pin");

  status = gpio_[OT_RADIO_RESET_PIN].Write(0);
  if (status != ZX_OK) {
    zxlogf(ERROR, "ot-radio: gpio write failed");
    return status;
  }
  zx::nanosleep(zx::deadline_after(zx::msec(kRcpHardResetPinAssertTimeMs)));
  return status;
}

zx_status_t OtRadioDevice::Reset() {
  zx_status_t status = ZX_OK;
  zxlogf(DEBUG, "ot-radio: reset");

  BeginResetRetryTimer();
  status = AssertResetPin();
  if (status != ZX_OK) {
    zxlogf(ERROR, "ot-radio: assert reset pin failed");
    return status;
  }

  status = gpio_[OT_RADIO_RESET_PIN].Write(1);
  if (status != ZX_OK) {
    zxlogf(ERROR, "ot-radio: gpio write failed");
  }
  return status;
}

uint32_t OtRadioDevice::GetTimeoutMs() {
  uint32_t timeout = spinel_framer_->GetTimeoutMs();
  if (!(IsHardResetting() && !inbound_frame_available_)) {
    return timeout;
  }
  // If reset is in progress and inbound frame is not available
  // apply short timeout
  timeout = std::min(kRcpBusyPollingDelayMs, timeout);
  return timeout;
}

void OtRadioDevice::HandleResetRetry() {
  if (ShouldRetryReset()) {
    zxlogf(INFO, "ot-radio: rcp unresponsive, firing hard reset again!");
    Reset();
  }
}

void OtRadioDevice::BeginResetRetryTimer() {
  hard_reset_end_ = zx::clock::get_monotonic() + kRcpHardResetRetryWaitTime;
}

bool OtRadioDevice::IsHardResetting() { return hard_reset_end_ != zx::time(0); }

bool OtRadioDevice::ShouldRetryReset() {
  if (!IsHardResetting()) {
    return false;
  }
  if (inbound_frame_available_) {
    zxlogf(DEBUG, "ot-radio: rcp back online");
    hard_reset_end_ = zx::time(0);
    return false;
  }
  zx::time cur_time_us = zx::clock::get_monotonic();
  if (hard_reset_end_ > cur_time_us) {
    zxlogf(DEBUG, "ot-radio: waiting for rcp to back online");
    return false;
  }
  return true;
}

zx_status_t OtRadioDevice::RadioThread() {
  zx_status_t status = ZX_OK;
  zxlogf(INFO, "ot-radio: entered thread");

  while (true) {
    zx_port_packet_t packet = {};
    uint32_t timeout_ms = GetTimeoutMs();
    auto status = port_.wait(zx::deadline_after(zx::msec(timeout_ms)), &packet);

    if (status == ZX_ERR_TIMED_OUT) {
      spinel_framer_->TrySpiTransaction();
      ReadRadioPacket();
      HandleResetRetry();
      continue;
    } else if (status != ZX_OK) {
      zxlogf(ERROR, "ot-radio: port wait failed: %d", status);
      return thrd_error;
    }

    if (packet.key == PORT_KEY_EXIT_THREAD) {
      break;
    } else if (packet.key == PORT_KEY_RADIO_IRQ) {
      interrupt_.ack();
      zxlogf(DEBUG, "ot-radio: interrupt");
      do {
        spinel_framer_->HandleInterrupt();
        ReadRadioPacket();

        if (!inbound_frame_available_) {
          zxlogf(DEBUG, "ot-radio: new packet unavailable, sleeping");
          zx::nanosleep(zx::deadline_after(zx::msec(kRcpBusyPollingDelayMs)));
        }

        HandleResetRetry();

        interrupt_is_asserted_ = IsInterruptAsserted();
      } while (interrupt_is_asserted_ && inbound_allowance_ != 0);
    } else if (packet.key == PORT_KEY_TX_TO_RADIO) {
      spinel_framer_->SendPacketToRadio(spi_tx_buffer_, spi_tx_buffer_len_);
    }
  }
  zxlogf(DEBUG, "ot-radio: exiting");

  return status;
}

zx_status_t OtRadioDevice::CreateBindAndStart(void* ctx, zx_device_t* parent) {
  std::unique_ptr<OtRadioDevice> ot_radio_dev;
  zx_status_t status = Create(ctx, parent, &ot_radio_dev);
  if (status != ZX_OK) {
    return status;
  }

  status = ot_radio_dev->Bind();
  if (status != ZX_OK) {
    return status;
  }
  // device intentionally leaked as it is now held by DevMgr
  auto dev_ptr = ot_radio_dev.release();

  status = dev_ptr->Start();
  if (status != ZX_OK) {
    return status;
  }

  return status;
}

zx_status_t OtRadioDevice::Create(void* ctx, zx_device_t* parent,
                                  std::unique_ptr<OtRadioDevice>* out) {
  auto dev = std::make_unique<OtRadioDevice>(parent);
  zx_status_t status = dev->Init();

  if (status != ZX_OK) {
    zxlogf(ERROR, "ot-radio: Driver init failed %d", status);
    return status;
  }

  *out = std::move(dev);

  return ZX_OK;
}

zx_status_t OtRadioDevice::Bind() {
  zx_status_t status = DdkAdd(ddk::DeviceAddArgs("ot-radio").set_proto_id(ZX_PROTOCOL_OT_RADIO));
  if (status != ZX_OK) {
    zxlogf(ERROR, "ot-radio: Could not create device: %d", status);
    return status;
  } else {
    zxlogf(DEBUG, "ot-radio: Added device");
  }
  return status;
}

zx_status_t OtRadioDevice::CreateAndBindPortToIntr() {
  zx_status_t status = zx::port::create(ZX_PORT_BIND_TO_INTERRUPT, &port_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "ot-radio: port create failed %d", status);
    return status;
  }

  status = interrupt_.bind(port_, PORT_KEY_RADIO_IRQ, 0);
  if (status != ZX_OK) {
    zxlogf(ERROR, "ot-radio: interrupt bind failed %d", status);
    return status;
  }

  return ZX_OK;
}

void OtRadioDevice::StartRadioThread() {
  auto callback = [](void* cookie) {
    return reinterpret_cast<OtRadioDevice*>(cookie)->RadioThread();
  };
  int ret = thrd_create_with_name(&thread_, callback, this, "ot-radio-thread");
  ZX_DEBUG_ASSERT(ret == thrd_success);

  // Set status flag so shutdown can take appropriate action
  // cleared by StopRadioThread
  thrd_status_.radio_thrd_running = true;
}

zx_status_t OtRadioDevice::Start() {
  zx_status_t status = CreateAndBindPortToIntr();
  if (status != ZX_OK) {
    return status;
  }

  StartRadioThread();
  auto cleanup = fit::defer([&]() { ShutDown(); });

#ifdef INTERNAL_ACCESS
  // Update the NCP Firmware if new version is available
  bool update_fw = false;
  status = CheckFWUpdateRequired(&update_fw);
  if (status != ZX_OK) {
    zxlogf(ERROR, "ot-radio: CheckFWUpdateRequired failed with status: %d", status);
    return status;
  }

  if (update_fw) {
    // Print as it may be useful, expected to be rare occurrence,
    zxlogf(INFO, "ot-radio: Will start FW update");

    // Stop the loop for handling port events, so port can be used by bootloader
    StopRadioThread();

    // Update firmware here
    OtRadioDeviceBootloader dev_bl(this);
    OtRadioBlResult result = dev_bl.UpdateRadioFirmware();
    if (result != BL_RET_SUCCESS) {
      zxlogf(ERROR, "ot-radio: radio firmware update failed with %d. Last zx_status %d", result,
             dev_bl.GetLastZxStatus());
      return ZX_ERR_INTERNAL;
    }

    zxlogf(INFO, "ot-radio: FW update done successfully");

    // Restart the Radio thread:
    StartRadioThread();
  } else {
    zxlogf(DEBUG, "ot-radio: NCP firmware is already up-to-date");
  }
#endif

  status = StartLoopThread();
  if (status != ZX_OK) {
    zxlogf(ERROR, "ot-radio: Could not start loop thread");
    return status;
  }

  zxlogf(DEBUG, "ot-radio: Started thread");

  cleanup.cancel();

  return status;
}

#ifdef INTERNAL_ACCESS
zx_status_t OtRadioDevice::CheckFWUpdateRequired(bool* update_fw) {
  *update_fw = false;

  // Get the new firmware version:
  std::string new_fw_version = GetNewFirmwareVersion();
  if (new_fw_version.size() == 0) {
    // Invalid version string indicates invalid firmware
    zxlogf(ERROR, "ot-radio: The new firmware is invalid");
    *update_fw = false;
    // Return error instead of ZX_OK and just not-updating,
    // may point to some bug
    return ZX_ERR_NO_RESOURCES;
  }

  int attempts;
  bool response_received = false;

  // Now get the ncp version
  auto status = GetNCPVersion();
  if (status != ZX_OK) {
    zxlogf(ERROR, "ot-radio: get ncp version failed with status: %d", status);
    return status;
  }

  for (attempts = 0; attempts < kGetNcpVersionMaxRetries; attempts++) {
    zxlogf(DEBUG, "ot-radio: waiting for response for GetNCPVersion cmd, attempt: %d / %d",
           attempts + 1, kGetNcpVersionMaxRetries);

    // Simply update the allowance for each attempt. Radio will send
    // response to GetNCPVersion to us eventually. Ignore any response to
    // earlier commands.
    SetMaxInboundAllowance();

    // Wait for response to arrive, signaled by spi_rx_complete_
    status = sync_completion_wait(&spi_rx_complete_, ZX_SEC(10));
    sync_completion_reset(&spi_rx_complete_);
    if (status != ZX_OK) {
      zxlogf(ERROR,
             "ot-radio: sync_completion_wait failed with status: %d, \
         this means firmware may be behaving incorrectly\n",
             status);
      // We want to update fw in this case
      *update_fw = true;
      return ZX_OK;
    }

    // Check for matching TID in spinel header
    if ((spi_rx_buffer_[0] & 0xf) == kGetNcpVersionTID) {
      response_received = true;
      break;
    }
  }

  if (!response_received) {
    zxlogf(ERROR, "ot-radio: no matching response is received for get ncp version command.");
    zxlogf(ERROR, "ot-radio: updating the the firmware");
    // This can again mean bad firmware, so update the firmware:
    *update_fw = true;
    return ZX_OK;
  }

  // Response is received, copy it to the string cur_fw_version
  // First make sure that last character is null
  ZX_DEBUG_ASSERT(spi_rx_buffer_len_ <= kMaxFrameSize);
  spi_rx_buffer_[spi_rx_buffer_len_ - 1] = '\0';
  zxlogf(DEBUG, "ot-radio: response received size = %d, value : %s", spi_rx_buffer_len_,
         reinterpret_cast<char*>(&(spi_rx_buffer_[3])));
  std::string cur_fw_version;
  cur_fw_version.assign(reinterpret_cast<char*>(&(spi_rx_buffer_[3])));
  zxlogf(INFO, "ot-radio: cur_fw_version: %s", cur_fw_version.c_str());

  // We want to update firmware if the versions don't match
  *update_fw = (cur_fw_version.compare(new_fw_version) != 0);
  if (*update_fw) {
    zxlogf(INFO, "ot-radio: fw update required, new_fw_version: %s", new_fw_version.c_str());
  }
  return ZX_OK;
}
#endif

void OtRadioDevice::DdkRelease() { delete this; }

void OtRadioDevice::DdkUnbind(ddk::UnbindTxn txn) {
  ShutDown();
  txn.Reply();
}

void OtRadioDevice::StopRadioThread() {
  if (thrd_status_.radio_thrd_running) {
    zx_port_packet packet = {PORT_KEY_EXIT_THREAD, ZX_PKT_TYPE_USER, ZX_OK, {}};
    port_.queue(&packet);
    thrd_join(thread_, NULL);
    thrd_status_.radio_thrd_running = false;
  }
}

void OtRadioDevice::StopLoopThread() {
  if (thrd_status_.loop_thrd_running) {
    loop_.Shutdown();
    thrd_status_.loop_thrd_running = false;
  }
}

zx_status_t OtRadioDevice::ShutDown() {
  StopRadioThread();

  gpio_[OT_RADIO_INT_PIN].ReleaseInterrupt();
  interrupt_.destroy();

  StopLoopThread();

  return ZX_OK;
}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = OtRadioDevice::CreateBindAndStart;
  ops.run_unit_tests = OtRadioDevice::RunUnitTests;
  return ops;
}();

}  // namespace ot

ZIRCON_DRIVER(ot, ot::driver_ops, "ot_radio", "0.1");
