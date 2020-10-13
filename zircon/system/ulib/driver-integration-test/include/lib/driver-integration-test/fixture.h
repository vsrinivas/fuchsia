// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_DRIVER_INTEGRATION_TEST_FIXTURE_H_
#define LIB_DRIVER_INTEGRATION_TEST_FIXTURE_H_

#include <lib/devmgr-integration-test/fixture.h>
#include <lib/fit/function.h>

#include <ddk/metadata/test.h>
#include <fbl/string.h>
#include <fbl/unique_fd.h>

namespace driver_integration_test {

using SuspendCallback = fit::function<void(zx_status_t status)>;
class IsolatedDevmgr {
 public:
  struct Args {
    // A list of absolute paths (in devmgr's view of the filesystem) to search
    // for drivers in.  The search is non-recursive.  If empty, this uses
    // devmgr's default.
    fbl::Vector<const char*> driver_search_paths;
    // A list of absolute paths (in devmgr's view of the filesystem) to load
    // drivers from.  This differs from |driver_search_paths| in that it
    // specifies specific drivers rather than entire directories.
    fbl::Vector<const char*> load_drivers;
    // A list of path prefixes and channels to add to the isolated devmgr's namespace. Note that
    // /boot is always forwarded from the parent namespace, and a /svc is always provided that
    // forwards fuchsia.process.Launcher from the parent namespace. This argument may be used to
    // allow the isolated devmgr access to drivers from /system/drivers.
    std::vector<std::pair<const char*, zx::channel>> flat_namespace;
    // A list of vid/pid/did triplets to spawn in their own devhosts.
    fbl::Vector<board_test::DeviceEntry> device_list;
    // A list of kernel cmdline arguments to pass to the devmgr process.
    fbl::Vector<const char*> arguments;
    // A map of boot arguments. See devmgr_lanucher::Args::boot_args.
    std::map<std::string, std::string> boot_args;
    // A board name to appear.
    fbl::String board_name;
    // Board_revision
    uint32_t board_revision;
    // If set to true, the block watcher will be disabled.
    bool disable_block_watcher = true;
    // If set to true, the netsvc will be disabled.
    bool disable_netsvc = true;

    bool no_exit_after_suspend = true;
  };

  // Notifies if driver manager job has an exception.
  void SetExceptionCallback(fit::closure callback) {
    devmgr_.SetExceptionCallback(std::move(callback));
  }

  // Returns true if any process in driver manager process crashes.
  bool crashed() const { return devmgr_.crashed(); }

  // Launch a new isolated devmgr.  The instance will be destroyed when
  // |*out|'s dtor runs.
  static zx_status_t Create(Args* args, IsolatedDevmgr* out);

  // Get a fd to the root of the isolate devmgr's devfs.  This fd
  // may be used with openat() and fdio_watch_directory().
  const fbl::unique_fd& devfs_root() const { return devmgr_.devfs_root(); }

  const zx::channel& svc_root_dir() const { return devmgr_.svc_root_dir(); }

  const zx::channel& fshost_outgoing_dir() const { return devmgr_.fshost_outgoing_dir(); }

  const zx::channel& component_lifecycle_svc() const { return devmgr_.component_lifecycle_svc(); }

 private:
  devmgr_integration_test::IsolatedDevmgr devmgr_;
};

}  // namespace driver_integration_test

#endif  // LIB_DRIVER_INTEGRATION_TEST_FIXTURE_H_
