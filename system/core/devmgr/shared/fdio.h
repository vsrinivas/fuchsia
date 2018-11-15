// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/zx/channel.h>
#include <launchpad/launchpad.h>

namespace devmgr {

// Signals for synchronizing between devmgr and fshost during fshost launch
#define FSHOST_SIGNAL_READY      ZX_USER_SIGNAL_0  // Signalled by fshost
#define FSHOST_SIGNAL_EXIT       ZX_USER_SIGNAL_1  // Signalled by devmgr
#define FSHOST_SIGNAL_EXIT_DONE  ZX_USER_SIGNAL_2  // Signalled by fshost

// Flags for specifying what should be in a new process's namespace
#define FS_SVC      0x0001
#define FS_DEV      0x0002
#define FS_BOOT     0x0004
#define FS_DATA     0x0010
#define FS_SYSTEM   0x0020
#define FS_BLOB     0x0040
#define FS_VOLUME   0x0080
#define FS_PKGFS    0x0100
#define FS_INSTALL  0x0200
#define FS_TMP      0x0400
#define FS_HUB      0x0800
#define FS_ALL      0xFFFF

#define FS_FOR_FSPROC  (FS_SVC)
#define FS_FOR_APPMGR  (FS_ALL & (~FS_HUB))

#define FS_DIR_FLAGS \
    (ZX_FS_RIGHT_READABLE | ZX_FS_RIGHT_ADMIN |\
    ZX_FS_FLAG_DIRECTORY | ZX_FS_FLAG_NOREMOTE)

zx_status_t devmgr_launch(
    const zx::job& job, const char* name,
    zx_status_t (*load)(void* ctx, launchpad_t*, const char* file), void* ctx,
    int argc, const char* const* argv,
    const char** envp, int stdiofd,
    const zx_handle_t* handles, const uint32_t* types, size_t hcount,
    zx::process* proc_out, uint32_t flags);
zx_status_t devmgr_launch_cmdline(
    const char* me, const zx::job& job, const char* name,
    zx_status_t (*load)(void* ctx, launchpad_t*, const char* file), void* ctx,
    const char* cmdline,
    const zx_handle_t* handles, const uint32_t* types, size_t hcount,
    zx::process* proc_out, uint32_t flags);

// getenv_bool looks in the environment for name. If not found, it returns
// default. If found, it returns false if the found value matches "0", "off", or
// "false", otherwise it returns true.
bool getenv_bool(const char* key, bool _default);

// The variable to set on the kernel command line to enable ld.so tracing
// of the processes we launch.
#define LDSO_TRACE_CMDLINE "ldso.trace"
// The env var to set to enable ld.so tracing.
#define LDSO_TRACE_ENV "LD_TRACE=1"

// Opens a path relative to locally-specified roots.
//
// This acts similar to 'open', but avoids utilizing the local process' namespace.
// Instead, it manually translates hardcoded paths, such as "svc", "dev", etc into
// their corresponding root connection, where the request is forwarded.
//
// This function is implemented by both devmgr and fshost.
zx::channel fs_clone(const char* path);

zx_status_t copy_vmo(zx_handle_t src, zx_off_t offset, size_t length, zx_handle_t* out_dest);

} // namespace devmgr
