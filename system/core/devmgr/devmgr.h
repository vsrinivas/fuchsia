// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

void coordinator(void);

void devfs_init(zx_handle_t root_job);

void devmgr_io_init(void);
void devmgr_vfs_init(void);

zx_status_t devmgr_launch(zx_handle_t job, const char* name,
                          int argc, const char* const* argv,
                          const char** envp, int stdiofd,
                          zx_handle_t* handles, uint32_t* types, size_t len,
                          zx_handle_t* proc_out);
void devmgr_launch_devhost(zx_handle_t job,
                           const char* name, int argc, char** argv,
                           zx_handle_t hdevice, zx_handle_t hrpc);
ssize_t devmgr_add_systemfs_vmo(zx_handle_t vmo);
bool secondary_bootfs_ready(void);
int devmgr_start_appmgr(void* arg);
void fshost_start(void);
zx_status_t copy_vmo(zx_handle_t src, zx_off_t offset, size_t length, zx_handle_t* out_dest);

void load_system_drivers(void);

// The variable to set on the kernel command line to enable ld.so tracing
// of the processes we launch.
#define LDSO_TRACE_CMDLINE "ldso.trace"
// The env var to set to enable ld.so tracing.
#define LDSO_TRACE_ENV "LD_TRACE=1"

zx_handle_t fs_root_clone(void);
zx_handle_t devfs_root_clone(void);
zx_handle_t svc_root_clone(void);

void block_device_watcher(zx_handle_t job);

// getenv_bool looks in the environment for name. If not found, it returns
// default. If found, it returns false if the found value matches "0", "off", or
// "false", otherwise it returns true.
bool getenv_bool(const char* key, bool _default);

__END_CDECLS
