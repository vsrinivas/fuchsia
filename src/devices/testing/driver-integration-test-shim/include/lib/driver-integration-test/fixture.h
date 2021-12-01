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

#include "sdk/lib/device-watcher/cpp/device-watcher.h"
#include "sdk/lib/driver_test_realm/realm_builder/cpp/lib.h"

namespace devmgr_integration_test {
using device_watcher::RecursiveWaitForFile;
}

namespace driver_integration_test {

class IsolatedDevmgr {
 public:
  struct Args {
    // A list of vid/pid/did triplets to spawn in their own devhosts.
    fbl::Vector<board_test::DeviceEntry> device_list;
    std::vector<fuchsia::driver::test::DriverLog> log_level;
  };

  // Launch a new isolated devmgr.  The instance will be destroyed when
  // |*out|'s dtor runs.
  static zx_status_t Create(Args* args, IsolatedDevmgr* out);

  // Get a fd to the root of the isolate devmgr's devfs.  This fd
  // may be used with openat() and fdio_watch_directory().
  const fbl::unique_fd& devfs_root() const { return devfs_root_; }

 private:
  std::unique_ptr<async::Loop> loop_;
  std::unique_ptr<sys::testing::Realm> realm_;

  // FD to the root of devmgr's devfs
  fbl::unique_fd devfs_root_;
};

}  // namespace driver_integration_test

#endif  // SRC_DEVICES_TESTING_DRIVER_INTEGRATION_TEST_SHIM_INCLUDE_LIB_DRIVER_INTEGRATION_TEST_FIXTURE_H_
