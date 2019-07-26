// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/metadata/test.h>
#include <fbl/unique_fd.h>
#include <fbl/unique_ptr.h>
#include <lib/devmgr-integration-test/fixture.h>

namespace driver_integration_test {

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
    // /boot is always forwarded from the parent namespace, and /svc will be forwarded if
    // |use_system_svchost| is true. This argument may be used to allow the isolated devmgr
    // access to drivers from /system/drivers.
    std::vector<std::pair<const char*, zx::channel>> flat_namespace;
    // A list of vid/pid/did triplets to spawn in their own devhosts.
    fbl::Vector<board_test::DeviceEntry> device_list;
    // A list of kernel cmdline arguments to pass to the devmgr process.
    fbl::Vector<const char*> arguments;
    // If set to true, the block watcher will be disabled.
    bool disable_block_watcher = true;
    // If set to true, the netsvc will be disabled.
    bool disable_netsvc = true;
  };

  // Launch a new isolated devmgr.  The instance will be destroyed when
  // |*out|'s dtor runs.
  static zx_status_t Create(Args* args, IsolatedDevmgr* out);

  // Get a fd to the root of the isolate devmgr's devfs.  This fd
  // may be used with openat() and fdio_watch_directory().
  const fbl::unique_fd& devfs_root() const { return devmgr_.devfs_root(); }

 private:
  devmgr_integration_test::IsolatedDevmgr devmgr_;
};

}  // namespace driver_integration_test
