// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fake-ot-radio.h"

#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
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
constexpr uint8_t kCmdLoc = 0x01;
constexpr uint8_t kNcpSoftResetRequest = 0x01;
constexpr uint8_t kPropValueGet = 0x02;
constexpr uint8_t kPropValueSet = 0x03;
constexpr uint8_t kPropValueIs = 0x06;
constexpr uint8_t kNcpVer = 0x02;
constexpr uint8_t kProtocolVer = 0x01;
constexpr uint8_t kPropCaps = 0x5;
constexpr uint8_t kPropHwAddr = 0x8;
constexpr uint8_t kPhyRxSensitivity = 0x27;
constexpr uint8_t kPropGetRadioCap[] = {0x8b, 0x24};

constexpr uint8_t kNcpVerReply[] = {
    0x80, 0x06, 0x02, 0x4F, 0x50, 0x45, 0x4E, 0x54, 0x48, 0x52, 0x45, 0x41, 0x44, 0x2F, 0x31,
    0x2E, 0x30, 0x64, 0x37, 0x32, 0x35, 0x3B, 0x20, 0x52, 0x43, 0x50, 0x2D, 0x4E, 0x65, 0x77,
    0x6D, 0x61, 0x6E, 0x31, 0x3B, 0x20, 0x46, 0x65, 0x62, 0x20, 0x32, 0x34, 0x20, 0x32, 0x30,
    0x31, 0x39, 0x20, 0x31, 0x33, 0x3A, 0x33, 0x38, 0x3A, 0x32, 0x32, 0x00};
constexpr uint8_t kProtocolVerReply[] = {0x80, 0x6, 0x1, 0x4, 0x3};
constexpr uint8_t kPropCapsReply[] = {0x80, 0x6, 0x5, 0x5, 0xc, 0xd, 0x18, 0x22, 0x81, 0x4};
constexpr uint8_t kPropHwAddrReply[] = {0x80, 0x6,  0x8,  0x64, 0x16, 0x66,
                                        0x0,  0x47, 0x34, 0xaf, 0x1a};
constexpr uint8_t kPhyRxSensitivityReply[] = {0x80, 0x6, 0x27, 0x9c};
constexpr uint8_t kPropGetRadioCapReply[] = {0x80, 0x6, 0x8b, 0x24, 0xd};

constexpr uint8_t kSpinelFrameHeader = 0x80;
constexpr uint8_t kSpinelHeaderInvalid = 0xFF;

FakeOtRadioDevice::LowpanSpinelDeviceFidlImpl::LowpanSpinelDeviceFidlImpl(
    FakeOtRadioDevice& ot_radio)
    : ot_radio_obj_(ot_radio) {}

zx_status_t FakeOtRadioDevice::LowpanSpinelDeviceFidlImpl::Bind(async_dispatcher_t* dispatcher,
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

void FakeOtRadioDevice::LowpanSpinelDeviceFidlImpl::Open(OpenCompleter::Sync& completer) {
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

void FakeOtRadioDevice::LowpanSpinelDeviceFidlImpl::Close(CloseCompleter::Sync& completer) {
  zx_status_t res = ot_radio_obj_.Reset();
  if (res == ZX_OK) {
    ot_radio_obj_.power_status_ = OT_SPINEL_DEVICE_OFF;
    completer.ReplySuccess();
  } else {
    zxlogf(ERROR, "Error in handling FIDL close req: %s, power status: %u",
           zx_status_get_string(res), ot_radio_obj_.power_status_);
    completer.ReplyError(lowpan_spinel_fidl::Error::UNSPECIFIED);
  }
}

void FakeOtRadioDevice::LowpanSpinelDeviceFidlImpl::GetMaxFrameSize(
    GetMaxFrameSizeCompleter::Sync& completer) {
  completer.Reply(kMaxFrameSize);
}

void FakeOtRadioDevice::LowpanSpinelDeviceFidlImpl::SendFrame(::fidl::VectorView<uint8_t> data,
                                                              SendFrameCompleter::Sync& completer) {
  if (ot_radio_obj_.power_status_ == OT_SPINEL_DEVICE_OFF) {
    (*ot_radio_obj_.fidl_binding_)->OnError(lowpan_spinel_fidl::Error::CLOSED, false);
  } else if (data.count() > kMaxFrameSize) {
    (*ot_radio_obj_.fidl_binding_)
        ->OnError(lowpan_spinel_fidl::Error::OUTBOUND_FRAME_TOO_LARGE, false);
  } else if (ot_radio_obj_.outbound_allowance_ == 0) {
    // Client violates the protocol, close FIDL channel and device. Will not send OnError event.
    ot_radio_obj_.power_status_ = OT_SPINEL_DEVICE_OFF;
    ot_radio_obj_.Reset();
    ot_radio_obj_.fidl_binding_->Close(ZX_ERR_IO_OVERRUN);
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
      (*ot_radio_obj_.fidl_binding_)->OnReadyForSendFrames(kOutboundAllowanceInc);
      ot_radio_obj_.outbound_allowance_ += kOutboundAllowanceInc;
    }
  }
}

void FakeOtRadioDevice::LowpanSpinelDeviceFidlImpl::ReadyToReceiveFrames(
    uint32_t number_of_frames, ReadyToReceiveFramesCompleter::Sync& completer) {
  zxlogf(DEBUG, "allow to receive %u frame", number_of_frames);
  bool prev_no_inbound_allowance = (ot_radio_obj_.inbound_allowance_ == 0) ? true : false;
  ot_radio_obj_.inbound_allowance_ += number_of_frames;

  if (prev_no_inbound_allowance && ot_radio_obj_.inbound_allowance_ > 0) {
    zx_port_packet packet = {PORT_KEY_INBOUND_ALLOWANCE, ZX_PKT_TYPE_USER, ZX_OK, {}};
    ot_radio_obj_.port_.queue(&packet);
  }
}

FakeOtRadioDevice::FakeOtRadioDevice(zx_device_t* device)
    : ddk::Device<FakeOtRadioDevice, ddk::Unbindable, ddk::Messageable>(device),
      loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {}

zx_status_t FakeOtRadioDevice::DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn) {
  DdkTransaction transaction(txn);
  lowpan_spinel_fidl::DeviceSetup::Dispatch(this, msg, &transaction);
  return transaction.Status();
}

void FakeOtRadioDevice::SetChannel(zx::channel channel, SetChannelCompleter::Sync& completer) {
  if (fidl_impl_obj_ != nullptr) {
    zxlogf(ERROR, "ot-audio: channel already set");
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

zx_status_t FakeOtRadioDevice::StartLoopThread() {
  zxlogf(DEBUG, "Start loop thread");
  return loop_.StartThread("ot-stack-loop");
}

zx_status_t FakeOtRadioDevice::Reset() {
  zx_status_t status = ZX_OK;
  zxlogf(INFO, "fake-ot-radio: reset");

  fbl::AutoLock lock_in(&inbound_lock_);
  std::queue<std::vector<uint8_t>> empty_inbound_queue;
  std::swap(inbound_queue_, empty_inbound_queue);
  lock_in.release();

  fbl::AutoLock lock_out(&outbound_lock_);
  std::queue<::fidl::VectorView<uint8_t>> empty_outbound_queue;
  std::swap(outbound_queue_, empty_outbound_queue);
  lock_out.release();

  zx::nanosleep(zx::deadline_after(zx::msec(kResetMsDelay)));

  std::vector<uint8_t> event;
  event.assign(std::begin(kNcpResetEvent), std::end(kNcpResetEvent));
  PostSendInboundFrameTask(std::move(event));

  return status;
}

void FakeOtRadioDevice::TryHandleOutboundFrame() {
  fbl::AutoLock lock(&outbound_lock_);
  if (outbound_queue_.size() > 0) {
    zxlogf(DEBUG, "fake-ot-stack: TryHandleOutboundFrame() outbound_queue_.size():%lu",
           outbound_queue_.size());
    FrameHandler(std::move(outbound_queue_.front()));
    outbound_queue_.pop();
  }
}

uint8_t FakeOtRadioDevice::ValidateSpinelHeaderAndGetTid(const uint8_t* data, uint32_t len) {
  if ((len == 0) || ((data[0] & kBitMaskHigherFourBits) != kSpinelFrameHeader)) {
    return false;
  }

  return data[0] & kBitMaskLowerFourBits;
}

void FakeOtRadioDevice::FrameHandler(::fidl::VectorView<uint8_t> data) {
  if (power_status_ != OT_SPINEL_DEVICE_ON) {
    zxlogf(ERROR, "fake-ot-radio: failed to handle frame due to device off");
    return;
  }

  uint8_t tid = ValidateSpinelHeaderAndGetTid(data.data(), data.count());
  if (tid == kSpinelHeaderInvalid) {
    return;
  }

  if (data.data()[kCmdLoc] == kNcpSoftResetRequest) {
    async::PostTask(loop_.dispatcher(), [this]() { this->Reset(); });
  } else if (data.data()[kCmdLoc] == kPropValueGet) {
    // Handle prop value get
    std::vector<uint8_t> reply;
    switch (data.data()[kCmdLoc + 1]) {
      case kNcpVer:
        reply.assign(std::begin(kNcpVerReply), std::end(kNcpVerReply));
        break;
      case kProtocolVer:
        reply.assign(std::begin(kProtocolVerReply), std::end(kProtocolVerReply));
        break;
      case kPropCaps:
        reply.assign(std::begin(kPropCapsReply), std::end(kPropCapsReply));
        break;
      case kPropHwAddr:
        reply.assign(std::begin(kPropHwAddrReply), std::end(kPropHwAddrReply));
        break;
      case kPhyRxSensitivity:
        reply.assign(std::begin(kPhyRxSensitivityReply), std::end(kPhyRxSensitivityReply));
        break;
      default:
        if (memcmp(kPropGetRadioCap, &data.data()[kCmdLoc + 1], sizeof(kPropGetRadioCap)) == 0) {
          reply.assign(std::begin(kPropGetRadioCapReply), std::end(kPropGetRadioCapReply));
        } else {
          zxlogf(ERROR, "fake-ot-radio: not supported prop value get cmd");
        }
        break;
    }
    reply.data()[0] |= tid;
    PostSendInboundFrameTask(std::move(reply));
  } else if (data.data()[kCmdLoc] == kPropValueSet) {
    // Handle prop value set
    // now we just reply what is being set
    // TODO (jiamingw): make rcp stateful
    std::vector<uint8_t> reply;
    reply.assign(data.cbegin(), data.cend());
    reply.data()[1] = kPropValueIs;
    PostSendInboundFrameTask(std::move(reply));
  } else {
    // TODO (jiamingw): Send back response for invalid request.
    zxlogf(ERROR, "fake-ot-radio: received invalid spinel frame");
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
    data.set_data(fidl::unowned_ptr(spinel_frame.data()));
    zx_status_t res = (*fidl_binding_)->OnReceiveFrame(std::move(data));
    if (res != ZX_OK) {
      zxlogf(ERROR, "fake-ot-radio: failed to send OnReceive() event due to %s",
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
  zxlogf(INFO, "fake-ot-radio: entered thread");

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
      zxlogf(ERROR, "fake-ot-radio: port wait failed: %d", status);
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
  zxlogf(DEBUG, "fake-ot-radio: exiting");

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
  zx_status_t status =
      DdkAdd(ddk::DeviceAddArgs("fake-ot-radio").set_proto_id(ZX_PROTOCOL_OT_RADIO));
  if (status != ZX_OK) {
    zxlogf(ERROR, "fake-ot-radio: Could not create device: %d", status);
    return status;
  } else {
    zxlogf(DEBUG, "fake-ot-radio: Added device");
  }
  return status;
}

zx_status_t FakeOtRadioDevice::Start() {
  zx_status_t status = zx::port::create(0, &port_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "fake-ot-radio: port create failed %d", status);
    return status;
  }

  auto cleanup = fbl::MakeAutoCall([&]() { ShutDown(); });

  auto callback = [](void* cookie) {
    return reinterpret_cast<FakeOtRadioDevice*>(cookie)->RadioThread();
  };
  event_loop_thread_ = std::thread(callback, this);

  status = StartLoopThread();
  if (status != ZX_OK) {
    zxlogf(ERROR, "fake-ot-radio: Could not start loop thread");
    return status;
  }

  zxlogf(DEBUG, "fake-ot-radio: Started thread");

  cleanup.cancel();

  return status;
}

void FakeOtRadioDevice::DdkRelease() { delete this; }

void FakeOtRadioDevice::DdkUnbind(ddk::UnbindTxn txn) {
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
