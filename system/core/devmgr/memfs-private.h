// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/thread_annotations.h>
#include <zircon/types.h>
#include <fdio/io.h>
#include <fdio/remoteio.h>
#include <fdio/vfs.h>

#ifdef __cplusplus

#include <memfs/vnode.h>

using VnodeMemfs = memfs::VnodeMemfs;
using VnodeDir = memfs::VnodeDir;

fbl::RefPtr<VnodeDir> BootfsRoot();
fbl::RefPtr<VnodeDir> MemfsRoot();
fbl::RefPtr<VnodeDir> SystemfsRoot();
fbl::RefPtr<VnodeDir> DevfsRoot();

#else

typedef struct VnodeMemfs VnodeMemfs;
typedef struct VnodeDir VnodeDir;

#endif  // ifdef __cplusplus

__BEGIN_CDECLS

void vfs_global_init(VnodeDir* root);
void vfs_watch_exit(zx_handle_t event);

// generate mxremoteio handles
zx_status_t vfs_create_global_root_handle(zx_handle_t* out);
zx_status_t vfs_connect_global_root_handle(zx_handle_t h);
zx_status_t vfs_create_root_handle(VnodeMemfs* vn, zx_handle_t* out);
zx_status_t vfs_connect_root_handle(VnodeMemfs* vn, zx_handle_t h);

zx_status_t vfs_install_fs(const char* path, zx_handle_t h);

// device fs
zx_status_t devfs_mount(zx_handle_t h);

// boot fs
zx_status_t bootfs_add_file(const char* path, zx_handle_t vmo, zx_off_t off, size_t len);

// system fs
VnodeDir* systemfs_get_root(void);
zx_status_t systemfs_add_file(const char* path, zx_handle_t vmo, zx_off_t off, size_t len);
void systemfs_set_readonly(bool value);

// Create the global root to memfs
VnodeDir* vfs_create_global_root(void) TA_NO_THREAD_SAFETY_ANALYSIS;

zx_status_t memfs_mount(VnodeDir* parent, const char* name, VnodeDir* subtree);

void devmgr_vfs_exit(void);

__END_CDECLS
