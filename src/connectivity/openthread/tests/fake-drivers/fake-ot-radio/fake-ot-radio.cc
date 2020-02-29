// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fake-ot-radio.h"

#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/fidl-async/cpp/async_bind.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <zircon/compiler.h>
#include <zircon/status.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/driver.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <ddktl/fidl.h>
#include <fbl/algorithm.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>

namespace fake_ot {
namespace lowpan_spinel_fidl = ::llcpp::fuchsia::lowpan::spinel;

enum {
  PORT_KEY_EXIT_THREAD,
  PORT_KEY_INBOUND_FRAME,
  PORT_KEY_INBOUND_ALLOWANCE,
};

constexpr uint8_t kNcpResetEvent[] = {0x80, 0x06, 0x0, 0x70};
constexpr uint8_t kNcpVerRequest[] = {0x81, 0x02, 0x02};
constexpr uint8_t kNcpVerReply[] = {
    0x81, 0x06, 0x02, 0x4F, 0x50, 0x45, 0x4E, 0x54, 0x48, 0x52, 0x45, 0x41, 0x44, 0x2F, 0x31,
    0x2E, 0x30, 0x64, 0x37, 0x32, 0x35, 0x3B, 0x20, 0x52, 0x43, 0x50, 0x2D, 0x4E, 0x65, 0x77,
    0x6D, 0x61, 0x6E, 0x31, 0x3B, 0x20, 0x46, 0x65, 0x62, 0x20, 0x32, 0x34, 0x20, 0x32, 0x30,
    0x31, 0x39, 0x20, 0x31, 0x33, 0x3A, 0x33, 0x38, 0x3A, 0x32, 0x32, 0x00};

FakeOtRadioDevice::LowpanSpinelDeviceFidlImpl::LowpanSpinelDeviceFidlImpl(
    FakeOtRadioDevice& ot_radio)
    : ot_radio_obj_(ot_radio) {}

void FakeOtRadioDevice::LowpanSpinelDeviceFidlImpl::Bind(async_dispatcher_t* dispatcher,
                                                         zx::channel channel) {
  ot_radio_obj_.fidl_channel_ = zx::unowned_channel(channel);
  fidl::OnUnboundFn<LowpanSpinelDeviceFidlImpl> on_unbound =
      [](LowpanSpinelDeviceFidlImpl* server, fidl::UnboundReason, zx::channel channel) {
        server->ot_radio_obj_.fidl_channel_ = zx::unowned_channel(ZX_HANDLE_INVALID);
        server->ot_radio_obj_.fidl_impl_obj_.release();
      };
  fidl::AsyncBind(dispatcher, std::move(channel), this, std::move(on_unbound));
}

void FakeOtRadioDevice::LowpanSpinelDeviceFidlImpl::Open(OpenCompleter::Sync completer) {
  zx_status_t res = ot_radio_obj_.Reset();
  if (res == ZX_OK) {
    zxlogf(TRACE, "open succeed, returning\n");
    ot_radio_obj_.power_status_ = OT_SPINEL_DEVICE_ON;
    lowpan_spinel_fidl::Device::SendOnReadyForSendFramesEvent(ot_radio_obj_.fidl_channel_->borrow(),
                                                              kOutboundAllowanceInit);
    ot_radio_obj_.inbound_allowance_ = 0;
    ot_radio_obj_.outbound_allowance_ = kOutboundAllowanceInit;
    ot_radio_obj_.inbound_cnt_ = 0;
    ot_radio_obj_.outbound_cnt_ = 0;
    completer.ReplySuccess();
  } else {
    zxlogf(ERROR, "Error in handling FIDL close req: %s, power status: %u\n",
           zx_status_get_string(res), ot_radio_obj_.power_status_);
    completer.ReplyError(lowpan_spinel_fidl::Error::UNSPECIFIED);
  }
}

void FakeOtRadioDevice::LowpanSpinelDeviceFidlImpl::Close(CloseCompleter::Sync completer) {
  zx_status_t res = ot_radio_obj_.Reset();
  if (res == ZX_OK) {
    ot_radio_obj_.power_status_ = OT_SPINEL_DEVICE_OFF;
    completer.ReplySuccess();
  } else {
    zxlogf(ERROR, "Error in handling FIDL close req: %s, power status: %u\n",
           zx_status_get_string(res), ot_radio_obj_.power_status_);
    completer.ReplyError(lowpan_spinel_fidl::Error::UNSPECIFIED);
  }
}

void FakeOtRadioDevice::LowpanSpinelDeviceFidlImpl::GetMaxFrameSize(
    GetMaxFrameSizeCompleter::Sync completer) {
  completer.Reply(kMaxFrameSize);
}

void FakeOtRadioDevice::LowpanSpinelDeviceFidlImpl::SendFrame(::fidl::VectorView<uint8_t> data,
                                                              SendFrameCompleter::Sync completer) {
  if (ot_radio_obj_.power_status_ == OT_SPINEL_DEVICE_OFF) {
    lowpan_spinel_fidl::Device::SendOnErrorEvent(ot_radio_obj_.fidl_channel_->borrow(),
                                                 lowpan_spinel_fidl::Error::CLOSED, false);
  } else if (data.count() > kMaxFrameSize) {
    lowpan_spinel_fidl::Device::SendOnErrorEvent(
        ot_radio_obj_.fidl_channel_->borrow(), lowpan_spinel_fidl::Error::OUTBOUND_FRAME_TOO_LARGE,
        false);
  } else if (ot_radio_obj_.outbound_allowance_ == 0) {
    // Client violates the protocol, close FIDL channel and device. Will not send OnError event.
    ot_radio_obj_.fidl_channel_ = zx::unowned_channel(ZX_HANDLE_INVALID);
    ot_radio_obj_.power_status_ = OT_SPINEL_DEVICE_OFF;
    ot_radio_obj_.Reset();
    ot_radio_obj_.fidl_impl_obj_.release();
    completer.Close(ZX_ERR_IO_OVERRUN);
  } else {
    // Send out the frame.
    fbl::AutoLock lock(&ot_radio_obj_.outbound_lock_);
    ot_radio_obj_.outbound_queue_.push(std::move(data));
    lock.release();
    async::PostTask(ot_radio_obj_.loop_.dispatcher(),
                    [this]() { this->ot_radio_obj_.TryHandleOutboundFrame(); });

    ot_radio_obj_.outbound_allowance_--;
    ot_radio_obj_.outbound_cnt_++;

    if ((ot_radio_obj_.outbound_cnt_ & 1) == 0) {
      lowpan_spinel_fidl::Device::SendOnReadyForSendFramesEvent(
          ot_radio_obj_.fidl_channel_->borrow(), kOutboundAllowanceInc);
      ot_radio_obj_.outbound_allowance_ += kOutboundAllowanceInc;
    }
  }
}

void FakeOtRadioDevice::LowpanSpinelDeviceFidlImpl::ReadyToReceiveFrames(
    uint32_t number_of_frames, ReadyToReceiveFramesCompleter::Sync completer) {
  zxlogf(TRACE, "allow to receive %u frame\n", number_of_frames);
  bool prev_no_inbound_allowance = (ot_radio_obj_.inbound_allowance_ == 0) ? true : false;
  ot_radio_obj_.inbound_allowance_ += number_of_frames;

  if (prev_no_inbound_allowance && ot_radio_obj_.inbound_allowance_ > 0) {
    zx_port_packet packet = {PORT_KEY_INBOUND_ALLOWANCE, ZX_PKT_TYPE_USER, ZX_OK, {}};
    ot_radio_obj_.port_.queue(&packet);
  }
}

FakeOtRadioDevice::FakeOtRadioDevice(zx_device_t* device)
    : ddk::Device<FakeOtRadioDevice, ddk::UnbindableNew, ddk::Messageable>(device),
      loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {}

zx_status_t FakeOtRadioDevice::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
  DdkTransaction transaction(txn);
  lowpan_spinel_fidl::DeviceSetup::Dispatch(this, msg, &transaction);
  return transaction.Status();
}

void FakeOtRadioDevice::SetChannel(zx::channel channel, SetChannelCompleter::Sync completer) {
  if (fidl_impl_obj_ != nullptr) {
    zxlogf(ERROR, "ot-audio: channel already set\n");
    completer.ReplyError(ZX_ERR_ALREADY_BOUND);
    return;
  }
  if (!channel.is_valid()) {
    completer.ReplyError(ZX_ERR_BAD_HANDLE);
    return;
  }
  fidl_impl_obj_ = std::make_unique<LowpanSpinelDeviceFidlImpl>(*this);
  fidl_impl_obj_->Bind(loop_.dispatcher(), std::move(channel));
  completer.ReplySuccess();
}

zx_status_t FakeOtRadioDevice::StartLoopThread() {
  zxlogf(TRACE, "Start loop thread\n");
  return loop_.StartThread("ot-stack-loop");
}

zx_status_t FakeOtRadioDevice::Reset() {
  zx_status_t status = ZX_OK;
  zxlogf(TRACE, "fake-ot-radio: reset\n");

  fbl::AutoLock lock_in(&inbound_lock_);
  std::queue<std::vector<uint8_t>> empty_inbound_queue;
  std::swap(inbound_queue_, empty_inbound_queue);
  lock_in.release();

  fbl::AutoLock lock_out(&outbound_lock_);
  std::queue<::fidl::VectorView<uint8_t>> empty_outbound_queue;
  std::swap(outbound_queue_, empty_outbound_queue);
  lock_out.release();

  zx::nanosleep(zx::deadline_after(zx::msec(500)));

  std::vector<uint8_t> event;
  event.assign(std::begin(kNcpResetEvent), std::end(kNcpResetEvent));
  PostSendInboundFrameTask(std::move(event));

  return status;
}

void FakeOtRadioDevice::TryHandleOutboundFrame() {
  fbl::AutoLock lock(&outbound_lock_);
  if (outbound_queue_.size() > 0) {
    zxlogf(TRACE, "fake-ot-stack: TryHandleOutboundFrame() outbound_queue_.size():%lu\n",
           outbound_queue_.size());
    FrameHandler(outbound_queue_.front());
    outbound_queue_.pop();
  }
}

void FakeOtRadioDevice::FrameHandler(::fidl::VectorView<uint8_t> data) {
  if (power_status_ != OT_SPINEL_DEVICE_ON) {
    zxlogf(ERROR, "fake-ot-radio: failed to handle frame due to device off\n");
    return;
  }
  if (memcmp(data.cbegin(), kNcpVerRequest, sizeof(kNcpVerRequest)) == 0) {
    std::vector<uint8_t> reply;
    reply.assign(std::begin(kNcpVerReply), std::end(kNcpVerReply));
    PostSendInboundFrameTask(std::move(reply));
  } else {
    // TODO (jiamingw): Send back response for invalid request.
  }
}

uint32_t FakeOtRadioDevice::GetTimeoutMs() {
  int timeout_ms = kLoopTimeOutMsOneDay;

  fbl::AutoLock lock(&inbound_lock_);
  if (inbound_queue_.size() > 0 && inbound_allowance_ > 0) {
    timeout_ms = 0;
  }

  return timeout_ms;
}

void FakeOtRadioDevice::PostSendInboundFrameTask(std::vector<uint8_t> frame) {
  fbl::AutoLock lock(&inbound_lock_);
  inbound_queue_.push(std::move(frame));
  zx_port_packet packet = {PORT_KEY_INBOUND_FRAME, ZX_PKT_TYPE_USER, ZX_OK, {}};
  port_.queue(&packet);
}

zx_status_t FakeOtRadioDevice::TrySendInboundFrame() {
  fbl::AutoLock lock(&inbound_lock_);
  if (power_status_ == OT_SPINEL_DEVICE_ON && inbound_allowance_ > 0 && inbound_queue_.size() > 0) {
    // send out 1 packet
    auto spinel_frame = inbound_queue_.front();
    ::fidl::VectorView<uint8_t> data;
    data.set_count(spinel_frame.size());
    data.set_data(spinel_frame.data());
    zx_status_t res = lowpan_spinel_fidl::Device::SendOnReceiveFrameEvent(fidl_channel_->borrow(),
                                                                          std::move(data));
    if (res != ZX_OK) {
      zxlogf(ERROR, "fake-ot-radio: failed to send OnReceive() event due to %s\n",
             zx_status_get_string(res));
      return res;
    }

    inbound_allowance_--;
    inbound_cnt_++;
    inbound_queue_.pop();
  }
  return ZX_OK;
}

zx_status_t FakeOtRadioDevice::RadioThread() {
  zx_status_t status = ZX_OK;
  zxlogf(ERROR, "fake-ot-radio: entered thread\n");

  while (true) {
    zx_port_packet_t packet = {};
    int timeout_ms = GetTimeoutMs();
    status = port_.wait(zx::deadline_after(zx::msec(timeout_ms)), &packet);

    if (status == ZX_ERR_TIMED_OUT) {
      zx_status_t send_status = TrySendInboundFrame();
      if (send_status != ZX_OK) {
        power_status_ = OT_SPINEL_DEVICE_OFF;
      }
      continue;
    } else if (status != ZX_OK) {
      zxlogf(ERROR, "fake-ot-radio: port wait failed: %d\n", status);
      return thrd_error;
    }

    if (packet.key == PORT_KEY_INBOUND_FRAME || packet.key == PORT_KEY_INBOUND_ALLOWANCE) {
      zx_status_t send_status = TrySendInboundFrame();
      if (send_status != ZX_OK) {
        power_status_ = OT_SPINEL_DEVICE_OFF;
      }
    } else if (packet.key == PORT_KEY_EXIT_THREAD) {
      break;
    }
  }
  zxlogf(TRACE, "fake-ot-radio: exiting\n");

  return status;
}

zx_status_t FakeOtRadioDevice::CreateBindAndStart(void* ctx, zx_device_t* parent) {
  std::unique_ptr<FakeOtRadioDevice> ot_radio_dev;
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

zx_status_t FakeOtRadioDevice::Create(void* ctx, zx_device_t* parent,
                                      std::unique_ptr<FakeOtRadioDevice>* out) {
  auto dev = std::make_unique<FakeOtRadioDevice>(parent);

  *out = std::move(dev);

  return ZX_OK;
}

zx_status_t FakeOtRadioDevice::Bind() {
  zx_status_t status = DdkAdd("fake-ot-radio", 0, nullptr, 0, ZX_PROTOCOL_OT_RADIO);
  if (status != ZX_OK) {
    zxlogf(ERROR, "fake-ot-radio: Could not create device: %d\n", status);
    return status;
  } else {
    zxlogf(TRACE, "fake-ot-radio: Added device\n");
  }
  return status;
}

zx_status_t FakeOtRadioDevice::Start() {
  zx_status_t status = zx::port::create(0, &port_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "fake-ot-radio: port create failed %d\n", status);
    return status;
  }

  auto cleanup = fbl::MakeAutoCall([&]() { ShutDown(); });

  auto callback = [](void* cookie) {
    return reinterpret_cast<FakeOtRadioDevice*>(cookie)->RadioThread();
  };
  event_loop_thread_ = std::thread(callback, this);

  status = StartLoopThread();
  if (status != ZX_OK) {
    zxlogf(ERROR, "fake-ot-radio: Could not start loop thread\n");
    return status;
  }

  zxlogf(TRACE, "fake-ot-radio: Started thread\n");

  cleanup.cancel();

  return status;
}

void FakeOtRadioDevice::DdkRelease() { delete this; }

void FakeOtRadioDevice::DdkUnbindNew(ddk::UnbindTxn txn) {
  ShutDown();
  txn.Reply();
}

zx_status_t FakeOtRadioDevice::ShutDown() {
  zx_port_packet packet = {PORT_KEY_EXIT_THREAD, ZX_PKT_TYPE_USER, ZX_OK, {}};
  port_.queue(&packet);
  event_loop_thread_.join();
  loop_.Shutdown();
  return ZX_OK;
}

static constexpr zx_driver_ops_t device_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = FakeOtRadioDevice::CreateBindAndStart;
  return ops;
}();

}  // namespace fake_ot

// clang-format off
ZIRCON_DRIVER_BEGIN(fake_ot, fake_ot::device_ops, "zircon", "0.1", 3)
  BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_TEST),
  BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_OT_TEST),
  BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_TEST_OT_RADIO),
ZIRCON_DRIVER_END(fake_ot)
