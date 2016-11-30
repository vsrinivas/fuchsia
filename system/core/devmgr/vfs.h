// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <magenta/types.h>
#include <mxio/remoteio.h>
#include <mxio/vfs.h>
#include <magenta/listnode.h>
#include <threads.h>

typedef struct dnode dnode_t;

#define MEMFS_TYPE_DATA 0
#define MEMFS_TYPE_DIR 1
#define MEMFS_TYPE_VMO 2
#define MEMFS_TYPE_DEVICE 3
#define MEMFS_TYPE_MASK 0x3
#define MEMFS_FLAG_VMO_REUSE 4

struct vnode {
    VNODE_BASE_FIELDS
    uint32_t seqcount;
    uint32_t memfs_flags; // type + flags

    dnode_t* dnode;      // list of my children

    mx_handle_t remote;

    // all dnodes that point at this vnode
    list_node_t dn_list;
    uint32_t dn_count;

    // all directory watchers
    list_node_t watch_list;

    union {
        struct {
            mx_handle_t h;
            mx_off_t offset; // offset into object
            mx_off_t length; // extent of data
        } vmo;
        struct {
            mx_off_t length;
            uint8_t* block[];
        } data;
    };
};

typedef struct vnode_watcher {
    list_node_t node;
    mx_handle_t h;
} vnode_watcher_t;

#define V_FLAG_DEVICE 1
#define V_FLAG_REMOTE 2
#define V_FLAG_VMOFILE 4

mx_status_t vfs_open(vnode_t* vndir, vnode_t** out, const char* path,
                     const char** pathout, uint32_t flags, uint32_t mode);
mx_status_t vfs_walk(vnode_t* vn, vnode_t** out,
                     const char* path, const char** pathout);

mx_status_t vfs_rename(vnode_t* vn, const char* oldpath, const char* newpath,
                       mx_handle_t rh);

ssize_t vfs_do_ioctl(vnode_t* vn, uint32_t op, const void* in_buf,
                     size_t in_len, void* out_buf, size_t out_len);

mx_handle_t vfs_get_vmofile(vnode_t* vn, mx_off_t* off, mx_off_t* len);

// helper for filling out dents
// returns offset to next vdirent_t on success
mx_status_t vfs_fill_dirent(vdirent_t* de, size_t delen,
                            const char* name, size_t len, uint32_t type);


void vfs_notify_add(vnode_t* vndir, const char* name, size_t namelen);
void vfs_global_init(vnode_t* root);

// generate mxremoteio handles
mx_handle_t vfs_create_global_root_handle(void);
mx_handle_t vfs_create_root_handle(vnode_t* vn);

// vmo fs
ssize_t vmo_read(vnode_t* vn, void* data, size_t len, size_t off);
mx_status_t vmo_getattr(vnode_t* vn, vnattr_t* attr);
void vmo_release(vnode_t* vn);

// device fs
vnode_t* devfs_get_root(void);
mx_status_t memfs_create_device_at(vnode_t* parent, vnode_t** out, const char* name, mx_handle_t hdevice);
mx_status_t devfs_remove(vnode_t* vn);

// boot fs
vnode_t* bootfs_get_root(void);
mx_status_t bootfs_add_file(const char* path, mx_handle_t vmo, mx_off_t off, size_t len);

// system fs
vnode_t* systemfs_get_root(void);
mx_status_t systemfs_add_file(const char* path, mx_handle_t vmo, mx_off_t off, size_t len);

// memory fs
vnode_t* memfs_get_root(void);
mx_status_t memfs_add_link(vnode_t* parent, const char* name, vnode_t* target);
mx_status_t mem_lookup_none(vnode_t* parent, vnode_t** out, const char* name, size_t len);
mx_status_t mem_create_none(vnode_t* vndir, vnode_t** out, const char* name, size_t len, uint32_t mode);
ssize_t mem_write_none(vnode_t* vn, const void* data, size_t len, size_t off);
ssize_t memfs_read_none(vnode_t* vn, void* data, size_t len, size_t off);
mx_status_t mem_readdir_none(vnode_t* parent, void* cookie, void* data, size_t len);

// TODO(orr) normally static; temporary exposure, to be undone in subsequent patch
mx_status_t _mem_create(vnode_t* parent, vnode_t** out,
                        const char* name, size_t namelen,
                        uint32_t type);

// Create the global root to memfs
vnode_t* vfs_create_global_root(void);

// Create a generic root to memfs
vnode_t* vfs_create_root(void);

void vfs_dump_handles(void);

// shared among all memory filesystems
mx_status_t memfs_open(vnode_t** _vn, uint32_t flags);
mx_status_t memfs_close(vnode_t* vn);
mx_status_t memfs_lookup(vnode_t* parent, vnode_t** out, const char* name, size_t len);
mx_status_t memfs_readdir(vnode_t* parent, void* cookie, void* data, size_t len);
mx_status_t mem_truncate_none(vnode_t* vn, size_t len);
mx_status_t mem_rename_none(vnode_t* olddir, vnode_t* newdir,
                              const char* oldname, size_t oldlen,
                              const char* newname, size_t newlen);
ssize_t memfs_read_none(vnode_t* vn, void* data, size_t len, size_t off);
ssize_t mem_write_none(vnode_t* vn, const void* data, size_t len, size_t off);
mx_status_t memfs_unlink(vnode_t* vn, const char* name, size_t len);
ssize_t memfs_ioctl(vnode_t* vn, uint32_t op, const void* in_data, size_t in_len,
                    void* out_data, size_t out_len);
mx_status_t memfs_lookup_name(const vnode_t* vn, char* outname, size_t out_len);

mx_status_t memfs_create_from_buffer(const char* path, uint32_t flags,
                                     const char* ptr, mx_off_t len);
mx_status_t memfs_create_directory(const char* path, uint32_t flags);
mx_status_t memfs_create_from_vmo(const char* path, uint32_t flags,
                                  mx_handle_t vmo, mx_off_t off, mx_off_t len);

mx_status_t vfs_install_remote(vnode_t* vn, mx_handle_t h);
mx_status_t vfs_uninstall_remote(vnode_t* vn);
mx_status_t vfs_uninstall_all(void);

// big vfs lock protects lookup and walk operations
//TODO: finer grained locking
extern mtx_t vfs_lock;

typedef struct vfs_iostate {
    union {
        mx_device_t* dev;
        vnode_t* vn;
    };
    vdircookie_t dircookie;
    size_t io_off;
    uint32_t io_flags;

    list_node_t node;
    const char* fn;
} vfs_iostate_t;

void track_vfs_iostate(vfs_iostate_t* ios, const char* fn);
void untrack_vfs_iostate(vfs_iostate_t* ios);

vfs_iostate_t* create_vfs_iostate(mx_device_t* dev);
