// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <sys/types.h>

#include <fbl/function.h>
#include <launchpad/launchpad.h>
#include <lib/zx/channel.h>
#include <lib/zx/job.h>
#include <lib/zx/process.h>
#include <lib/zx/vmo.h>
#include <zircon/compiler.h>
#include <zircon/types.h>
#include <zircon/device/vfs.h>

namespace devmgr {

struct Device;
struct Devnode;

extern zx_handle_t virtcon_open;

// Initializes a devfs directory from |device|.
void devfs_init(Device* device, async_dispatcher_t* dispatcher);

// Watches the devfs directory |dn|, and sends events to |watcher|.
zx_status_t devfs_watch(Devnode* dn, zx::channel h, uint32_t mask);

// Borrows the channel connected to the root of devfs.
zx::unowned_channel devfs_root_borrow();

// Clones the channel connected to the root of devfs.
zx::channel devfs_root_clone();

// Tells VFS to exit by shutting down the fshost.
void devmgr_vfs_exit();

zx_handle_t get_root_resource();
zx::job get_sysinfo_job_root();

} // namespace devmgr
