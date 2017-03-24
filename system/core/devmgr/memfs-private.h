// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <fs/vfs.h>
#include <magenta/compiler.h>
#include <magenta/listnode.h>
#include <magenta/types.h>
#include <mxio/remoteio.h>
#include <mxio/vfs.h>
#include <threads.h>

#define MEMFS_TYPE_DATA   0
#define MEMFS_TYPE_DIR    1
#define MEMFS_TYPE_VMO    2
#define MEMFS_TYPE_DEVICE 3
#define MEMFS_TYPE_MASK 0x3

#ifdef __cplusplus

namespace memfs {

typedef struct dnode dnode_t;

class VnodeMemfs : public fs::Vnode {
public:
    mx_status_t IoctlWatchDir(const void* in_buf, size_t in_len, void* out_buf,
                              size_t out_len) override;
    void NotifyAdd(const char* name, size_t len) override;
    virtual mx_status_t GetHandles(uint32_t flags, mx_handle_t* hnds,
                                   uint32_t* type, void* extra, uint32_t* esize) override;
    virtual void Release() override;
    virtual mx_status_t Open(uint32_t flags) override;
    virtual mx_status_t Close() override;
    virtual mx_status_t Setattr(vnattr_t* a) override;
    virtual mx_status_t Sync() override;
    ssize_t Ioctl(uint32_t op, const void* in_buf,
                  size_t in_len, void* out_buf, size_t out_len) final;
    mx_status_t AttachRemote(mx_handle_t h) final;

    // To be more specific: Is this vnode connected into the directory hierarchy?
    // VnodeDirs can be unlinked, and this method will subsequently return false.
    bool IsDirectory() const { return dnode_ != nullptr; }

    virtual ~VnodeMemfs();

    // TODO(smklein): The following members should become private
    uint32_t seqcount_;

    dnode_t* dnode_;      // list of my children
    list_node_t dn_list_; // all dnodes that point at this vnode
    uint32_t link_count_;

protected:
    VnodeMemfs();

    list_node_t watch_list_; // all directory watchers
    uint64_t create_time_;
    uint64_t modify_time_;
};

class VnodeFile final : public VnodeMemfs {
public:
    VnodeFile();
    ~VnodeFile();

private:
    void Release() final;
    ssize_t Read(void* data, size_t len, size_t off) final;
    ssize_t Write(const void* data, size_t len, size_t off) final;
    mx_status_t Truncate(size_t len) final;
    mx_status_t Getattr(vnattr_t* a) final;

    mx_handle_t vmo_;
    mx_off_t length_;
};

class VnodeDir final : public VnodeMemfs {
public:
    VnodeDir();
    ~VnodeDir();

private:
    mx_status_t Lookup(fs::Vnode** out, const char* name, size_t len) final;
    mx_status_t Readdir(void* cookie, void* dirents, size_t len) final;
    mx_status_t Create(fs::Vnode** out, const char* name, size_t len, uint32_t mode) final;
    mx_status_t Unlink(const char* name, size_t len, bool must_be_dir) final;
    mx_status_t Rename(fs::Vnode* newdir,
                       const char* oldname, size_t oldlen,
                       const char* newname, size_t newlen,
                       bool src_must_be_dir, bool dst_must_be_dir) final;
    mx_status_t Link(const char* name, size_t len, fs::Vnode* target) final;
    mx_status_t Getattr(vnattr_t* a) final;
};

class VnodeVmo final : public VnodeMemfs {
public:
    VnodeVmo();
    ~VnodeVmo();

    // TODO(smklein): This 'initializer' function only exists as a hack
    // due to our implementation of "_memfs_create".
    // We should improve our construction of VnodeVmos, and remove this
    // function.
    void Init(mx_handle_t vmo, mx_off_t length, mx_off_t offset) {
        vmo_ = vmo;
        length_ = length;
        offset_ = offset;
    }

private:
    ssize_t Read(void* data, size_t len, size_t off) final;
    ssize_t Write(const void* data, size_t len, size_t off) final;
    mx_status_t Getattr(vnattr_t* a) final;
    mx_status_t GetHandles(uint32_t flags, mx_handle_t* hnds,
                           uint32_t* type, void* extra, uint32_t* esize) final;

    mx_handle_t vmo_;
    mx_off_t length_;
    mx_off_t offset_;
};

class VnodeDevice final : public VnodeMemfs {
public:
    VnodeDevice();
    ~VnodeDevice();

private:
    void Release() final;
    mx_status_t Lookup(fs::Vnode** out, const char* name, size_t len) final;
    mx_status_t Getattr(vnattr_t* a) final;
    mx_status_t Readdir(void* cookie, void* dirents, size_t len) final;
    mx_status_t GetHandles(uint32_t flags, mx_handle_t* hnds,
                           uint32_t* type, void* extra, uint32_t* esize) final;
};

} // namespace memfs

using VnodeMemfs = memfs::VnodeMemfs;

#else

typedef struct VnodeMemfs VnodeMemfs;

#endif  // ifdef __cplusplus

__BEGIN_CDECLS

typedef struct vnode_watcher {
    list_node_t node;
    mx_handle_t h;
} vnode_watcher_t;

void vfs_global_init(VnodeMemfs* root);

// generate mxremoteio handles
mx_handle_t vfs_create_global_root_handle(void);
mx_handle_t vfs_create_root_handle(VnodeMemfs* vn);

// device fs
VnodeMemfs* devfs_get_root(void);
mx_status_t memfs_create_device_at(VnodeMemfs* parent, VnodeMemfs** out, const char* name, mx_handle_t hdevice);
mx_status_t devfs_remove(VnodeMemfs* vn);

// boot fs
VnodeMemfs* bootfs_get_root(void);
mx_status_t bootfs_add_file(const char* path, mx_handle_t vmo, mx_off_t off, size_t len);

// system fs
VnodeMemfs* systemfs_get_root(void);
mx_status_t systemfs_add_file(const char* path, mx_handle_t vmo, mx_off_t off, size_t len);

// memory fs
VnodeMemfs* memfs_get_root(void);
mx_status_t memfs_add_link(VnodeMemfs* parent, const char* name, VnodeMemfs* target);

// TODO(orr) normally static; temporary exposure, to be undone in subsequent patch
mx_status_t _memfs_create(VnodeMemfs* parent, VnodeMemfs** out,
                          const char* name, size_t namelen,
                          uint32_t type);

// Create the global root to memfs
VnodeMemfs* vfs_create_global_root(void);

// Create a generic root to memfs
VnodeMemfs* vfs_create_root(void);

// shared among all memory filesystems
mx_status_t memfs_lookup_name(const VnodeMemfs* vn, char* outname, size_t out_len);

mx_status_t memfs_create_from_buffer(const char* path, uint32_t flags,
                                     const char* ptr, mx_off_t len);
mx_status_t memfs_create_directory(const char* path, uint32_t flags);
mx_status_t memfs_create_from_vmo(const char* path, uint32_t flags,
                                  mx_handle_t vmo, mx_off_t off, mx_off_t len);

// big vfs lock protects lookup and walk operations
//TODO: finer grained locking
extern mtx_t vfs_lock;

void memfs_mount(VnodeMemfs* parent, VnodeMemfs* subtree);

__END_CDECLS
