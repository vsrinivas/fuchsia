// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_OPENTHREAD_TESTS_FAKE_DRIVERS_FAKE_OT_RADIO_FAKE_OT_RADIO_H_
#define SRC_CONNECTIVITY_OPENTHREAD_TESTS_FAKE_DRIVERS_FAKE_OT_RADIO_FAKE_OT_RADIO_H_

#include <fidl/fuchsia.lowpan.spinel/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/ddk/device.h>
#include <lib/fidl/llcpp/server.h>
#include <lib/zx/port.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <array>
#include <atomic>
#include <queue>
#include <thread>

#include <ddktl/device.h>
#include <fbl/mutex.h>

typedef enum {
  OT_SPINEL_DEVICE_ON,
  OT_SPINEL_DEVICE_OFF,
} ot_radio_power_status_e;

namespace fake_ot {
constexpr uint32_t kRadioboundAllowanceInit = 4;
constexpr uint32_t kRadioboundAllowanceInc = 2;
constexpr uint32_t kMaxFrameSize = 2048;
constexpr uint32_t kLoopTimeOutMsOneDay = 1000 * 60 * 60 * 24;  // 24 hours
constexpr uint32_t kResetMsDelay = 100;
constexpr uint32_t kBitMaskHigherFourBits = 0xF0;
constexpr uint32_t kBitMaskLowerFourBits = 0x0F;

class FakeOtRadioDevice;
using DeviceType = ddk::Device<FakeOtRadioDevice, ddk::Unbindable,
                               ddk::Messageable<fuchsia_lowpan_spinel::DeviceSetup>::Mixin>;

class FakeOtRadioDevice : public DeviceType {
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
  zx_status_t ShutDown();
  zx_status_t Reset();

 private:
  // FIDL request handlers
  void SetChannel(SetChannelRequestView request, SetChannelCompleter::Sync& _completer) override;
  // Loop
  zx_status_t RadioThread();
  uint32_t GetTimeoutMs();
  zx_status_t TrySendClientboundFrame();
  void TryHandleRadioboundFrame();
  void FrameHandler(::fidl::VectorView<uint8_t> data);
  void PostSendClientboundFrameTask(std::vector<uint8_t> packet);

  static uint8_t ValidateSpinelHeaderAndGetTid(const uint8_t* data, uint32_t len);

  // Nested class for FIDL implementation
  class LowpanSpinelDeviceFidlImpl : public fidl::WireServer<fuchsia_lowpan_spinel::Device> {
   public:
    explicit LowpanSpinelDeviceFidlImpl(FakeOtRadioDevice& ot_radio);
    void Bind(async_dispatcher_t* dispatcher,
              fidl::ServerEnd<fuchsia_lowpan_spinel::Device> channel);

   private:
    // FIDL request handlers
    void Open(OpenRequestView request, OpenCompleter::Sync& completer) override;
    void Close(CloseRequestView request, CloseCompleter::Sync& completer) override;
    void GetMaxFrameSize(GetMaxFrameSizeRequestView request,
                         GetMaxFrameSizeCompleter::Sync& completer) override;
    void SendFrame(SendFrameRequestView request, SendFrameCompleter::Sync& completer) override;
    void ReadyToReceiveFrames(ReadyToReceiveFramesRequestView request,
                              ReadyToReceiveFramesCompleter::Sync& completer) override;

    FakeOtRadioDevice& ot_radio_obj_;
  };

  std::thread event_loop_thread_;
  async::Loop loop_;
  zx::port port_;

  fbl::Mutex clientbound_lock_;
  std::queue<std::vector<uint8_t>> clientbound_queue_ __TA_GUARDED(clientbound_lock_);

  fbl::Mutex radiobound_lock_;
  std::queue<::fidl::VectorView<uint8_t>> radiobound_queue_ __TA_GUARDED(radiobound_lock_);

  uint32_t clientbound_allowance_ = 0;
  uint32_t radiobound_allowance_ = kRadioboundAllowanceInit;
  uint64_t clientbound_cnt_ = 0;
  uint64_t radiobound_cnt_ = 0;
  std::optional<fidl::ServerBindingRef<fuchsia_lowpan_spinel::Device>> fidl_binding_;
  std::unique_ptr<LowpanSpinelDeviceFidlImpl> fidl_impl_obj_ = 0;
  ot_radio_power_status_e power_status_ = OT_SPINEL_DEVICE_OFF;
};

}  // namespace fake_ot

#endif  // SRC_CONNECTIVITY_OPENTHREAD_TESTS_FAKE_DRIVERS_FAKE_OT_RADIO_FAKE_OT_RADIO_H_
