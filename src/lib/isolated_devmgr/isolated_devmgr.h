// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ISOLATED_DEVMGR_ISOLATED_DEVMGR_H_
#define SRC_LIB_ISOLATED_DEVMGR_ISOLATED_DEVMGR_H_

#include <lib/async/cpp/wait.h>
#include <lib/devmgr-integration-test/fixture.h>
#include <lib/zx/channel.h>

#include <memory>

#include <ddk/metadata/test.h>

namespace isolated_devmgr {

class IsolatedDevmgr {
 public:
  using ExceptionCallback = fit::function<void()>;
  IsolatedDevmgr(async_dispatcher_t* dispatcher, devmgr_integration_test::IsolatedDevmgr devmgr)
      : devmgr_(std::move(devmgr)), watcher_(this) {
    devmgr_.containing_job().create_exception_channel(0, &devmgr_exception_channel_);

    watcher_.set_object(devmgr_exception_channel_.get());
    watcher_.set_trigger(ZX_CHANNEL_READABLE);
    watcher_.Begin(dispatcher);
  }

  ~IsolatedDevmgr() = default;

  int root() { return devmgr_.devfs_root().get(); }

  const fbl::unique_fd& devfs_root() { return devmgr_.devfs_root(); }

  void Connect(zx::channel req);
  zx_status_t WaitForFile(const char* path);

  void SetExceptionCallback(ExceptionCallback cb) { exception_callback_ = std::move(cb); }

  struct ExtraArgs {
    // A list of vid/pid/did triplets to spawn in their own devhosts.
    fbl::Vector<board_test::DeviceEntry> device_list;
  };

  static std::unique_ptr<IsolatedDevmgr> Create(
      devmgr_launcher::Args args,
      std::unique_ptr<fbl::Vector<board_test::DeviceEntry>> device_list_unique_ptr = nullptr,
      async_dispatcher_t* dispatcher = nullptr);

 private:
  void DevmgrException(async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
                       const zx_packet_signal_t* signal);
  void HandleException();

  ExceptionCallback exception_callback_;
  devmgr_integration_test::IsolatedDevmgr devmgr_;
  zx::channel devmgr_exception_channel_;
  async::WaitMethod<IsolatedDevmgr, &IsolatedDevmgr::DevmgrException> watcher_;
};
}  // namespace isolated_devmgr

#endif  // SRC_LIB_ISOLATED_DEVMGR_ISOLATED_DEVMGR_H_
