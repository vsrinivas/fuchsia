// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <magenta/compiler.h>
#include <magenta/types.h>

__BEGIN_CDECLS

void devmgr_init(mx_handle_t root_job);
void devmgr_handle_messages(void);

void devmgr_io_init(void);
void devmgr_vfs_init(void);
mx_status_t devmgr_launch(mx_handle_t job, const char* name,
                          int argc, const char* const* argv,
                          const char** envp, int stdiofd,
                          mx_handle_t* handles, uint32_t* types, size_t len,
                          mx_handle_t* proc_out);
void devmgr_launch_devhost(mx_handle_t job,
                           const char* name, int argc, char** argv,
                           mx_handle_t hdevice, mx_handle_t hrpc);
ssize_t devmgr_add_systemfs_vmo(mx_handle_t vmo);
bool secondary_bootfs_ready(void);
int devmgr_start_appmgr(void* arg);

void load_system_drivers(void);

// The variable to set on the kernel command line to enable ld.so tracing
// of the processes we launch.
#define LDSO_TRACE_CMDLINE "ldso.trace"
// The env var to set to enable ld.so tracing.
#define LDSO_TRACE_ENV "LD_TRACE=1"

mx_handle_t get_service_root(void);

void block_device_watcher(mx_handle_t job);

// getenv_bool looks in the environment for name. If not found, it returns
// default. If found, it returns false if the found value matches "0", "off", or
// "false", otherwise it returns true.
bool getenv_bool(const char* key, bool _default);

__END_CDECLS
