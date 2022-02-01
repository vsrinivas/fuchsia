// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_TESTING_DRIVER_INTEGRATION_TEST_SHIM_INCLUDE_LIB_DRIVER_INTEGRATION_TEST_FIXTURE_H_
#define SRC_DEVICES_TESTING_DRIVER_INTEGRATION_TEST_SHIM_INCLUDE_LIB_DRIVER_INTEGRATION_TEST_FIXTURE_H_

#include <fidl/fuchsia.io/cpp/wire.h>
#include <fuchsia/diagnostics/cpp/fidl.h>
#include <fuchsia/driver/test/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/dispatcher.h>

#include <vector>

#include <ddk/metadata/test.h>
#include <fbl/string.h>
#include <fbl/unique_fd.h>
#include <fbl/vector.h>
#include <sdk/lib/device-watcher/cpp/device-watcher.h>

#include "sdk/lib/driver_test_realm/realm_builder/cpp/lib.h"

namespace devmgr_integration_test {
using device_watcher::DirWatcher;
using device_watcher::RecursiveWaitForFile;
using device_watcher::RecursiveWaitForFileReadOnly;
}  // namespace devmgr_integration_test

namespace driver_integration_test {

class IsolatedDevmgr {
 public:
  struct Args {
    // A list of vid/pid/did triplets to spawn in their own devhosts.
    fbl::Vector<board_test::DeviceEntry> device_list;
    std::vector<fuchsia::driver::test::DriverLog> log_level;

    // If this is true then tell fshost not to create a block watcher.
    bool disable_block_watcher = true;

    // A board name to appear.
    fbl::String board_name;
    std::vector<std::string> driver_disable;
    std::vector<std::string> driver_bind_eager;
  };

  static Args DefaultArgs() { return Args{}; }

  // Launch a new isolated devmgr.  The instance will be destroyed when
  // |*out|'s dtor runs.
  static zx_status_t Create(Args* args, IsolatedDevmgr* out);

  // Get a fd to the root of the isolate devmgr's devfs.  This fd
  // may be used with openat() and fdio_watch_directory().
  const fbl::unique_fd& devfs_root() const { return devfs_root_; }

  zx_status_t Connect(const std::string& interface_name, zx::channel channel) {
    return realm_->Connect(interface_name, std::move(channel));
  }

  fidl::ClientEnd<fuchsia_io::Directory> fshost_svc_dir() const {
    auto root = realm_->CloneRoot();
    return fidl::ClientEnd<fuchsia_io::Directory>(root.TakeChannel());
  }

 private:
  // `loop_` must come before `realm_` so that they are destroyed in order.
  // That is, `realm_` needs to be destroyed before `loop_` because it will
  // hold a reference to `loop_` async dispatcher object.
  std::unique_ptr<async::Loop> loop_;
  std::unique_ptr<component_testing::RealmRoot> realm_;

  // FD to the root of devmgr's devfs
  fbl::unique_fd devfs_root_;
};

}  // namespace driver_integration_test

#endif  // SRC_DEVICES_TESTING_DRIVER_INTEGRATION_TEST_SHIM_INCLUDE_LIB_DRIVER_INTEGRATION_TEST_FIXTURE_H_
