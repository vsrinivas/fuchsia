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
  IsolatedDevmgr(devmgr_integration_test::IsolatedDevmgr devmgr) : devmgr_(std::move(devmgr)) {}

  ~IsolatedDevmgr() = default;

  int root() { return devmgr_.devfs_root().get(); }

  const fbl::unique_fd& devfs_root() { return devmgr_.devfs_root(); }

  void Connect(zx::channel req);
  zx_status_t WaitForFile(const char* path);

  void SetExceptionCallback(fit::function<void(zx_exception_info_t)> cb) {
    devmgr_.SetExceptionCallback(std::move(cb));
  }

  const zx::process& driver_manager_process() const { return devmgr_.driver_manager_process(); }

  struct ExtraArgs {
    // A list of vid/pid/did triplets to spawn in their own devhosts.
    fbl::Vector<board_test::DeviceEntry> device_list;
  };

  static std::unique_ptr<IsolatedDevmgr> Create(
      devmgr_launcher::Args args,
      std::unique_ptr<fbl::Vector<board_test::DeviceEntry>> device_list_unique_ptr = nullptr,
      async_dispatcher_t* dispatcher = nullptr);

 private:
  devmgr_integration_test::IsolatedDevmgr devmgr_;
};
}  // namespace isolated_devmgr

#endif  // SRC_LIB_ISOLATED_DEVMGR_ISOLATED_DEVMGR_H_
