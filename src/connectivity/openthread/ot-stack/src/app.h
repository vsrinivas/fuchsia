// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_OPENTHREAD_OT_STACK_SRC_APP_H_
#define SRC_CONNECTIVITY_OPENTHREAD_OT_STACK_SRC_APP_H_

#include <fuchsia/lowpan/spinel/llcpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/ot-stack/ot-stack-callback.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/port.h>

#include <list>
#include <thread>

#include <fbl/mutex.h>
#include <fbl/unique_fd.h>

#include "ncp_fidl.h"

namespace otstack {

namespace fidl_spinel = llcpp::fuchsia::lowpan::spinel;

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

typedef enum {
  kInbound = 1,
  kOutbound,
} packet_direction_e;

class OtStackApp {
 public:
  OtStackApp(){};

  zx_status_t Init(const std::string& path, bool is_test_env);
  async::Loop* loop() { return &loop_; }

  void AddFidlRequestHandler(const char* service_name, zx_handle_t service_request);
  void EventLoopHandleInboundFrame(::fidl::VectorView<uint8_t> data);
  void HandleRadioOnReadyForSendFrame(uint32_t allowance);
  void HandleClientReadyToReceiveFrames(uint32_t allowance);
  void PushFrameToOtLib();

 private:
  zx_status_t SetupFidlService();
  zx_status_t ConnectToOtRadioDev();
  zx_status_t SetDeviceSetupClientInDevmgr(const std::string& path);
  zx_status_t SetDeviceSetupClientInIsolatedDevmgr(const std::string& path);
  zx_status_t SetupOtRadioDev();
  zx_status_t ConnectServiceByName(const char name[], zx::channel* out);

  void ClientAllowanceInit();
  void RadioAllowanceInit();
  void UpdateRadioOutboundAllowance();
  void UpdateRadioInboundAllowance();
  void UpdateClientOutboundAllowance();
  void UpdateClientInboundAllowance();
  void SendOneFrameToClient();
  void AlarmTask();
  void HandleEvents();
  void EventThread();
  void TerminateEventThread();
  void DisconnectDevice();
  void Shutdown();

  class LowpanSpinelDeviceFidlImpl : public fidl_spinel::Device::Interface {
   public:
    LowpanSpinelDeviceFidlImpl(OtStackApp& ot_stack_app);
    void Bind(async_dispatcher_t* dispatcher, const char* service_name,
              zx_handle_t service_request);

   private:
    // FIDL request handlers
    void Open(OpenCompleter::Sync completer);
    void Close(CloseCompleter::Sync completer);
    void GetMaxFrameSize(GetMaxFrameSizeCompleter::Sync completer);
    void SendFrame(::fidl::VectorView<uint8_t> data, SendFrameCompleter::Sync completer);
    void ReadyToReceiveFrames(uint32_t number_of_frames,
                              ReadyToReceiveFramesCompleter::Sync completer);

    OtStackApp& app_;
  };

  class OtStackCallBackImpl : public OtStackCallBack {
   public:
    OtStackCallBackImpl(OtStackApp& ot_stack_app);
    ~OtStackCallBackImpl(){};

    void SendOneFrameToRadio(uint8_t* buffer, uint32_t size);
    std::vector<uint8_t> WaitForFrameFromRadio(uint64_t timeout_us);
    std::vector<uint8_t> Process();
    void SendOneFrameToClient(uint8_t* buffer, uint32_t size);
    void PostNcpFidlInboundTask();
    void PostOtLibTaskletProcessTask();
    void PostDelayedAlarmTask(zx::duration delay);

   private:
    OtStackApp& app_;
  };

  zx::port port_;
  std::thread event_thread_;

  std::optional<fidl::ServerBindingRef<fidl_spinel::Device>> binding_;
  std::unique_ptr<LowpanSpinelDeviceFidlImpl> fidl_request_handler_ptr_;

  std::string device_path_;
  bool connected_to_device_ = false;
  std::unique_ptr<fidl_spinel::DeviceSetup::SyncClient> device_setup_client_ptr_ = 0;
  std::unique_ptr<fidl_spinel::Device::SyncClient> device_client_ptr_ = 0;
  zx::unowned_channel device_channel_ = zx::unowned_channel(ZX_HANDLE_INVALID);

  async::Loop loop_{&kAsyncLoopConfigAttachToCurrentThread};

  sync_completion_t radio_rx_complete_;
  std::unique_ptr<OtStackCallBackImpl> lowpan_spinel_ptr_ = 0;
  uint32_t radio_inbound_allowance_ = 0;
  uint32_t radio_inbound_cnt = 0;
  uint32_t radio_outbound_allowance_ = 0;
  uint32_t radio_outbound_cnt = 0;
  uint32_t client_inbound_allowance_ = 0;
  uint32_t client_inbound_cnt = 0;
  uint32_t client_outbound_allowance_ = 0;
  uint32_t client_outbound_cnt = 0;

  fbl::Mutex radio_q_mtx_;

  std::list<std::vector<uint8_t>> radio_inbound_queue_ __TA_GUARDED(radio_q_mtx_);
  std::list<std::vector<uint8_t>> client_outbound_queue_;
  std::list<std::vector<uint8_t>> client_inbound_queue_;

  std::optional<void*> ot_instance_ptr_ = nullptr;
  bool is_test_env_;
  zx::channel isolated_devfs_;
};

}  // namespace otstack

#endif  // SRC_CONNECTIVITY_OPENTHREAD_OT_STACK_SRC_APP_H_
