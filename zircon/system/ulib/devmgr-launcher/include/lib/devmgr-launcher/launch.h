// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/unique_fd.h>
#include <fbl/vector.h>
#include <lib/fdio/io.h>
#include <lib/zx/channel.h>
#include <lib/zx/job.h>
#include <lib/zx/vmo.h>

namespace devmgr_launcher {

struct Args {
    // A list of absolute paths (in devmgr's view of the filesystem) to search
    // for drivers in.  The search is non-recursive.  If empty, this uses
    // devmgr's default.
    fbl::Vector<const char*> driver_search_paths;
    // A list of absolute paths (in devmgr's view of the filesystem) to load
    // drivers from.  This differs from |driver_search_paths| in that it
    // specifies specific drivers rather than entire directories.
    fbl::Vector<const char*> load_drivers;
    // An absolute path (in devmgr's view of the filesystem) for which driver
    // should be bound to the sys_device (the top-level device for most
    // devices).  If nullptr, this uses devmgr's default.
    const char* sys_device_driver = nullptr;
    // VMO containing ZBI passed in from bootloader. Devmgr will simply
    // forward this along to the sys_device as well as the fs_host.
    zx::vmo bootdata;
    // If valid, the FD to give to devmgr as stdin/stdout/stderr.  Otherwise
    // inherits from the caller of Launch().
    fbl::unique_fd stdio;
    // Select whether to use the system svchost or to launch a new one
    bool use_system_svchost = false;
};

// Launches an isolated devmgr, passing the given |args| to it.
//
// Returns its containing job and a channel to the root of its devfs.
// To destroy the devmgr, issue |devmgr_job->kill()|.
zx_status_t Launch(Args args, zx::job* devmgr_job, zx::channel* devfs_root);

} // namespace devmgr_launcher
