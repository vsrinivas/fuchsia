// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/recycler.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <lib/zx/channel.h>
#include <zircon/compiler.h>

namespace devmgr {

struct ProxyIostate;

} // namespace devmgr

#define DEV_MAGIC 'MDEV'

// This needs to be a struct, not a class, to match the public definition
struct zx_device : fbl::RefCounted<zx_device>, fbl::Recyclable<zx_device>  {
    ~zx_device() = default;

    zx_device(const zx_device&) = delete;
    zx_device& operator=(const zx_device&) = delete;

    static zx_status_t Create(fbl::RefPtr<zx_device>* out_dev);

    zx_status_t OpenOp(zx_device_t** dev_out, uint32_t flags) {
        return ops->open(ctx, dev_out, flags);
    }

    zx_status_t OpenAtOp(zx_device_t** dev_out, const char* path, uint32_t flags) {
        return ops->open_at(ctx, dev_out, path, flags);
    }

    zx_status_t CloseOp(uint32_t flags) {
        return ops->close(ctx, flags);
    }

    void UnbindOp() {
        ops->unbind(ctx);
    }

    void ReleaseOp() {
        ops->release(ctx);
    }

    zx_status_t SuspendOp(uint32_t flags) {
        return ops->suspend(ctx, flags);
    }

    zx_status_t ResumeOp(uint32_t flags) {
        return ops->resume(ctx, flags);
    }

    zx_status_t ReadOp(void* buf, size_t count, zx_off_t off,
                       size_t* actual) {
        return ops->read(ctx, buf, count, off, actual);
    }

    zx_status_t WriteOp(const void* buf, size_t count,
                        zx_off_t off, size_t* actual) {
        return ops->write(ctx, buf, count, off, actual);
    }

    zx_off_t GetSizeOp() {
        return ops->get_size(ctx);
    }

    zx_status_t IoctlOp(uint32_t op, const void* in_buf, size_t in_len,
                        void* out_buf, size_t out_len, size_t* out_actual) {
        return ops->ioctl(ctx, op, in_buf, in_len, out_buf, out_len, out_actual);
    }

    zx_status_t MessageOp(fidl_msg_t* msg, fidl_txn_t* txn) {
        return ops->message(ctx, msg, txn);
    }

    uintptr_t magic = DEV_MAGIC;

    zx_protocol_device_t* ops = nullptr;

    // reserved for driver use; will not be touched by devmgr
    void* ctx = nullptr;

    uint32_t flags = 0;

    zx_handle_t event = ZX_HANDLE_INVALID;
    zx_handle_t local_event = ZX_HANDLE_INVALID;
    zx::channel rpc;

    // most devices implement a single
    // protocol beyond the base device protocol
    uint32_t protocol_id = 0;
    void* protocol_ops = nullptr;

    // driver that has published this device
    zx_driver_t* driver = nullptr;

    // parent in the device tree
    fbl::RefPtr<zx_device_t> parent;

    // for the parent's device_list
    fbl::DoublyLinkedListNodeState<zx_device*> node;
    struct Node {
        static fbl::DoublyLinkedListNodeState<zx_device*>& node_state(
            zx_device& obj) {
            return obj.node;
        }
    };

    // list of this device's children in the device tree
    fbl::DoublyLinkedList<zx_device*, Node> children;

    // list node for the defer_device_list
    fbl::DoublyLinkedListNodeState<zx_device*> defer;
    struct DeferNode {
        static fbl::DoublyLinkedListNodeState<zx_device*>& node_state(
            zx_device& obj) {
            return obj.defer;
        }
    };

    // iostate
    void* ios = nullptr;
    devmgr::ProxyIostate* proxy_ios = nullptr;

    char name[ZX_DEVICE_NAME_MAX + 1] = {};
private:
    zx_device() = default;

    friend class fbl::Recyclable<zx_device_t>;
    void fbl_recycle();
};

// zx_device_t objects must be created or initialized by the driver manager's
// device_create() function.  Drivers MAY NOT touch any
// fields in the zx_device_t, except for the protocol_id and protocol_ops
// fields which it may fill out after init and before device_add() is called,
// and the ctx field which may be used to store driver-specific data.

#define DEV_FLAG_DEAD           0x00000001  // being deleted
#define DEV_FLAG_VERY_DEAD      0x00000002  // safe for ref0 and release()
#define DEV_FLAG_UNBINDABLE     0x00000004  // nobody may bind to this device
#define DEV_FLAG_BUSY           0x00000010  // device being created
#define DEV_FLAG_INSTANCE       0x00000020  // this device was created-on-open
#define DEV_FLAG_MULTI_BIND     0x00000080  // this device accepts many children
#define DEV_FLAG_ADDED          0x00000100  // device_add() has been called for this device
#define DEV_FLAG_INVISIBLE      0x00000200  // device not visible via devfs
#define DEV_FLAG_UNBOUND        0x00000400  // informed that it should self-delete asap
#define DEV_FLAG_WANTS_REBIND   0x00000800  // when last child goes, rebind this device

zx_status_t device_bind(const fbl::RefPtr<zx_device_t>& dev, const char* drv_libname);
zx_status_t device_unbind(const fbl::RefPtr<zx_device_t>& dev);
zx_status_t device_open_at(const fbl::RefPtr<zx_device_t>& dev, fbl::RefPtr<zx_device_t>* out,
                           const char* path, uint32_t flags);
// Note that device_close() is intended to consume a reference (logically, the
// one created by device_open_at).
zx_status_t device_close(fbl::RefPtr<zx_device_t> dev, uint32_t flags);
