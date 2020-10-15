// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_OPENTHREAD_TESTS_FAKE_DRIVERS_FAKE_OT_RADIO_FAKE_OT_RADIO_H_
#define SRC_CONNECTIVITY_OPENTHREAD_TESTS_FAKE_DRIVERS_FAKE_OT_RADIO_FAKE_OT_RADIO_H_

#include <fuchsia/lowpan/spinel/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl/llcpp/server.h>
#include <lib/zx/port.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <array>
#include <atomic>
#include <queue>
#include <thread>

#include <ddk/device.h>
#include <ddktl/device.h>
#include <fbl/mutex.h>

typedef enum {
  OT_SPINEL_DEVICE_ON,
  OT_SPINEL_DEVICE_OFF,
} ot_radio_power_status_e;

namespace fake_ot {
constexpr uint32_t kOutboundAllowanceInit = 4;
constexpr uint32_t kOutboundAllowanceInc = 2;
constexpr uint32_t kMaxFrameSize = 2048;
constexpr uint32_t kLoopTimeOutMsOneDay = 1000 * 60 * 60 * 24;  // 24 hours
constexpr uint32_t kResetMsDelay = 100;
constexpr uint32_t kBitMaskHigherFourBits = 0xF0;
constexpr uint32_t kBitMaskLowerFourBits = 0x0F;

class FakeOtRadioDevice : public ddk::Device<FakeOtRadioDevice, ddk::Unbindable, ddk::Messageable>,
                          public llcpp::fuchsia::lowpan::spinel::DeviceSetup::Interface {
 public:
  explicit FakeOtRadioDevice(zx_device_t* device);

  static zx_status_t Create(void* ctx, zx_device_t* parent,
                            std::unique_ptr<FakeOtRadioDevice>* out);
  static zx_status_t CreateBindAndStart(void* ctx, zx_device_t* parent);
  zx_status_t Bind(void);
  zx_status_t Start(void);
  zx_status_t StartLoopThread();

  void DdkRelease();
  void DdkUnbind(ddk::UnbindTxn txn);
  zx_status_t DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn);
  zx_status_t ShutDown();
  zx_status_t Reset();

 private:
  // FIDL request handlers
  void SetChannel(zx::channel channel, SetChannelCompleter::Sync& _completer);
  // Loop
  zx_status_t RadioThread();
  uint32_t GetTimeoutMs();
  zx_status_t TrySendInboundFrame();
  void TryHandleOutboundFrame();
  void FrameHandler(::fidl::VectorView<uint8_t> data);
  void PostSendInboundFrameTask(std::vector<uint8_t> packet);

  uint8_t ValidateSpinelHeaderAndGetTid(const uint8_t* data, uint32_t len);

  // Nested class for FIDL implementation
  class LowpanSpinelDeviceFidlImpl : public llcpp::fuchsia::lowpan::spinel::Device::Interface {
   public:
    LowpanSpinelDeviceFidlImpl(FakeOtRadioDevice& ot_radio);
    zx_status_t Bind(async_dispatcher_t* dispatcher, zx::channel channel);

   private:
    // FIDL request handlers
    void Open(OpenCompleter::Sync& completer);
    void Close(CloseCompleter::Sync& completer);
    void GetMaxFrameSize(GetMaxFrameSizeCompleter::Sync& completer);
    void SendFrame(::fidl::VectorView<uint8_t> data, SendFrameCompleter::Sync& completer);
    void ReadyToReceiveFrames(uint32_t number_of_frames,
                              ReadyToReceiveFramesCompleter::Sync& completer);

    FakeOtRadioDevice& ot_radio_obj_;
  };

  std::thread event_loop_thread_;
  async::Loop loop_;
  zx::port port_;

  fbl::Mutex inbound_lock_;
  std::queue<std::vector<uint8_t>> inbound_queue_ __TA_GUARDED(inbound_lock_);

  fbl::Mutex outbound_lock_;
  std::queue<::fidl::VectorView<uint8_t>> outbound_queue_ __TA_GUARDED(outbound_lock_);

  uint32_t inbound_allowance_ = 0;
  uint32_t outbound_allowance_ = kOutboundAllowanceInit;
  uint64_t inbound_cnt_ = 0;
  uint64_t outbound_cnt_ = 0;
  std::optional<fidl::ServerBindingRef<llcpp::fuchsia::lowpan::spinel::Device>> fidl_binding_;
  std::unique_ptr<LowpanSpinelDeviceFidlImpl> fidl_impl_obj_ = 0;
  ot_radio_power_status_e power_status_ = OT_SPINEL_DEVICE_OFF;
};

}  // namespace fake_ot

#endif  // SRC_CONNECTIVITY_OPENTHREAD_TESTS_FAKE_DRIVERS_FAKE_OT_RADIO_FAKE_OT_RADIO_H_
