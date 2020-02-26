// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_OPENTHREAD_OT_STACK_SRC_APP_H_
#define SRC_CONNECTIVITY_OPENTHREAD_OT_STACK_SRC_APP_H_

#include <fuchsia/lowpan/spinel/llcpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl-async/cpp/async_bind.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/zx/port.h>

#include <thread>

#include <fbl/unique_fd.h>
#include <src/lib/syslog/cpp/logger.h>

namespace otstack {

namespace fidl_spinel = llcpp::fuchsia::lowpan::spinel;

enum {
  kPortPktChannelRead = 1,
  kPortPktTerminate,
};

class OtStackApp {
 public:
  OtStackApp(){};

  zx_status_t Init(const std::string& path, bool is_test_env);
  async::Loop* loop() { return &loop_; }

  void AddFidlRequestHandler(const char* service_name, zx_handle_t service_request);
  void RemoveFidlRequestHandler(zx_handle_t service_request, fidl::UnboundReason reason);

 private:
  zx_status_t SetupFidlService();
  zx_status_t ConnectToOtRadioDev();
  zx_status_t SetDeviceSetupClientInDevmgr(const std::string& path);
  zx_status_t SetDeviceSetupClientInIsolatedDevmgr(const std::string& path);
  zx_status_t SetupOtRadioDev();
  zx_status_t ConnectServiceByName(const char name[], zx::channel* out);
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

  zx::port port_;
  std::thread event_thread_;

  zx::channel fidl_request_handle_ = zx::channel(ZX_HANDLE_INVALID);
  std::unique_ptr<LowpanSpinelDeviceFidlImpl> fidl_request_handler_ptr_;

  std::string device_path_;
  bool connected_to_device_ = false;
  std::unique_ptr<fidl_spinel::DeviceSetup::SyncClient> device_setup_client_ptr_ = 0;
  std::unique_ptr<fidl_spinel::Device::SyncClient> device_client_ptr_ = 0;
  zx::unowned_channel device_channel_ = zx::unowned_channel(ZX_HANDLE_INVALID);

  async::Loop loop_{&kAsyncLoopConfigAttachToCurrentThread};

  bool is_test_env_;
  zx::channel isolated_devfs_;
};

}  // namespace otstack

#endif  // SRC_CONNECTIVITY_OPENTHREAD_OT_STACK_SRC_APP_H_
