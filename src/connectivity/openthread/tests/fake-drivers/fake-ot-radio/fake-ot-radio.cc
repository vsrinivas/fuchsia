// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fake-ot-radio.h"

#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/driver.h>
#include <lib/fit/defer.h>
#include <stdio.h>
#include <sys/types.h>
#include <zircon/compiler.h>
#include <zircon/status.h>

#include <ddktl/fidl.h>
#include <fbl/auto_lock.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>

#include "src/connectivity/openthread/tests/fake-drivers/fake-ot-radio/fake_ot_radio_bind.h"

namespace fake_ot {
namespace lowpan_spinel_fidl = fuchsia_lowpan_spinel;

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
constexpr uint8_t kPhyChan = 0x22;
constexpr uint8_t kRegionCode = 0x2c;
constexpr uint8_t kPropGetRadioCap[] = {0x8b, 0x24};
constexpr uint8_t kPropGetB001[] = {0xb0, 0x1};
constexpr uint8_t kPropGet8210[] = {0x82, 0x10};

constexpr uint8_t kNcpVerReply[] = {
    0x80, 0x06, 0x02, 0x4f, 0x50, 0x45, 0x4e, 0x54, 0x48, 0x52, 0x45, 0x41, 0x44, 0x2f, 0x67, 0x66,
    0x36, 0x30, 0x38, 0x62, 0x31, 0x64, 0x3b, 0x20, 0x52, 0x43, 0x50, 0x2d, 0x4e, 0x65, 0x77, 0x6d,
    0x61, 0x6e, 0x2d, 0x75, 0x73, 0x65, 0x72, 0x3b, 0x20, 0x4a, 0x75, 0x6e, 0x20, 0x20, 0x39, 0x20,
    0x32, 0x30, 0x32, 0x31, 0x20, 0x30, 0x32, 0x3a, 0x32, 0x38, 0x3a, 0x30, 0x32, 0x00};
constexpr uint8_t kProtocolVerReply[] = {0x80, 0x6, 0x1, 0x4, 0x3};
constexpr uint8_t kPropCapsReply[] = {0x80, 0x6,  0x5, 0x5,  0xc,  0x18,
                                      0x22, 0x81, 0x4, 0x40, 0x86, 0x4};
constexpr uint8_t kPropHwAddrReply[] = {0x80, 0x6,  0x8,  0x18, 0xb4, 0x30,
                                        0x5,  0xf8, 0xdf, 0x51, 0x39};
constexpr uint8_t kPhyRxSensitivityReply[] = {0x80, 0x6, 0x27, 0x9c};
constexpr uint8_t kPhyChanReply[] = {0x80, 0x6,  0x22, 0xb,  0xc,  0xd,  0xe,  0xf,  0x10, 0x11,
                                     0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a};
constexpr uint8_t kRegionCodeReply[] = {0x80, 0x6, 0x2c, 0x55, 0x53};
constexpr uint8_t kPropGetRadioCapReply[] = {0x80, 0x6, 0x8b, 0x24, 0x6d};
constexpr uint8_t kPropGetB001Reply[] = {0x80, 0x6, 0xb0, 0x1, 0x3};
constexpr uint8_t kPropGet8210Reply[] = {0x80, 0x6, 0x82, 0x10, 0xdc, 0xc7,
                                         0x1,  0x0, 0x0,  0x0,  0x0,  0x0};
constexpr uint8_t kSpinelFrameHeader = 0x80;
constexpr uint8_t kSpinelHeaderInvalid = 0xFF;

FakeOtRadioDevice::LowpanSpinelDeviceFidlImpl::LowpanSpinelDeviceFidlImpl(
    FakeOtRadioDevice& ot_radio)
    : ot_radio_obj_(ot_radio) {}

void FakeOtRadioDevice::LowpanSpinelDeviceFidlImpl::Bind(
    async_dispatcher_t* dispatcher, fidl::ServerEnd<fuchsia_lowpan_spinel::Device> channel) {
  fidl::OnUnboundFn<LowpanSpinelDeviceFidlImpl> on_unbound =
      [](LowpanSpinelDeviceFidlImpl* server, fidl::UnbindInfo /*unused*/,
         fidl::ServerEnd<fuchsia_lowpan_spinel::Device> /*unused*/) {
        server->ot_radio_obj_.fidl_impl_obj_.reset();
      };
  ot_radio_obj_.fidl_binding_ =
      fidl::BindServer(dispatcher, std::move(channel), this, std::move(on_unbound));
}

void FakeOtRadioDevice::LowpanSpinelDeviceFidlImpl::Open(OpenRequestView request,
                                                         OpenCompleter::Sync& completer) {
  zx_status_t res = ot_radio_obj_.Reset();
  if (res == ZX_OK) {
    zxlogf(DEBUG, "open succeed, returning");
    ot_radio_obj_.power_status_ = OT_SPINEL_DEVICE_ON;
    (*ot_radio_obj_.fidl_binding_)->OnReadyForSendFrames(kRadioboundAllowanceInit);
    ot_radio_obj_.clientbound_allowance_ = 0;
    ot_radio_obj_.radiobound_allowance_ = kRadioboundAllowanceInit;
    ot_radio_obj_.clientbound_cnt_ = 0;
    ot_radio_obj_.radiobound_cnt_ = 0;
    completer.ReplySuccess();
  } else {
    zxlogf(ERROR, "Error in handling FIDL close req: %s, power status: %u",
           zx_status_get_string(res), ot_radio_obj_.power_status_);
    completer.ReplyError(lowpan_spinel_fidl::wire::Error::kUnspecified);
  }
}

void FakeOtRadioDevice::LowpanSpinelDeviceFidlImpl::Close(CloseRequestView request,
                                                          CloseCompleter::Sync& completer) {
  zx_status_t res = ot_radio_obj_.Reset();
  if (res == ZX_OK) {
    ot_radio_obj_.power_status_ = OT_SPINEL_DEVICE_OFF;
    completer.ReplySuccess();
  } else {
    zxlogf(ERROR, "Error in handling FIDL close req: %s, power status: %u",
           zx_status_get_string(res), ot_radio_obj_.power_status_);
    completer.ReplyError(lowpan_spinel_fidl::wire::Error::kUnspecified);
  }
}

void FakeOtRadioDevice::LowpanSpinelDeviceFidlImpl::GetMaxFrameSize(
    GetMaxFrameSizeRequestView request, GetMaxFrameSizeCompleter::Sync& completer) {
  completer.Reply(kMaxFrameSize);
}

void FakeOtRadioDevice::LowpanSpinelDeviceFidlImpl::SendFrame(SendFrameRequestView request,
                                                              SendFrameCompleter::Sync& completer) {
  if (ot_radio_obj_.power_status_ == OT_SPINEL_DEVICE_OFF) {
    (*ot_radio_obj_.fidl_binding_)->OnError(lowpan_spinel_fidl::wire::Error::kClosed, false);
  } else if (request->data.count() > kMaxFrameSize) {
    (*ot_radio_obj_.fidl_binding_)
        ->OnError(lowpan_spinel_fidl::wire::Error::kOutboundFrameTooLarge, false);
  } else if (ot_radio_obj_.radiobound_allowance_ == 0) {
    // Client violates the protocol, close FIDL channel and device. Will not send OnError event.
    ot_radio_obj_.power_status_ = OT_SPINEL_DEVICE_OFF;
    ot_radio_obj_.Reset();
    ot_radio_obj_.fidl_binding_->Close(ZX_ERR_IO_OVERRUN);
    completer.Close(ZX_ERR_IO_OVERRUN);
  } else {
    // Send out the frame.
    fbl::AutoLock lock(&ot_radio_obj_.radiobound_lock_);
    std::vector<uint8_t> radiobound_bytes;
    radiobound_bytes.assign(request->data.data(), request->data.data() + request->data.count());
    ot_radio_obj_.radiobound_queue_.push(std::move(radiobound_bytes));
    lock.release();
    async::PostTask(ot_radio_obj_.loop_.dispatcher(),
                    [this]() { this->ot_radio_obj_.TryHandleRadioboundFrame(); });

    ot_radio_obj_.radiobound_allowance_--;
    ot_radio_obj_.radiobound_cnt_++;

    if ((ot_radio_obj_.radiobound_cnt_ & 1) == 0) {
      (*ot_radio_obj_.fidl_binding_)->OnReadyForSendFrames(kRadioboundAllowanceInc);
      ot_radio_obj_.radiobound_allowance_ += kRadioboundAllowanceInc;
    }
  }
}

void FakeOtRadioDevice::LowpanSpinelDeviceFidlImpl::ReadyToReceiveFrames(
    ReadyToReceiveFramesRequestView request, ReadyToReceiveFramesCompleter::Sync& completer) {
  zxlogf(DEBUG, "allow to receive %u frame", request->number_of_frames);
  bool prev_no_clientbound_allowance = ot_radio_obj_.clientbound_allowance_ == 0;
  ot_radio_obj_.clientbound_allowance_ += request->number_of_frames;

  if (prev_no_clientbound_allowance && ot_radio_obj_.clientbound_allowance_ > 0) {
    zx_port_packet packet = {PORT_KEY_INBOUND_ALLOWANCE, ZX_PKT_TYPE_USER, ZX_OK, {}};
    ot_radio_obj_.port_.queue(&packet);
  }
}

FakeOtRadioDevice::FakeOtRadioDevice(zx_device_t* device)
    : DeviceType(device), loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {}

void FakeOtRadioDevice::SetChannel(SetChannelRequestView request,
                                   SetChannelCompleter::Sync& completer) {
  if (fidl_impl_obj_ != nullptr) {
    zxlogf(ERROR, "ot-audio: channel already set");
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

zx_status_t FakeOtRadioDevice::StartLoopThread() {
  zxlogf(DEBUG, "Start loop thread");
  return loop_.StartThread("ot-stack-loop");
}

zx_status_t FakeOtRadioDevice::Reset() {
  zxlogf(INFO, "fake-ot-radio: reset");

  fbl::AutoLock lock_in(&clientbound_lock_);
  std::queue<std::vector<uint8_t>> empty_clientbound_queue;
  std::swap(clientbound_queue_, empty_clientbound_queue);
  lock_in.release();

  zx::nanosleep(zx::deadline_after(zx::msec(kResetMsDelay)));

  std::vector<uint8_t> event;
  event.assign(std::begin(kNcpResetEvent), std::end(kNcpResetEvent));
  PostSendClientboundFrameTask(std::move(event));

  return ZX_OK;
}

void FakeOtRadioDevice::TryHandleRadioboundFrame() {
  fbl::AutoLock lock(&radiobound_lock_);
  if (!radiobound_queue_.empty()) {
    zxlogf(DEBUG, "fake-ot-stack: TryHandleRadioboundFrame() radiobound_queue_.size():%lu",
           radiobound_queue_.size());
    FrameHandler(std::move(radiobound_queue_.front()));
    radiobound_queue_.pop();
  }
}

uint8_t FakeOtRadioDevice::ValidateSpinelHeaderAndGetTid(const uint8_t* data, uint32_t len) {
  if ((len == 0) || ((data[0] & kBitMaskHigherFourBits) != kSpinelFrameHeader)) {
    return false;
  }

  return data[0] & kBitMaskLowerFourBits;
}

void FakeOtRadioDevice::FrameHandler(std::vector<uint8_t> data) {
  if (power_status_ != OT_SPINEL_DEVICE_ON) {
    zxlogf(ERROR, "fake-ot-radio: failed to handle frame due to device off");
    return;
  }

  uint8_t tid = ValidateSpinelHeaderAndGetTid(data.data(), data.size());
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
      case kPhyChan:
        reply.assign(std::begin(kPhyChanReply), std::end(kPhyChanReply));
        break;
      case kRegionCode:
        reply.assign(std::begin(kRegionCodeReply), std::end(kRegionCodeReply));
        break;
      default:
        if (memcmp(kPropGetRadioCap, &data.data()[kCmdLoc + 1], sizeof(kPropGetRadioCap)) == 0) {
          reply.assign(std::begin(kPropGetRadioCapReply), std::end(kPropGetRadioCapReply));
        } else if (memcmp(kPropGetB001, &data.data()[kCmdLoc + 1], sizeof(kPropGetB001)) == 0) {
          reply.assign(std::begin(kPropGetB001Reply), std::end(kPropGetB001Reply));
        } else if (memcmp(kPropGet8210, &data.data()[kCmdLoc + 1], sizeof(kPropGet8210)) == 0) {
          reply.assign(std::begin(kPropGet8210Reply), std::end(kPropGet8210Reply));
        } else {
          zxlogf(ERROR, "fake-ot-radio: not supported prop value get cmd");
        }
        break;
    }
    reply[0] |= tid;
    PostSendClientboundFrameTask(std::move(reply));
  } else if (data.data()[kCmdLoc] == kPropValueSet) {
    // Handle prop value set
    // now we just reply what is being set
    // TODO (jiamingw): make rcp stateful
    std::vector<uint8_t> reply;
    reply.assign(data.cbegin(), data.cend());
    reply[1] = kPropValueIs;
    PostSendClientboundFrameTask(std::move(reply));
  } else {
    // TODO (jiamingw): Send back response for invalid request.
    zxlogf(ERROR, "fake-ot-radio: received invalid spinel frame");
  }
}

uint32_t FakeOtRadioDevice::GetTimeoutMs() {
  int timeout_ms = kLoopTimeOutMsOneDay;

  fbl::AutoLock lock(&clientbound_lock_);
  if (!clientbound_queue_.empty() && clientbound_allowance_ > 0) {
    timeout_ms = 0;
  }

  return timeout_ms;
}

void FakeOtRadioDevice::PostSendClientboundFrameTask(std::vector<uint8_t> frame) {
  fbl::AutoLock lock(&clientbound_lock_);
  clientbound_queue_.push(std::move(frame));
  zx_port_packet packet = {PORT_KEY_INBOUND_FRAME, ZX_PKT_TYPE_USER, ZX_OK, {}};
  port_.queue(&packet);
}

zx_status_t FakeOtRadioDevice::TrySendClientboundFrame() {
  fbl::AutoLock lock(&clientbound_lock_);
  if (power_status_ == OT_SPINEL_DEVICE_ON && clientbound_allowance_ > 0 &&
      !clientbound_queue_.empty()) {
    // send out 1 packet
    auto data = fidl::VectorView<uint8_t>::FromExternal(clientbound_queue_.front());
    zx_status_t res = (*fidl_binding_)->OnReceiveFrame(std::move(data));
    if (res != ZX_OK) {
      zxlogf(ERROR, "fake-ot-radio: failed to send OnReceive() event due to %s",
             zx_status_get_string(res));
      return res;
    }

    clientbound_allowance_--;
    clientbound_cnt_++;
    clientbound_queue_.pop();
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
      zx_status_t send_status = TrySendClientboundFrame();
      if (send_status != ZX_OK) {
        power_status_ = OT_SPINEL_DEVICE_OFF;
      }
      continue;
    }
    if (status != ZX_OK) {
      zxlogf(ERROR, "fake-ot-radio: port wait failed: %d", status);
      return status;
    }

    if (packet.key == PORT_KEY_INBOUND_FRAME || packet.key == PORT_KEY_INBOUND_ALLOWANCE) {
      zx_status_t send_status = TrySendClientboundFrame();
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

  return dev_ptr->Start();
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
  }
  zxlogf(DEBUG, "fake-ot-radio: Added device");
  return ZX_OK;
}

zx_status_t FakeOtRadioDevice::Start() {
  zx_status_t status = zx::port::create(0, &port_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "fake-ot-radio: port create failed %d", status);
    return status;
  }

  auto cleanup = fit::defer([&]() { ShutDown(); });

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

  return ZX_OK;
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

ZIRCON_DRIVER(fake_ot, fake_ot::device_ops, "zircon", "0.1");
