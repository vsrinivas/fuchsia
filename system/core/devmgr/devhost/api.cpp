// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/compiler.h>

#include <ddk/debug.h>
#include <ddk/device.h>
#include "devhost.h"

#include <stdarg.h>
#include <stdio.h>

#include <utility>

using namespace devmgr;

// These are the API entry-points from drivers
// They must take the devhost_api_lock before calling devhost_* internals
//
// Driver code MUST NOT directly call devhost_* APIs


// LibDriver Device Interface

#define ALLOWED_FLAGS (\
    DEVICE_ADD_NON_BINDABLE | DEVICE_ADD_INSTANCE |\
    DEVICE_ADD_MUST_ISOLATE | DEVICE_ADD_INVISIBLE)

__EXPORT zx_status_t device_add_from_driver(zx_driver_t* drv, zx_device_t* parent,
                                            device_add_args_t* args, zx_device_t** out) {
    zx_status_t r;
    fbl::RefPtr<zx_device_t> dev;

    if (!parent) {
        return ZX_ERR_INVALID_ARGS;
    }

    fbl::RefPtr<zx_device> parent_ref(parent);

    if (!args || args->version != DEVICE_ADD_ARGS_VERSION) {
        return ZX_ERR_INVALID_ARGS;
    }
    if (!args->ops || args->ops->version != DEVICE_OPS_VERSION) {
        return ZX_ERR_INVALID_ARGS;
    }
    if (args->flags & ~ALLOWED_FLAGS) {
        return ZX_ERR_INVALID_ARGS;
    }
    if ((args->flags & DEVICE_ADD_INSTANCE) &&
        (args->flags & (DEVICE_ADD_MUST_ISOLATE | DEVICE_ADD_INVISIBLE))) {
        return ZX_ERR_INVALID_ARGS;
    }

    DM_LOCK();
    r = devhost_device_create(drv, parent_ref, args->name, args->ctx, args->ops, &dev);
    if (r != ZX_OK) {
        DM_UNLOCK();
        return r;
    }
    if (args->proto_id) {
        dev->protocol_id = args->proto_id;
        dev->protocol_ops = args->proto_ops;
    }
    if (args->flags & DEVICE_ADD_NON_BINDABLE) {
        dev->flags |= DEV_FLAG_UNBINDABLE;
    }
    if (args->flags & DEVICE_ADD_INVISIBLE) {
        dev->flags |= DEV_FLAG_INVISIBLE;
    }

    // out must be set before calling devhost_device_add().
    // devhost_device_add() may result in child devices being created before it returns,
    // and those children may call ops on the device before device_add() returns.
    // This leaked-ref will be accounted below.
    if (out) {
        *out = dev.get();
    }

    if (args->flags & DEVICE_ADD_MUST_ISOLATE) {
        r = devhost_device_add(dev, parent_ref, args->props, args->prop_count, args->proxy_args);
    } else if (args->flags & DEVICE_ADD_INSTANCE) {
        dev->flags |= DEV_FLAG_INSTANCE | DEV_FLAG_UNBINDABLE;
        r = devhost_device_add(dev, parent_ref, nullptr, 0, nullptr);
    } else {
        r = devhost_device_add(dev, parent_ref, args->props, args->prop_count, nullptr);
    }
    if (r != ZX_OK) {
        if (out) {
            *out = nullptr;
        }
        dev.reset();
    }

    // Leak the reference that was written to |out|, it will be recovered in
    // device_remove().
    __UNUSED auto ptr = dev.leak_ref();

    DM_UNLOCK();
    return r;
}

__EXPORT zx_status_t device_remove(zx_device_t* dev) {
    zx_status_t r;
    DM_LOCK();
    // This recovers the leaked reference that happened in
    // device_add_from_driver() above.
    auto dev_ref = fbl::internal::MakeRefPtrNoAdopt(dev);
    r = devhost_device_remove(std::move(dev_ref));
    DM_UNLOCK();
    return r;
}

__EXPORT zx_status_t device_rebind(zx_device_t* dev) {
    zx_status_t r;
    DM_LOCK();
    fbl::RefPtr<zx_device_t> dev_ref(dev);
    r = devhost_device_rebind(dev_ref);
    DM_UNLOCK();
    return r;
}

__EXPORT void device_make_visible(zx_device_t* dev) {
    DM_LOCK();
    fbl::RefPtr<zx_device_t> dev_ref(dev);
    devhost_make_visible(dev_ref);
    DM_UNLOCK();
}


__EXPORT const char* device_get_name(zx_device_t* dev) {
    return dev->name;
}

__EXPORT zx_device_t* device_get_parent(zx_device_t* dev) {
    // The caller should not hold on to this past the lifetime of |dev|.
    return dev->parent.get();
}

struct GenericProtocol {
    void* ops;
    void* ctx;
};

__EXPORT zx_status_t device_get_protocol(const zx_device_t* dev, uint32_t proto_id, void* out) {
    auto proto = static_cast<GenericProtocol*>(out);
    if (dev->ops->get_protocol) {
        return dev->ops->get_protocol(dev->ctx, proto_id, out);
    }
    if ((proto_id == dev->protocol_id) && (dev->protocol_ops != nullptr)) {
        proto->ops = dev->protocol_ops;
        proto->ctx = dev->ctx;
        return ZX_OK;
    }
    return ZX_ERR_NOT_SUPPORTED;
}

__EXPORT void device_state_clr_set(zx_device_t* dev, zx_signals_t clearflag, zx_signals_t setflag) {
    dev->event.signal(clearflag, setflag);
}


__EXPORT zx_off_t device_get_size(zx_device_t* dev) {
    return dev->ops->get_size(dev->ctx);
}

__EXPORT zx_status_t device_read(zx_device_t* dev, void* buf, size_t count,
                                 zx_off_t off, size_t* actual) {
    return dev->ops->read(dev->ctx, buf, count, off, actual);
}

__EXPORT zx_status_t device_write(zx_device_t* dev, const void* buf, size_t count,
                                  zx_off_t off, size_t* actual) {
    return dev->ops->write(dev->ctx, buf, count, off, actual);
}

__EXPORT zx_status_t device_ioctl(zx_device_t* dev, uint32_t op,
                                  const void* in_buf, size_t in_len,
                                  void* out_buf, size_t out_len,
                                  size_t* out_actual) {
    return dev->ops->ioctl(dev->ctx, op, in_buf, in_len, out_buf, out_len, out_actual);
}

// LibDriver Misc Interfaces

namespace devmgr {
extern zx_handle_t root_resource_handle;
} // namespace devmgr

__EXPORT zx_handle_t get_root_resource() {
    return root_resource_handle;
}

__EXPORT zx_status_t load_firmware(zx_device_t* dev, const char* path,
                                   zx_handle_t* fw, size_t* size) {
    zx_status_t r;
    DM_LOCK();
    fbl::RefPtr<zx_device_t> dev_ref(dev);
    r = devhost_load_firmware(dev_ref, path, fw, size);
    DM_UNLOCK();
    return r;
}

// Interface Used by DevHost RPC Layer

zx_status_t device_bind(const fbl::RefPtr<zx_device_t>& dev, const char* drv_libname) {
    zx_status_t r;
    DM_LOCK();
    r = devhost_device_bind(dev, drv_libname);
    DM_UNLOCK();
    return r;
}

zx_status_t device_unbind(const fbl::RefPtr<zx_device_t>& dev) {
    DM_LOCK();
    zx_status_t r = devhost_device_unbind(dev);
    DM_UNLOCK();
    return r;
}

zx_status_t device_open_at(const fbl::RefPtr<zx_device_t>& dev, fbl::RefPtr<zx_device_t>* out,
                           const char* path, uint32_t flags) {
    zx_status_t r;
    DM_LOCK();
    r = devhost_device_open_at(dev, out, path, flags);
    DM_UNLOCK();
    return r;
}

// This function is intended to consume the reference produced by
// device_open_at()
zx_status_t device_close(fbl::RefPtr<zx_device_t> dev, uint32_t flags) {
    zx_status_t r;
    DM_LOCK();
    r = devhost_device_close(std::move(dev), flags);
    DM_UNLOCK();
    return r;
}

__EXPORT zx_status_t device_get_metadata(zx_device_t* dev, uint32_t type,
                                         void* buf, size_t buflen, size_t* actual) {
    zx_status_t r;
    DM_LOCK();
    auto dev_ref = fbl::WrapRefPtr(dev);
    r = devhost_get_metadata(dev_ref, type, buf, buflen, actual);
    DM_UNLOCK();
    return r;
}

__EXPORT zx_status_t device_add_metadata(zx_device_t* dev, uint32_t type,
                                         const void* data, size_t length) {
    zx_status_t r;
    DM_LOCK();
    auto dev_ref = fbl::WrapRefPtr(dev);
    r = devhost_add_metadata(dev_ref, type, data, length);
    DM_UNLOCK();
    return r;
}

__EXPORT zx_status_t device_publish_metadata(zx_device_t* dev, const char* path,
                                             uint32_t type, const void* data, size_t length) {
    zx_status_t r;
    DM_LOCK();
    auto dev_ref = fbl::WrapRefPtr(dev);
    r = devhost_publish_metadata(dev_ref, path, type, data, length);
    DM_UNLOCK();
    return r;
}
