// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_OPENTHREAD_OT_STACK_SRC_APP_H_
#define SRC_CONNECTIVITY_OPENTHREAD_OT_STACK_SRC_APP_H_

#include <fidl/fuchsia.lowpan.spinel/cpp/wire.h>
#include <fidl/fuchsia.openthread.devmgr/cpp/wire.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/ot-stack/ot-stack-callback.h>
#include <lib/svc/outgoing.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/port.h>

#include <list>
#include <thread>

#include <fbl/mutex.h>
#include <fbl/unique_fd.h>

#include "bootstrap_fidl_impl.h"
#include "ncp_fidl.h"

namespace otstack {

namespace fidl_spinel = fuchsia_lowpan_spinel;

constexpr uint32_t kOutboundAllowanceInit = 4;
constexpr uint32_t kOutboundAllowanceInc = 2;
constexpr uint32_t kInboundAllowanceInit = 4;
constexpr uint32_t kInboundAllowanceInc = 2;
constexpr uint32_t kMaxFrameSize = 1300;

enum {
  kPortRadioChannelRead = 1,
  kPortSendClientEvent,
  kPortTerminate,
};

class OtStackApp : public fidl::WireSyncEventHandler<fidl_spinel::Device> {
 public:
  OtStackApp() = default;

  zx_status_t Init(const std::string& path, bool is_test_env);
  async::Loop* loop() { return &loop_; }

  void EventLoopHandleInboundFrame(::fidl::VectorView<uint8_t> data);
  void HandleRadioOnReadyForSendFrame(uint32_t allowance);
  void HandleClientReadyToReceiveFrames(uint32_t allowance);
  void PushFrameToOtLib();

 private:
  zx_status_t InitOutgoingAndServe();
  zx_status_t SetupFidlService();
  zx_status_t SetupBootstrapFidlService();
  zx_status_t ConnectToOtRadioDev();
  zx_status_t SetDeviceSetupClientInDevmgr(const std::string& path);
  zx_status_t SetDeviceSetupClientInIsolatedDevmgr(const std::string& path);
  zx_status_t SetupOtRadioDev();
  zx_status_t InitRadioDriver();
  void InitOpenThreadLibrary(bool reset_rcp);

  // Events.
  void OnReadyForSendFrames(
      fidl::WireResponse<fidl_spinel::Device::OnReadyForSendFrames>* event) override;
  void OnReceiveFrame(fidl::WireResponse<fidl_spinel::Device::OnReceiveFrame>* event) override;
  void OnError(fidl::WireResponse<fidl_spinel::Device::OnError>* event) override;

  zx_status_t Unknown() override;

  void ClientAllowanceInit();
  void RadioAllowanceInit();
  void UpdateRadioOutboundAllowance();
  void UpdateRadioInboundAllowance();
  void UpdateClientOutboundAllowance();
  void UpdateClientInboundAllowance();
  void SendOneFrameToClient();
  void AlarmTask();
  void EventThread();
  void TerminateEventThread();
  void DisconnectDevice();
  void ResetAsync();
  void Shutdown();

  class LowpanSpinelDeviceFidlImpl : public fidl::WireServer<fidl_spinel::Device> {
   public:
    explicit LowpanSpinelDeviceFidlImpl(OtStackApp& ot_stack_app);

   private:
    // FIDL request handlers
    void Open(OpenRequestView request, OpenCompleter::Sync& completer) override;
    void Close(CloseRequestView request, CloseCompleter::Sync& completer) override;
    void GetMaxFrameSize(GetMaxFrameSizeRequestView request,
                         GetMaxFrameSizeCompleter::Sync& completer) override;
    void SendFrame(SendFrameRequestView request, SendFrameCompleter::Sync& completer) override;
    void ReadyToReceiveFrames(ReadyToReceiveFramesRequestView request,
                              ReadyToReceiveFramesCompleter::Sync& completer) override;

    OtStackApp& app_;
  };

  class OtStackCallBackImpl : public OtStackCallBack {
   public:
    explicit OtStackCallBackImpl(OtStackApp& ot_stack_app);
    ~OtStackCallBackImpl() override = default;

    void SendOneFrameToRadio(uint8_t* buffer, uint32_t size) override;
    std::vector<uint8_t> WaitForFrameFromRadio(uint64_t timeout_us) override;
    std::vector<uint8_t> Process() override;
    void SendOneFrameToClient(uint8_t* buffer, uint32_t size) override;
    void PostNcpFidlInboundTask() override;
    void PostOtLibTaskletProcessTask() override;
    void PostDelayedAlarmTask(zx::duration delay) override;
    void Reset() override;

   private:
    OtStackApp& app_;
  };

  async::Loop loop_{&kAsyncLoopConfigAttachToCurrentThread};
  std::unique_ptr<ot::Fuchsia::BootstrapThreadImpl> bootstrap_impl_;

  zx::port port_;
  std::thread event_thread_;

  std::optional<fidl::ServerBindingRef<fidl_spinel::Device>> binding_;
  std::unique_ptr<LowpanSpinelDeviceFidlImpl> fidl_request_handler_ptr_;

  std::string device_path_;
  bool connected_to_device_ = false;
  std::unique_ptr<fidl::WireSyncClient<fidl_spinel::DeviceSetup>> device_setup_client_ptr_ =
      nullptr;
  std::unique_ptr<fidl::WireSyncClient<fidl_spinel::Device>> device_client_ptr_ = nullptr;
  zx::unowned_channel device_channel_ = zx::unowned_channel(ZX_HANDLE_INVALID);

  std::unique_ptr<svc::Outgoing> outgoing_ = nullptr;

  sync_completion_t radio_rx_complete_;
  std::unique_ptr<OtStackCallBackImpl> lowpan_spinel_ptr_ = nullptr;
  uint32_t radio_inbound_allowance_ = 0;
  uint32_t radio_inbound_cnt = 0;
  uint32_t radio_outbound_allowance_ __TA_GUARDED(radio_ctrl_flow_mtx_);
  uint32_t radio_outbound_cnt = 0;
  uint32_t client_inbound_allowance_ = 0;
  uint32_t client_inbound_cnt = 0;
  uint32_t client_outbound_allowance_ = 0;
  uint32_t client_outbound_cnt = 0;

  fbl::Mutex radio_q_mtx_;
  fbl::Mutex radio_ctrl_flow_mtx_;

  std::list<std::vector<uint8_t>> radio_inbound_queue_ __TA_GUARDED(radio_q_mtx_);
  std::list<std::vector<uint8_t>> client_outbound_queue_;
  std::list<std::vector<uint8_t>> client_inbound_queue_;

  std::optional<void*> ot_instance_ptr_ = nullptr;
  bool is_test_env_;

  zx_status_t handler_status_ = ZX_OK;
};

}  // namespace otstack

#endif  // SRC_CONNECTIVITY_OPENTHREAD_OT_STACK_SRC_APP_H_
