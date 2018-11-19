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

void coordinator();

void devfs_init(const zx::job& root_job);

void devmgr_svc_init();
void devmgr_vfs_init();
void devmgr_set_bootdata(zx::unowned_vmo vmo);

zx_status_t devmgr_load_file(const char* path, zx::vmo* out_vmo, uint32_t* out_size);
zx_status_t devmgr_launch_load(void* ctx, launchpad_t* lp, const char* file);

bool secondary_bootfs_ready();

void fshost_start();

zx::job get_sysinfo_job_root();

void load_system_drivers();

void devmgr_disable_appmgr_services();

// Borrows the channel connected to the root of devfs.
zx::unowned_channel devfs_root_borrow();

// Clones the channel connected to the root of devfs.
zx::channel devfs_root_clone();

// Global flag tracking if devmgr believes this is a full Fuchsia build
// (requiring /system, etc) or not.
extern bool require_system;

void devmgr_vfs_exit();
zx_handle_t get_root_resource();

} // namespace devmgr
