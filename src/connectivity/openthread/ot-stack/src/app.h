// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_OPENTHREAD_OT_STACK_SRC_APP_H_
#define SRC_CONNECTIVITY_OPENTHREAD_OT_STACK_SRC_APP_H_

#include <fuchsia/lowpan/spinel/llcpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>

#include <fbl/unique_fd.h>
#include <src/lib/syslog/cpp/logger.h>

namespace otstack {

namespace fidl_spinel = llcpp::fuchsia::lowpan::spinel;

struct ConnectRequestContext {
  async_dispatcher_t* dispatcher;
  std::unique_ptr<fidl_spinel::Device::Interface> server;
};

class OtStackApp {
 public:
  OtStackApp(){};

  zx_status_t Init(const std::string& path, bool is_test_env);
  async::Loop* loop() { return &loop_; }

 private:
  zx_status_t SetupFidlService();
  zx_status_t ConnectToOtRadioDev(const std::string& path, bool is_test_env);
  zx_status_t SetDeviceSetupClientInDevmgr(const std::string& path);
  zx_status_t SetDeviceSetupClientInIsolatedDevmgr(const std::string& path);
  zx_status_t SetupOtRadioDev();
  zx_status_t ConnectServiceByName(const char name[], zx::channel* out);
  void Shutdown();

  class LowpanSpinelDeviceFidlImpl : public fidl_spinel::Device::Interface {
   public:
    LowpanSpinelDeviceFidlImpl(OtStackApp& ot_stack_app);

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

  std::unique_ptr<ConnectRequestContext> fidl_req_ctx_ptr_ = 0;
  std::unique_ptr<fidl_spinel::DeviceSetup::SyncClient> device_setup_client_ptr_ = 0;
  std::unique_ptr<fidl_spinel::Device::SyncClient> device_client_ptr_ = 0;
  async::Loop loop_{&kAsyncLoopConfigAttachToCurrentThread};

  zx::channel isolated_devfs_;
};

}  // namespace otstack

#endif  // SRC_CONNECTIVITY_OPENTHREAD_OT_STACK_SRC_APP_H_
