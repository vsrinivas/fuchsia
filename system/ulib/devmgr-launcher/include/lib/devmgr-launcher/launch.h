// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/zx/channel.h>
#include <lib/zx/job.h>

namespace devmgr_launcher {

// Launches an isolated devmgr.
//
// |driver_search_path| specifies an absolute path in devmgr's view of the
// filesystem to search for drivers in.  The search is non-recursive.  If
// nullptr, this uses devmgr's default.
//
// |sys_device_path| specifies which driver should be bound to the sys_device
// (the top-level device for most devices).  If nullptr, this uses devmgr's
// default.
//
// Returns its containing job and a channel to the root of its devfs.
// To destroy the devmgr, issue |devmgr_job->kill()|.
zx_status_t Launch(const char* driver_search_path, const char* sys_device_path,
                   zx::job* devmgr_job, zx::channel* devfs_root);

} // namespace devmgr_launcher
