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
};

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
    // TODO (jiamingw): All good, send out the frame.
  }
}

void FakeOtRadioDevice::LowpanSpinelDeviceFidlImpl::ReadyToReceiveFrames(
    uint32_t number_of_frames, ReadyToReceiveFramesCompleter::Sync completer) {
  zxlogf(TRACE, "allow to receive %u frame\n", number_of_frames);
  bool prev_no_inbound_allowance = (ot_radio_obj_.inbound_allowance_ == 0) ? true : false;
  ot_radio_obj_.inbound_allowance_ += number_of_frames;

  if (prev_no_inbound_allowance && ot_radio_obj_.inbound_allowance_ > 0) {
    // TODO (jiamingw): inform allowance available
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
  // TODO (jiamingw): Suspend spinel frame handler, clear rx queue
  zx::nanosleep(zx::deadline_after(zx::msec(500)));
  return status;
}

uint32_t FakeOtRadioDevice::GetTimeoutMs(void) {
  int timeout_ms = kLoopTimeOutMsOneDay;

  return timeout_ms;
}

zx_status_t FakeOtRadioDevice::RadioThread(void) {
  zx_status_t status = ZX_OK;
  zxlogf(ERROR, "fake-ot-radio: entered thread\n");

  while (true) {
    zx_port_packet_t packet = {};
    int timeout_ms = GetTimeoutMs();
    auto status = port_.wait(zx::deadline_after(zx::msec(timeout_ms)), &packet);

    if (status == ZX_ERR_TIMED_OUT) {
      // TODO (jiamingw): Check whether an inbound packet is available
      continue;
    } else if (status != ZX_OK) {
      zxlogf(ERROR, "fake-ot-radio: port wait failed: %d\n", status);
      return thrd_error;
    }

    if (packet.key == PORT_KEY_EXIT_THREAD) {
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

zx_status_t FakeOtRadioDevice::Bind(void) {
  zx_status_t status = DdkAdd("fake-ot-radio", 0, nullptr, 0, ZX_PROTOCOL_OT_RADIO);
  if (status != ZX_OK) {
    zxlogf(ERROR, "fake-ot-radio: Could not create device: %d\n", status);
    return status;
  } else {
    zxlogf(TRACE, "fake-ot-radio: Added device\n");
  }
  return status;
}

zx_status_t FakeOtRadioDevice::Start(void) {
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
