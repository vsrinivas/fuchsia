// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ISOLATED_DEVMGR_ISOLATED_DEVMGR_H_
#define SRC_LIB_ISOLATED_DEVMGR_ISOLATED_DEVMGR_H_

#include <lib/async/cpp/exception.h>
#include <lib/devmgr-integration-test/fixture.h>

#include <memory>

namespace isolated_devmgr {
class IsolatedDevmgr {
 public:
  using ExceptionCallback = fit::function<void()>;
  IsolatedDevmgr(async_dispatcher_t* dispatcher,
                 devmgr_integration_test::IsolatedDevmgr devmgr)
      : devmgr_(std::move(devmgr)),
        watcher_(this, devmgr_.containing_job().get(), 0) {
    watcher_.Bind(dispatcher);
  }

  ~IsolatedDevmgr() = default;

  int root() { return devmgr_.devfs_root().get(); }

  void Connect(zx::channel req);
  zx_status_t WaitForFile(const char* path);

  void SetExceptionCallback(ExceptionCallback cb) {
    exception_callback_ = std::move(cb);
  }

  static std::unique_ptr<IsolatedDevmgr> Create(
      devmgr_launcher::Args args, async_dispatcher_t* dispatcher = nullptr);

 private:
  void DevmgrException(async_dispatcher_t* dispatcher,
                       async::ExceptionBase* exception, zx_status_t status,
                       const zx_port_packet_t* report);

  ExceptionCallback exception_callback_;
  devmgr_integration_test::IsolatedDevmgr devmgr_;
  async::ExceptionMethod<IsolatedDevmgr, &IsolatedDevmgr::DevmgrException>
      watcher_;
};
}  // namespace isolated_devmgr

#endif  // SRC_LIB_ISOLATED_DEVMGR_ISOLATED_DEVMGR_H_
