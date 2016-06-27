// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <ddk/device.h>
#include <magenta/types.h>
#include <mxio/vfs.h>

void vfs_init(vnode_t* root);

// generate mxremoteio handles
mx_handle_t vfs_create_root_handle(void);
mx_handle_t vfs_create_handle(vnode_t* vn);

mx_status_t vfs_open_handles(mx_handle_t* hnds, uint32_t* ids, uint32_t arg,
                             const char* path, uint32_t flags);

// device fs
vnode_t* devfs_get_root(void);
mx_status_t devfs_add_node(vnode_t** out, vnode_t* parent, const char* name, mx_device_t* dev);
mx_status_t devfs_add_link(vnode_t* parent, const char* name, mx_device_t* dev);
mx_status_t devfs_remove(vnode_t* vn);

// boot fs
vnode_t* bootfs_get_root(void);
mx_status_t bootfs_add_file(const char* path, void* data, size_t len);

// memory fs
vnode_t* memfs_get_root(void);

vnode_t* vfs_get_root(void);

// shared among all memory filesystems
mx_status_t memfs_lookup(vnode_t* parent, vnode_t** out, const char* name, size_t len);
mx_status_t memfs_readdir(vnode_t* parent, void* cookie, void* data, size_t len);
ssize_t memfs_read_none(vnode_t* vn, void* data, size_t len, size_t off);
ssize_t memfs_write_none(vnode_t* vn, const void* data, size_t len, size_t off);