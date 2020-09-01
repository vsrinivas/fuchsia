// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ot_radio.h"

#include <ctype.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/driver-unit-test/utils.h>
#include <lib/fidl/llcpp/server.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <zircon/compiler.h>
#include <zircon/status.h>
#include <zircon/time.h>

#include <iostream>
#include <iterator>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/driver.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/composite.h>
#include <ddktl/fidl.h>
#include <fbl/algorithm.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <hw/arch_ops.h>
#include <hw/reg.h>

#include "ot_radio_bootloader.h"

namespace ot {
namespace lowpan_spinel_fidl = ::llcpp::fuchsia::lowpan::spinel;

enum {
  FRAGMENT_PDEV,
  FRAGMENT_SPI,
  FRAGMENT_INT_GPIO,
  FRAGMENT_RESET_GPIO,
  FRAGMENT_BOOTLOADER_GPIO,
  FRAGMENT_COUNT,
};

OtRadioDevice::LowpanSpinelDeviceFidlImpl::LowpanSpinelDeviceFidlImpl(OtRadioDevice& ot_radio)
    : ot_radio_obj_(ot_radio) {}

zx_status_t OtRadioDevice::LowpanSpinelDeviceFidlImpl::Bind(async_dispatcher_t* dispatcher,
                                                            zx::channel channel) {
  fidl::OnUnboundFn<LowpanSpinelDeviceFidlImpl> on_unbound = [](LowpanSpinelDeviceFidlImpl* server,
                                                                fidl::UnbindInfo, zx::channel) {
    server->ot_radio_obj_.fidl_impl_obj_.release();
  };
  auto res = fidl::BindServer(dispatcher, std::move(channel), this, std::move(on_unbound));
  if (res.is_error())
    return res.error();
  ot_radio_obj_.fidl_binding_ = res.take_value();
  return ZX_OK;
}

void OtRadioDevice::LowpanSpinelDeviceFidlImpl::Open(OpenCompleter::Sync completer) {
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
    completer.ReplyError(lowpan_spinel_fidl::Error::UNSPECIFIED);
  }
}

void OtRadioDevice::LowpanSpinelDeviceFidlImpl::Close(CloseCompleter::Sync completer) {
  zx_status_t res = ot_radio_obj_.AssertResetPin();
  if (res == ZX_OK) {
    ot_radio_obj_.power_status_ = OT_SPINEL_DEVICE_OFF;
    completer.ReplySuccess();
  } else {
    zxlogf(ERROR, "Error in handling FIDL close req: %s, power status: %u",
           zx_status_get_string(res), ot_radio_obj_.power_status_);
    completer.ReplyError(lowpan_spinel_fidl::Error::UNSPECIFIED);
  }
}

void OtRadioDevice::LowpanSpinelDeviceFidlImpl::GetMaxFrameSize(
    GetMaxFrameSizeCompleter::Sync completer) {
  completer.Reply(kMaxFrameSize);
}

void OtRadioDevice::LowpanSpinelDeviceFidlImpl::SendFrame(::fidl::VectorView<uint8_t> data,
                                                          SendFrameCompleter::Sync completer) {
  if (ot_radio_obj_.power_status_ == OT_SPINEL_DEVICE_OFF) {
    (*ot_radio_obj_.fidl_binding_)->OnError(lowpan_spinel_fidl::Error::CLOSED, false);
  } else if (data.count() > kMaxFrameSize) {
    (*ot_radio_obj_.fidl_binding_)
        ->OnError(lowpan_spinel_fidl::Error::OUTBOUND_FRAME_TOO_LARGE, false);
  } else if (ot_radio_obj_.outbound_allowance_ == 0) {
    // Client violates the protocol, close FIDL channel and device. Will not send OnError event.
    ot_radio_obj_.power_status_ = OT_SPINEL_DEVICE_OFF;
    ot_radio_obj_.AssertResetPin();
    ot_radio_obj_.fidl_binding_->Close(ZX_ERR_IO_OVERRUN);
    completer.Close(ZX_ERR_IO_OVERRUN);
  } else {
    // All good, send out the frame.
    zx_status_t res = ot_radio_obj_.RadioPacketTx(data.begin(), data.count());
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
    uint32_t number_of_frames, ReadyToReceiveFramesCompleter::Sync completer) {
  zxlogf(DEBUG, "ot-radio: allow to receive %u frame", number_of_frames);
  ot_radio_obj_.inbound_allowance_ += number_of_frames;
  if (ot_radio_obj_.inbound_allowance_ > 0 && ot_radio_obj_.spinel_framer_.get()) {
    ot_radio_obj_.spinel_framer_->SetInboundAllowanceStatus(true);
    ot_radio_obj_.ReadRadioPacket();
  }
}

OtRadioDevice::OtRadioDevice(zx_device_t* device)
    : ddk::Device<OtRadioDevice, ddk::UnbindableNew, ddk::Messageable>(device),
      loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {}

zx_status_t OtRadioDevice::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
  DdkTransaction transaction(txn);
  lowpan_spinel_fidl::DeviceSetup::Dispatch(this, msg, &transaction);
  return transaction.Status();
}

void OtRadioDevice::SetChannel(zx::channel channel, SetChannelCompleter::Sync completer) {
  if (fidl_impl_obj_ != nullptr) {
    zxlogf(ERROR, "ot-radio: channel already set");
    completer.ReplyError(ZX_ERR_ALREADY_BOUND);
    return;
  }
  if (!channel.is_valid()) {
    completer.ReplyError(ZX_ERR_BAD_HANDLE);
    return;
  }
  fidl_impl_obj_ = std::make_unique<LowpanSpinelDeviceFidlImpl>(*this);
  auto status = fidl_impl_obj_->Bind(loop_.dispatcher(), std::move(channel));
  if (status == ZX_OK) {
    completer.ReplySuccess();
  } else {
    fidl_impl_obj_ = nullptr;
    completer.ReplyError(status);
  }
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
  composite_protocol_t composite;

  auto status = device_get_protocol(parent(), ZX_PROTOCOL_COMPOSITE, &composite);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Could not get composite protocol");
    return status;
  }

  zx_device_t* fragments[FRAGMENT_COUNT];
  size_t actual;
  composite_get_fragments(&composite, fragments, std::size(fragments), &actual);
  if (actual != std::size(fragments)) {
    zxlogf(ERROR, "could not get fragments");
    return ZX_ERR_NOT_SUPPORTED;
  }

  spi_ = ddk::SpiProtocolClient(fragments[FRAGMENT_SPI]);
  if (!spi_.is_valid()) {
    zxlogf(ERROR, "ot-radio %s: failed to acquire spi", __func__);
    return ZX_ERR_NOT_SUPPORTED;
  }

  status =
      device_get_protocol(fragments[FRAGMENT_INT_GPIO], ZX_PROTOCOL_GPIO, &gpio_[OT_RADIO_INT_PIN]);
  if (status != ZX_OK) {
    zxlogf(ERROR, "ot-radio %s: failed to acquire interrupt gpio", __func__);
    return status;
  }

  status = gpio_[OT_RADIO_INT_PIN].ConfigIn(GPIO_NO_PULL);

  if (status != ZX_OK) {
    zxlogf(ERROR, "ot-radio %s: failed to configure interrupt gpio", __func__);
    return status;
  }

  status = gpio_[OT_RADIO_INT_PIN].GetInterrupt(ZX_INTERRUPT_MODE_EDGE_LOW, &interrupt_);

  if (status != ZX_OK) {
    zxlogf(ERROR, "ot-radio %s: failed to get interrupt", __func__);
    return status;
  }

  status = device_get_protocol(fragments[FRAGMENT_RESET_GPIO], ZX_PROTOCOL_GPIO,
                               &gpio_[OT_RADIO_RESET_PIN]);
  if (status != ZX_OK) {
    zxlogf(ERROR, "ot-radio %s: failed to acquire reset gpio", __func__);
    return status;
  }

  status = gpio_[OT_RADIO_RESET_PIN].ConfigOut(1);

  if (status != ZX_OK) {
    zxlogf(ERROR, "ot-radio %s: failed to configure rst gpio, status = %d", __func__, status);
    return status;
  }

  status = device_get_protocol(fragments[FRAGMENT_BOOTLOADER_GPIO], ZX_PROTOCOL_GPIO,
                               &gpio_[OT_RADIO_BOOTLOADER_PIN]);
  if (status != ZX_OK) {
    zxlogf(ERROR, "ot-radio %s: failed to acquire radio bootloader pin", __func__);
    return status;
  }

  status = gpio_[OT_RADIO_BOOTLOADER_PIN].ConfigOut(1);

  if (status != ZX_OK) {
    zxlogf(ERROR, "ot-radio %s: failed to configure bootloader gpio, status = %d", __func__,
           status);
    return status;
  }

  uint32_t device_id;
  status = device_get_metadata(fragments[FRAGMENT_PDEV], DEVICE_METADATA_PRIVATE, &device_id,
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
  }
  return ZX_OK;
}

zx_status_t OtRadioDevice::HandleRadioRxFrame(uint8_t* frameBuffer, uint16_t length) {
  zxlogf(DEBUG, "ot-radio: received frame of len:%d", length);
  if (power_status_ == OT_SPINEL_DEVICE_ON) {
    ::fidl::VectorView<uint8_t> data;
    data.set_count(length);
    data.set_data(fidl::unowned_ptr(frameBuffer));
    zx_status_t res = (*fidl_binding_)->OnReceiveFrame(std::move(data));
    if (res != ZX_OK) {
      zxlogf(ERROR, "ot-radio: failed to send OnReceive() event due to %s",
             zx_status_get_string(res));
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

zx_status_t OtRadioDevice::DriverUnitTestGetNCPVersion() { return GetNCPVersion(); }

zx_status_t OtRadioDevice::GetNCPVersion() {
  spinel_framer_->SetInboundAllowanceStatus(true);
  inbound_allowance_ = kOutboundAllowanceInit;
  uint8_t get_ncp_version_cmd[] = {0x80, 0x02, 0x02};  // HEADER, CMD ID, PROPERTY ID
  // populate TID (lower 4 bits in header)
  get_ncp_version_cmd[0] = (get_ncp_version_cmd[0] & 0xf0) | (kGetNcpVersionTID & 0x0f);
  return RadioPacketTx(get_ncp_version_cmd, sizeof(get_ncp_version_cmd));
}

zx_status_t OtRadioDevice::DriverUnitTestGetResetEvent() {
  spinel_framer_->SetInboundAllowanceStatus(true);
  inbound_allowance_ = kOutboundAllowanceInit;
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
  zx::nanosleep(zx::deadline_after(zx::msec(100)));
  return status;
}

zx_status_t OtRadioDevice::Reset() {
  zx_status_t status = ZX_OK;
  zxlogf(DEBUG, "ot-radio: reset");

  status = gpio_[OT_RADIO_RESET_PIN].Write(0);
  if (status != ZX_OK) {
    zxlogf(ERROR, "ot-radio: gpio write failed");
    return status;
  }
  zx::nanosleep(zx::deadline_after(zx::msec(100)));

  status = gpio_[OT_RADIO_RESET_PIN].Write(1);
  if (status != ZX_OK) {
    zxlogf(ERROR, "ot-radio: gpio write failed");
    return status;
  }
  zx::nanosleep(zx::deadline_after(zx::msec(400)));
  return status;
}

zx_status_t OtRadioDevice::RadioThread() {
  zx_status_t status = ZX_OK;
  zxlogf(ERROR, "ot-radio: entered thread");

  while (true) {
    zx_port_packet_t packet = {};
    int timeout_ms = spinel_framer_->GetTimeoutMs();
    auto status = port_.wait(zx::deadline_after(zx::msec(timeout_ms)), &packet);

    if (status == ZX_ERR_TIMED_OUT) {
      spinel_framer_->TrySpiTransaction();
      ReadRadioPacket();
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
      spinel_framer_->HandleInterrupt();
      ReadRadioPacket();
      while (true) {
        uint8_t pin_level = 0;
        gpio_[OT_RADIO_INT_PIN].Read(&pin_level);
        // Interrupt has de-asserted or no more frames can be received.
        if (pin_level != 0 || inbound_allowance_ == 0) {
          zxlogf(DEBUG, "ot-radio: break int handling: int_pin:%d, inbound_allowance_:%d",
                 pin_level, inbound_allowance_);
          break;
        }
        spinel_framer_->HandleInterrupt();
        ReadRadioPacket();
      }
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
  auto cleanup = fbl::MakeAutoCall([&]() { ShutDown(); });

#ifdef INTERNAL_ACCESS
  // Update the NCP Firmware if new version is available
  bool update_fw = false;
  status = CheckFWUpdateRequired(&update_fw);
  if (status != ZX_OK) {
    zxlogf(ERROR, "ot-radio: CheckFWUpdateRequired failed with status: %d", status);
    return status;
  }

  if (update_fw) {
    // Print as it may be useful, expected to be rare occurence,
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

  for (attempts = 0; attempts < kGetNcpVersionMaxRetries; attempts++) {
    zxlogf(DEBUG, "ot-radio: sending GetNCPVersionCmd, attempt : %d / %d", attempts + 1,
           kGetNcpVersionMaxRetries);
    // Now get the ncp version
    auto status = GetNCPVersion();
    if (status != ZX_OK) {
      zxlogf(ERROR, "ot-radio: get ncp version failed with status: %d", status);
      return status;
    }

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

  // We want to update firmware if the versions don't match
  *update_fw = (cur_fw_version.compare(new_fw_version) != 0);
  if (*update_fw) {
    zxlogf(INFO, "ot-radio: cur_fw_version: %s, new_fw_version: %s", cur_fw_version.c_str(),
           new_fw_version.c_str());
  }
  return ZX_OK;
}
#endif

void OtRadioDevice::DdkRelease() { delete this; }

void OtRadioDevice::DdkUnbindNew(ddk::UnbindTxn txn) {
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

// clang-format off
ZIRCON_DRIVER_BEGIN(ot, ot::driver_ops, "ot_radio", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_COMPOSITE),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_GENERIC),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_OT_RADIO),
ZIRCON_DRIVER_END(ot)
    // clang-format on
