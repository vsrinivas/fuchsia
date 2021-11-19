// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_TESTING_DEVMGR_INTEGRATION_TEST_SHIM_INCLUDE_LIB_DEVMGR_INTEGRATION_TEST_FIXTURE_H_
#define SRC_DEVICES_TESTING_DEVMGR_INTEGRATION_TEST_SHIM_INCLUDE_LIB_DEVMGR_INTEGRATION_TEST_FIXTURE_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/dispatcher.h>
#include <lib/devmgr-launcher/launch.h>

#include <fbl/unique_fd.h>

#include "sdk/lib/driver_test_realm/realm_builder/cpp/lib.h"
#include "src/devices/lib/device-watcher/cpp/device-watcher.h"

namespace devmgr_integration_test {

using device_watcher::RecursiveWaitForFile;

class IsolatedDevmgr {
 public:
  IsolatedDevmgr();
  ~IsolatedDevmgr();
  IsolatedDevmgr(const IsolatedDevmgr&) = delete;
  IsolatedDevmgr& operator=(const IsolatedDevmgr&) = delete;
  IsolatedDevmgr(IsolatedDevmgr&& other);
  IsolatedDevmgr& operator=(IsolatedDevmgr&& other);

  void reset() { *this = IsolatedDevmgr(); }

  // Get an args structure pre-populated with the test sysdev driver, the
  // test control driver, and the test driver directory.
  static devmgr_launcher::Args DefaultArgs();

  // Launch a new isolated devmgr.  The instance will be destroyed when
  // |*out|'s dtor runs.
  // |dispatcher| let's you choose which async loop the exception handler runs on.
  static zx_status_t Create(devmgr_launcher::Args args, IsolatedDevmgr* out);
  static zx_status_t Create(devmgr_launcher::Args args, async_dispatcher_t* dispatcher,
                            IsolatedDevmgr* out);

  // Get a fd to the root of the isolate devmgr's devfs.  This fd
  // may be used with openat() and fdio_watch_directory().
  const fbl::unique_fd& devfs_root() const { return devfs_root_; }

  // Expose devfs in component outgoing directory.
  zx_status_t AddDevfsToOutgoingDir(vfs::PseudoDir* outgoing_root_dir);

 private:
  std::unique_ptr<async::Loop> loop_;
  std::unique_ptr<sys::testing::Realm> realm_;

  // FD to the root of devmgr's devfs
  fbl::unique_fd devfs_root_;
};

}  // namespace devmgr_integration_test

#endif  // SRC_DEVICES_TESTING_DEVMGR_INTEGRATION_TEST_SHIM_INCLUDE_LIB_DEVMGR_INTEGRATION_TEST_FIXTURE_H_
