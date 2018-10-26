// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "devhost.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <threads.h>
#include <unistd.h>

#include <ddk/device.h>
#include <ddk/driver.h>

#include <zircon/assert.h>
#include <zircon/listnode.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <lib/fdio/remoteio.h>
#include <zxcpp/new.h>

namespace devmgr {

#define TRACE 0

#if TRACE
#define xprintf(fmt...) printf(fmt)
#else
#define xprintf(fmt...) \
    do {                \
    } while (0)
#endif

#define TRACE_ADD_REMOVE 0

namespace internal {
mtx_t devhost_api_lock = MTX_INIT;
} // namespace internal

static thread_local CreationContext* creation_ctx;

// The creation context is setup before the bind() or create() ops are
// invoked to provide the ability to sanity check the required device_add()
// operations these hooks should be making.
void devhost_set_creation_context(CreationContext* ctx) {
    creation_ctx = ctx;
}

static zx_status_t default_open(void* ctx, zx_device_t** out, uint32_t flags) {
    return ZX_OK;
}

static zx_status_t default_open_at(void* ctx, zx_device_t** out, const char* path, uint32_t flags) {
    return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t default_close(void* ctx, uint32_t flags) {
    return ZX_OK;
}

static void default_unbind(void* ctx) {
}

static void default_release(void* ctx) {
}

static zx_status_t default_read(void* ctx, void* buf, size_t count, zx_off_t off, size_t* actual) {
    return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t default_write(void* ctx, const void* buf, size_t count, zx_off_t off, size_t* actual) {
    return ZX_ERR_NOT_SUPPORTED;
}

static zx_off_t default_get_size(void* ctx) {
    return 0;
}

static zx_status_t default_ioctl(void* ctx, uint32_t op,
                             const void* in_buf, size_t in_len,
                             void* out_buf, size_t out_len, size_t* out_actual) {
    return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t default_suspend(void* ctx, uint32_t flags) {
    return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t default_resume(void* ctx, uint32_t flags) {
    return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t default_rxrpc(void* ctx, zx_handle_t channel) {
    return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t default_message(void *ctx, fidl_msg_t* msg, fidl_txn_t* txn) {
    fidl_message_header_t* hdr = (fidl_message_header_t*) msg->bytes;
    printf("devhost: Unsupported FIDL operation: 0x%x\n", hdr->ordinal);
    zx_handle_close_many(msg->handles, msg->num_handles);
    return ZX_ERR_NOT_SUPPORTED;
}

zx_protocol_device_t device_default_ops = []() {
    zx_protocol_device_t ops = {};
    ops.open = default_open;
    ops.open_at = default_open_at;
    ops.close = default_close;
    ops.unbind = default_unbind;
    ops.release = default_release;
    ops.read = default_read;
    ops.write = default_write;
    ops.get_size = default_get_size;
    ops.ioctl = default_ioctl;
    ops.suspend = default_suspend;
    ops.resume = default_resume;
    ops.rxrpc = default_rxrpc;
    ops.message = default_message;
    return ops;
}();

[[noreturn]]
static void device_invalid_fatal(void* ctx) {
    printf("devhost: FATAL: zx_device_t used after destruction.\n");
    __builtin_trap();
}

static zx_protocol_device_t device_invalid_ops = []() {
    zx_protocol_device_t ops = {};
    ops.open = +[](void* ctx, zx_device_t**, uint32_t) -> zx_status_t {
        device_invalid_fatal(ctx);
    };
    ops.open_at = +[](void* ctx, zx_device_t**, const char*, uint32_t) -> zx_status_t {
        device_invalid_fatal(ctx);
    };
    ops.close = +[](void* ctx, uint32_t) -> zx_status_t {
        device_invalid_fatal(ctx);
    };
    ops.unbind = +[](void* ctx) {
        device_invalid_fatal(ctx);
    };
    ops.release = +[](void* ctx) {
        device_invalid_fatal(ctx);
    };
    ops.read = +[](void* ctx, void*, size_t, size_t, size_t*) -> zx_status_t {
        device_invalid_fatal(ctx);
    };
    ops.write = +[](void* ctx, const void*, size_t, size_t, size_t*) -> zx_status_t {
        device_invalid_fatal(ctx);
    };
    ops.get_size = +[](void* ctx) -> zx_off_t {
        device_invalid_fatal(ctx);
    };
    ops.ioctl = +[](void* ctx, uint32_t, const void*, size_t, void*, size_t, size_t*)
        -> zx_status_t {
        device_invalid_fatal(ctx);
    };
    ops.suspend = +[](void* ctx, uint32_t) -> zx_status_t {
        device_invalid_fatal(ctx);
    };
    ops.resume = +[](void* ctx, uint32_t) -> zx_status_t {
        device_invalid_fatal(ctx);
    };
    ops.rxrpc = +[](void* ctx, zx_handle_t) -> zx_status_t {
        device_invalid_fatal(ctx);
    };
    ops.message = +[](void* ctx, fidl_msg_t*, fidl_txn_t*) -> zx_status_t {
        device_invalid_fatal(ctx);
    };
    return ops;
}();

// Maximum number of dead devices to hold on the dead device list
// before we start free'ing the oldest when adding a new one.
#define DEAD_DEVICE_MAX 7

void devhost_device_destroy(zx_device_t* dev) REQ_DM_LOCK {
    static fbl::DoublyLinkedList<zx_device*, zx_device::Node> dead_list;
    static unsigned dead_count = 0;

    // ensure any ops will be fatal
    dev->ops = &device_invalid_ops;

    dev->magic = 0xdeaddeaddeaddead;

    // ensure all handles are invalid
    dev->event = 0xffffffff;
    dev->local_event = 0xffffffff;

    // ensure all pointers are invalid
    dev->ctx = 0;
    dev->driver = 0;
    dev->parent = 0;
    dev->ios = 0;
    dev->proxy_ios = 0;

    // Defer destruction to help catch use-after-free and also
    // so the compiler can't (easily) optimize away the poisoning
    // we do above.
    dead_list.push_back(dev);

    if (dead_count == DEAD_DEVICE_MAX) {
        zx_device_t* to_delete = dead_list.pop_front();
        delete to_delete;
    } else {
        dead_count++;
    }
}

// defered work list
static fbl::DoublyLinkedList<zx_device*, zx_device::DeferNode> defer_device_list USE_DM_LOCK;

static int devhost_enumerators USE_DM_LOCK = 0;

static void devhost_finalize() REQ_DM_LOCK {
    // Early exit if there's no work
    if (defer_device_list.is_empty()) {
        return;
    }

    // Otherwise we snapshot the list
    auto list = fbl::move(defer_device_list);

    // We detach all the devices from their parents list-of-children
    // while under the DM lock to avoid an enumerator starting to mutate
    // things before we're done detaching them.
    for (auto& dev : list) {
        if (dev.parent) {
            dev.parent->children.erase(dev);
        }
    }

    // Then we can get to the actual final teardown where we have
    // to drop the lock to call the callback
    zx_device* dev;
    while ((dev = list.pop_front()) != nullptr) {
        // invoke release op
        if (dev->flags & DEV_FLAG_ADDED) {
            DM_UNLOCK();
            dev->Release();
            DM_LOCK();
        }

        if (dev->parent) {
            // If the parent wants rebinding when its children are gone,
            // And the parent is not dead, And this was the last child...
            if ((dev->parent->flags & DEV_FLAG_WANTS_REBIND) &&
                (!(dev->parent->flags & DEV_FLAG_DEAD)) &&
                dev->parent->children.is_empty()) {
                // Clear the wants rebind flag and request the rebind
                dev->parent->flags &= (~DEV_FLAG_WANTS_REBIND);
                devhost_device_bind(dev->parent, "");
            }

            dev_ref_release(dev->parent);
        }

        // destroy/deallocate the device
        devhost_device_destroy(dev);
    }
}


// enum_lock_{acquire,release}() are used whenever we're iterating
// on the device tree.  When "enum locked" it is legal to add a new
// child to the end of a device's list-of-children, but it is not
// legal to remove a child.  This avoids badness when we have to
// drop the DM lock to call into device ops while enumerating.

static void enum_lock_acquire() REQ_DM_LOCK {
    devhost_enumerators++;
}

static void enum_lock_release() REQ_DM_LOCK {
    if (--devhost_enumerators == 0) {
        devhost_finalize();
    }
}

void dev_ref_release(zx_device_t* dev) REQ_DM_LOCK {
    if (dev->refcount < 1) {
        printf("device: FATAL: %p: REFCOUNT GOING NEGATIVE\n", dev);
        __builtin_trap();
    }
    dev->refcount--;
    if (dev->refcount == 0) {
        if (dev->flags & DEV_FLAG_INSTANCE) {
            // these don't get removed, so mark dead state here
            dev->flags |= DEV_FLAG_DEAD | DEV_FLAG_VERY_DEAD;
        }
        if (dev->flags & DEV_FLAG_BUSY) {
            // this can happen if creation fails
            // the caller to device_add() will free it
            printf("device: %p(%s): ref=0, busy, not releasing\n", dev, dev->name);
            return;
        }
#if TRACE_ADD_REMOVE
        printf("device: %p(%s): ref=0. releasing.\n", dev, dev->name);
#endif

        if (!(dev->flags & DEV_FLAG_VERY_DEAD)) {
            printf("device: %p(%s): only mostly dead (this is bad)\n", dev, dev->name);
        }
        if (!dev->children.is_empty()) {
            printf("device: %p(%s): still has children! not good.\n", dev, dev->name);
        }

        zx_handle_close(dev->event);
        zx_handle_close(dev->local_event);

        // Put on the defered work list for finalization
        defer_device_list.push_back(dev);

        // Immediately finalize if there's not an active enumerator
        if (devhost_enumerators == 0) {
            devhost_finalize();
        }
    }
}

zx_status_t devhost_device_create(zx_driver_t* drv, zx_device_t* parent,
                                  const char* name, void* ctx,
                                  zx_protocol_device_t* ops, zx_device_t** out)
                                  REQ_DM_LOCK {

    if (!drv) {
        printf("devhost: _device_add could not find driver!\n");
        return ZX_ERR_INVALID_ARGS;
    }

    auto dev = fbl::make_unique<zx_device_t>();
    if (dev == nullptr) {
        return ZX_ERR_NO_MEMORY;
    }

    dev->ops = ops;
    dev->driver = drv;

    if (name == nullptr) {
        printf("devhost: dev=%p has null name.\n", dev.get());
        name = "invalid";
        dev->magic = 0;
    }

    size_t len = strlen(name);
    // TODO(teisenbe): I think this is overly aggresive, and could be changed
    // to |len > ZX_DEVICE_NAME_MAX| and |len = ZX_DEVICE_NAME_MAX|.
    if (len >= ZX_DEVICE_NAME_MAX) {
        printf("devhost: dev=%p name too large '%s'\n", dev.get(), name);
        len = ZX_DEVICE_NAME_MAX - 1;
        dev->magic = 0;
    }

    memcpy(dev->name, name, len);
    dev->name[len] = 0;
    // TODO(teisenbe): Why do we default to dev.get() here?  Why not just
    // nullptr
    dev->ctx = ctx ? ctx : dev.get();
    *out = dev.release();
    return ZX_OK;
}

#define DEFAULT_IF_NULL(ops,method) \
    if (ops->method == nullptr) { \
        ops->method = default_##method; \
    }

static zx_status_t device_validate(zx_device_t* dev) REQ_DM_LOCK {
    if (dev == nullptr) {
        printf("INVAL: nullptr!\n");
        return ZX_ERR_INVALID_ARGS;
    }
    if (dev->flags & DEV_FLAG_ADDED) {
        printf("device already added: %p(%s)\n", dev, dev->name);
        return ZX_ERR_BAD_STATE;
    }
    if (dev->magic != DEV_MAGIC) {
        return ZX_ERR_BAD_STATE;
    }
    if (dev->ops == nullptr) {
        printf("device add: %p(%s): nullptr ops\n", dev, dev->name);
        return ZX_ERR_INVALID_ARGS;
    }
    if ((dev->protocol_id == ZX_PROTOCOL_MISC_PARENT) ||
        (dev->protocol_id == ZX_PROTOCOL_ROOT)) {
        // These protocols is only allowed for the special
        // singleton misc or root parent devices.
        return ZX_ERR_INVALID_ARGS;
    }
    // devices which do not declare a primary protocol
    // are implied to be misc devices
    if (dev->protocol_id == 0) {
        dev->protocol_id = ZX_PROTOCOL_MISC;
    }

    // install default methods if needed
    zx_protocol_device_t* ops = dev->ops;
    DEFAULT_IF_NULL(ops, open);
    DEFAULT_IF_NULL(ops, open_at);
    DEFAULT_IF_NULL(ops, close);
    DEFAULT_IF_NULL(ops, unbind);
    DEFAULT_IF_NULL(ops, release);
    DEFAULT_IF_NULL(ops, read);
    DEFAULT_IF_NULL(ops, write);
    DEFAULT_IF_NULL(ops, get_size);
    DEFAULT_IF_NULL(ops, ioctl);
    DEFAULT_IF_NULL(ops, suspend);
    DEFAULT_IF_NULL(ops, resume);
    DEFAULT_IF_NULL(ops, rxrpc);
    DEFAULT_IF_NULL(ops, message);

    return ZX_OK;
}

zx_status_t devhost_device_add(zx_device_t* dev, zx_device_t* parent,
                               const zx_device_prop_t* props, uint32_t prop_count,
                               const char* proxy_args)
                               REQ_DM_LOCK {
    auto fail = [&dev](zx_status_t status) {
        dev->flags |= DEV_FLAG_DEAD | DEV_FLAG_VERY_DEAD;
        return status;
    };

    zx_status_t status;
    if ((status = device_validate(dev)) < 0) {
        return fail(status);
    }
    if (parent == nullptr) {
        printf("device_add: cannot add %p(%s) to nullptr parent\n", dev, dev->name);
        return fail(ZX_ERR_NOT_SUPPORTED);
    }
    if (parent->flags & DEV_FLAG_DEAD) {
        printf("device add: %p: is dead, cannot add child %p\n", parent, dev);
        return fail(ZX_ERR_BAD_STATE);
    }

    CreationContext* ctx = nullptr;

    // if creation ctx (thread local) is set, we are in a thread
    // that is handling a bind() or create() callback and if that
    // ctx's parent matches the one provided to add we need to do
    // some additional checking...
    if ((creation_ctx != nullptr) && (creation_ctx->parent == parent)) {
        ctx = creation_ctx;
        if (ctx->rpc != ZX_HANDLE_INVALID) {
            // create() must create only one child
            if (ctx->child != nullptr) {
                printf("devhost: driver attempted to create multiple proxy devices!\n");
                return ZX_ERR_BAD_STATE;
            }
        }
    }

#if TRACE_ADD_REMOVE
    printf("devhost: device add: %p(%s) parent=%p(%s)\n",
            dev, dev->name, parent, parent->name);
#endif

    // Don't create an event handle if we alredy have one
    if ((dev->event == ZX_HANDLE_INVALID) &&
        ((status = zx_eventpair_create(0, &dev->event, &dev->local_event)) < 0)) {
        printf("device add: %p(%s): cannot create event: %d\n",
               dev, dev->name, status);
        return fail(status);
    }

    dev->flags |= DEV_FLAG_BUSY;

    // this is balanced by end of devhost_device_remove
    // or, for instanced devices, by the last close
    dev_ref_acquire(dev);

    // proxy devices are created through this handshake process
    if (ctx && (ctx->rpc != ZX_HANDLE_INVALID)) {
        if (dev->flags & DEV_FLAG_INVISIBLE) {
            printf("devhost: driver attempted to create invisible device in create()\n");
            return ZX_ERR_INVALID_ARGS;
        }
        dev->flags |= DEV_FLAG_ADDED;
        dev->flags &= (~DEV_FLAG_BUSY);
        dev->rpc.reset(ctx->rpc);
        ctx->child = dev;
        return ZX_OK;
    }

    dev_ref_acquire(parent);
    dev->parent = parent;

    // attach to our parent
    parent->children.push_back(dev);

    if (!(dev->flags & DEV_FLAG_INSTANCE)) {
        // devhost_add always consumes the handle
        status = devhost_add(parent, dev, proxy_args, props, prop_count);
        if (status < 0) {
            printf("devhost: %p(%s): remote add failed %d\n",
                   dev, dev->name, status);
            dev->parent->children.erase(*dev);
            dev_ref_release(dev->parent);
            dev->parent = nullptr;

            // since we are under the lock the whole time, we added the node
            // to the tail and then we peeled it back off the tail when we
            // failed, we don't need to interact with the enum lock mechanism
            dev->flags &= (~DEV_FLAG_BUSY);
            dev_ref_release(dev);
            return status;
        }
    }
    dev->flags |= DEV_FLAG_ADDED;
    dev->flags &= (~DEV_FLAG_BUSY);

    // record this device in the creation context if there is one
    if (ctx && (ctx->child == nullptr)) {
        ctx->child = dev;
    }
    return ZX_OK;
}

#define REMOVAL_BAD_FLAGS \
    (DEV_FLAG_DEAD | DEV_FLAG_BUSY |\
     DEV_FLAG_INSTANCE | DEV_FLAG_MULTI_BIND)

static const char* removal_problem(uint32_t flags) {
    if (flags & DEV_FLAG_DEAD) {
        return "already dead";
    }
    if (flags & DEV_FLAG_BUSY) {
        return "being created";
    }
    if (flags & DEV_FLAG_INSTANCE) {
        return "ephemeral device";
    }
    if (flags & DEV_FLAG_MULTI_BIND) {
        return "multi-bind-able device";
    }
    return "?";
}

static void devhost_unbind_children(zx_device_t* dev) REQ_DM_LOCK {
#if TRACE_ADD_REMOVE
    printf("devhost_unbind_children: %p(%s)\n", dev, dev->name);
#endif
    enum_lock_acquire();
    for (auto& child : dev->children) {
        if (!(child.flags & DEV_FLAG_DEAD)) {
            devhost_device_unbind(&child);
        }
    }
    enum_lock_release();
}

zx_status_t devhost_device_remove(zx_device_t* dev) REQ_DM_LOCK {
    if (dev->flags & REMOVAL_BAD_FLAGS) {
        printf("device: %p(%s): cannot be removed (%s)\n",
               dev, dev->name, removal_problem(dev->flags));
        return ZX_ERR_INVALID_ARGS;
    }
#if TRACE_ADD_REMOVE
    printf("device: %p(%s): is being removed\n", dev, dev->name);
#endif
    dev->flags |= DEV_FLAG_DEAD;

    devhost_unbind_children(dev);

    // cause the vfs entry to be unpublished to avoid further open() attempts
    xprintf("device: %p: devhost->devmgr remove rpc\n", dev);
    devhost_remove(dev);

    dev->flags |= DEV_FLAG_VERY_DEAD;

    // this must be last, since it may result in the device structure being destroyed
    dev_ref_release(dev);

    return ZX_OK;
}

zx_status_t devhost_device_rebind(zx_device_t* dev) REQ_DM_LOCK {
    // note that we want to be rebound when our children are all gone
    dev->flags |= DEV_FLAG_WANTS_REBIND;

    // request that any existing children go away
    devhost_unbind_children(dev);

    return ZX_OK;
}

zx_status_t devhost_device_unbind(zx_device_t* dev) REQ_DM_LOCK {
    if (!(dev->flags & DEV_FLAG_UNBOUND)) {
        dev->flags |= DEV_FLAG_UNBOUND;
        // Call dev's unbind op.
        if (dev->ops->unbind) {
#if TRACE_ADD_REMOVE
            printf("call unbind dev: %p(%s)\n", dev, dev->name);
#endif
            // Hold a reference so the dev won't get released during its unbind callback.
            dev_ref_acquire(dev);
            DM_UNLOCK();
            dev->Unbind();
            DM_LOCK();
            dev_ref_release(dev);
        }
    }
    return ZX_OK;
}

zx_status_t devhost_device_open_at(zx_device_t* dev, zx_device_t** out,
                                   const char* path, uint32_t flags)
                                   REQ_DM_LOCK {
    if (dev->flags & DEV_FLAG_DEAD) {
        printf("device open: %p(%s) is dead!\n", dev, dev->name);
        return ZX_ERR_BAD_STATE;
    }
    dev_ref_acquire(dev);
    zx_status_t r;
    DM_UNLOCK();
    *out = dev;
    if (path) {
        r = dev->OpenAt(out, path, flags);
    } else {
        r = dev->Open(out, flags);
    }
    DM_LOCK();
    if (r < 0) {
        dev_ref_release(dev);
    } else if (*out != dev) {
        // open created a per-instance device for us
        dev_ref_release(dev);

        dev = *out;
        if (!(dev->flags & DEV_FLAG_INSTANCE)) {
            printf("device open: %p(%s) in bad state %x\n", dev, dev->name, flags);
            panic();
        }
    }
    return r;
}

zx_status_t devhost_device_close(zx_device_t* dev, uint32_t flags) REQ_DM_LOCK {
    DM_UNLOCK();
    zx_status_t r = dev->Close(flags);
    DM_LOCK();
    dev_ref_release(dev);
    return r;
}

static zx_status_t _devhost_device_suspend(zx_device_t* dev, uint32_t flags) REQ_DM_LOCK {
    // first suspend children (so we suspend from leaf up)
    zx_status_t st;
    for (auto& child : dev->children) {
        if (!(child.flags & DEV_FLAG_DEAD)) {
            st = devhost_device_suspend(&child, flags);
            if (st != ZX_OK) {
                return st;
            }
        }
    }

    // then invoke our suspend hook
    DM_UNLOCK();
    st = dev->ops->suspend(dev->ctx, flags);
    DM_LOCK();

    // default_suspend() returns ZX_ERR_NOT_SUPPORTED
    if ((st != ZX_OK) && (st != ZX_ERR_NOT_SUPPORTED)) {
        return st;
    } else {
        return ZX_OK;
    }
}

zx_status_t devhost_device_suspend(zx_device_t* dev, uint32_t flags) REQ_DM_LOCK {
    //TODO this should eventually be two-pass using SUSPENDING/SUSPENDED flags
    enum_lock_acquire();
    zx_status_t r = _devhost_device_suspend(dev, flags);
    enum_lock_release();
    return r;
}

} // namespace devmgr
