// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "device-internal.h"
#include "devcoordinator.h"

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>

#include <fbl/intrusive_double_list.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/string.h>
#include <fbl/unique_ptr.h>

#include <lib/fdio/remoteio.h>
#include <lib/zx/channel.h>

#include <zircon/compiler.h>
#include <zircon/fidl.h>
#include <zircon/thread_annotations.h>
#include <zircon/types.h>

#include <threads.h>
#include <stdint.h>

// Handle IDs for USER0 handles
#define ID_HJOBROOT 4

// Nothing outside of devmgr/{devmgr,devhost,rpc-device}.c
// should be calling devhost_*() APIs, as this could
// violate the internal locking design.

// Safe external APIs are in device.h and device_internal.h

// Note that this must be a struct to match the public opaque declaration.
struct zx_driver : fbl::DoublyLinkedListable<fbl::RefPtr<zx_driver>>,
                   fbl::RefCounted<zx_driver> {
    static zx_status_t Create(fbl::RefPtr<zx_driver>* out_driver);

    const char* name() const {
        return name_;
    }

    zx_driver_rec_t* driver_rec() const {
        return driver_rec_;
    }

    zx_status_t status() const {
        return status_;
    }

    const fbl::String& libname() const {
        return libname_;
    }

    void set_name(const char* name) {
        name_ = name;
    }

    void set_driver_rec(zx_driver_rec_t* driver_rec) {
        driver_rec_ = driver_rec;
    }

    void set_ops(const zx_driver_ops_t* ops) {
        ops_ = ops;
    }

    void set_status(zx_status_t status) {
        status_ = status;
    }

    void set_libname(fbl::StringPiece libname) {
        libname_ = libname;
    }

    // Interface to |ops|. These names contain Op in order to not
    // collide with e.g. RefPtr names.

    bool has_init_op() const {
        return ops_->init != nullptr;
    }

    bool has_bind_op() const {
        return ops_->bind != nullptr;
    }

    bool has_create_op() const {
        return ops_->create != nullptr;
    }

    zx_status_t InitOp() {
        return ops_->init(&ctx_);
    }

    zx_status_t BindOp(zx_device_t* device) const {
        return ops_->bind(ctx_, device);
    }

    zx_status_t CreateOp(zx_device_t* parent, const char* name, const char* args,
                         zx_handle_t rpc_channel) const {
        return ops_->create(ctx_, parent, name, args, rpc_channel);
    }

    void ReleaseOp() const {
        // TODO(kulakowski/teisenbe) Consider poisoning the ops_ table on release.
        ops_->release(ctx_);
    }

private:
    friend fbl::unique_ptr<zx_driver> fbl::make_unique<zx_driver>();
    zx_driver() = default;

    const char* name_ = nullptr;
    zx_driver_rec_t* driver_rec_ = nullptr;
    const zx_driver_ops_t* ops_ = nullptr;
    void* ctx_ = nullptr;
    fbl::String libname_;
    zx_status_t status_ = ZX_OK;
};

namespace devmgr {

extern zx_protocol_device_t device_default_ops;

// locking and lock debugging

namespace internal {
extern mtx_t devhost_api_lock;
} // namespace internal

#define REQ_DM_LOCK TA_REQ(&internal::devhost_api_lock)
#define USE_DM_LOCK TA_GUARDED(&internal::devhost_api_lock)

static inline void DM_LOCK() TA_ACQ(&internal::devhost_api_lock) {
    mtx_lock(&internal::devhost_api_lock);
}

static inline void DM_UNLOCK() TA_REL(&internal::devhost_api_lock) {
    mtx_unlock(&internal::devhost_api_lock);
}

zx_status_t devhost_device_add(zx_device_t* dev, zx_device_t* parent,
                               const zx_device_prop_t* props, uint32_t prop_count,
                               const char* proxy_args) REQ_DM_LOCK;
zx_status_t devhost_device_remove(zx_device_t* dev) REQ_DM_LOCK;
zx_status_t devhost_device_bind(zx_device_t* dev, const char* drv_libname) REQ_DM_LOCK;
zx_status_t devhost_device_rebind(zx_device_t* dev) REQ_DM_LOCK;
zx_status_t devhost_device_unbind(zx_device_t* dev) REQ_DM_LOCK;
zx_status_t devhost_device_create(zx_driver_t* drv, zx_device_t* parent,
                                  const char* name, void* ctx,
                                  zx_protocol_device_t* ops, zx_device_t** out) REQ_DM_LOCK;
zx_status_t devhost_device_open_at(zx_device_t* dev, zx_device_t** out,
                                 const char* path, uint32_t flags) REQ_DM_LOCK;
zx_status_t devhost_device_close(zx_device_t* dev, uint32_t flags) REQ_DM_LOCK;
zx_status_t devhost_device_suspend(zx_device_t* dev, uint32_t flags) REQ_DM_LOCK;
void devhost_device_destroy(zx_device_t* dev) REQ_DM_LOCK;

zx_status_t devhost_load_firmware(zx_device_t* dev, const char* path,
                                  zx_handle_t* fw, size_t* size) REQ_DM_LOCK;

zx_status_t devhost_get_topo_path(zx_device_t* dev, char* path,
                                  size_t max, size_t* actual);

zx_status_t devhost_get_metadata(zx_device_t* dev, uint32_t type, void* buf, size_t buflen,
                                 size_t* actual);

zx_status_t devhost_add_metadata(zx_device_t* dev, uint32_t type, const void* data, size_t length);

zx_status_t devhost_publish_metadata(zx_device_t* dev, const char* path, uint32_t type,
                                     const void* data, size_t length);

// shared between devhost.c and rpc-device.c
struct DevhostIostate {
    DevhostIostate() = default;

    zx_device_t* dev = nullptr;
    size_t io_off = 0;
    uint32_t flags = 0;
    bool dead = false;
    port_handler_t ph = {};
};

zx_status_t devhost_fidl_handler(fidl_msg_t* msg, fidl_txn_t* txn, void* cookie);

zx_status_t devhost_start_iostate(fbl::unique_ptr<DevhostIostate> ios, zx::channel h);

// routines devhost uses to talk to dev coordinator
zx_status_t devhost_add(zx_device_t* dev, zx_device_t* child, const char* proxy_args,
                        const zx_device_prop_t* props, uint32_t prop_count);
zx_status_t devhost_remove(zx_device_t* dev);
void devhost_make_visible(zx_device_t* dev);


// device refcounts
void dev_ref_release(zx_device_t* dev);
static inline void dev_ref_acquire(zx_device_t* dev) {
    dev->refcount++;
}

zx_handle_t get_root_resource();

struct CreationContext {
    zx_device_t* parent;
    zx_device_t* child;
    zx_handle_t rpc;
};

void devhost_set_creation_context(CreationContext* ctx);

} // namespace devmgr
