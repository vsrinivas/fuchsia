// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/compiler.h>
#include <driver/driver-api.h>
#include <ddk/device.h>
#include <ddk/driver.h>

static driver_api_t* API;

__EXPORT void driver_api_init(driver_api_t* api) {
    if (API == NULL) {
        API = api;
    }
}

__EXPORT void driver_unbind(mx_driver_t* drv, mx_device_t* dev) {
    API->driver_unbind(drv, dev);
}

__EXPORT mx_status_t device_add2(mx_device_t* parent, device_add_args_t* args, mx_device_t** out) {
    mx_device_t* dev;

    if (args->version != DEVICE_ADD_ARGS_VERSION) {
        return ERR_INVALID_ARGS;
    }
    if (args->flags & ~(DEVICE_ADD_NON_BINDABLE | DEVICE_ADD_INSTANCE | DEVICE_ADD_BUSDEV)) {
        return ERR_INVALID_ARGS;
    }
    if ((args->flags & DEVICE_ADD_INSTANCE) && (args->flags & DEVICE_ADD_BUSDEV)) {
        return ERR_INVALID_ARGS;
    }

    mx_status_t status = API->device_create(args->name, args->ctx, args->ops, args->driver, &dev);
    if (status != NO_ERROR) {
        return status;
    }
    if (args->proto_id) {
        API->device_set_protocol(dev, args->proto_id, args->proto_ops);
    }
    if (args->flags & DEVICE_ADD_NON_BINDABLE) {
        API->device_set_bindable(dev, false);
    }

    if (args->flags & DEVICE_ADD_BUSDEV) {
        status = API->device_add(dev, parent, args->props, args->prop_count, args->busdev_args,
                                 args->rsrc);
    } else if (args->flags & DEVICE_ADD_INSTANCE) {
        status = API->device_add_instance(dev, parent);
    } else {
        status = API->device_add(dev, parent, args->props, args->prop_count, NULL, 0);
    }
    if (status != NO_ERROR) {
        return status;
    }

    *out = dev;
    return NO_ERROR;

fail:
    API->device_destroy(dev);
    dev = NULL;
    return status;
}

__EXPORT mx_status_t device_create(const char* name, void* ctx,
                                   mx_protocol_device_t* ops, mx_driver_t* driver,
                                   mx_device_t** out) {
    return API->device_create(name, ctx, ops, driver, out);
}

__EXPORT void device_set_protocol(mx_device_t* dev, uint32_t proto_id, void* proto_ops) {
    API->device_set_protocol(dev, proto_id, proto_ops);
}

__EXPORT mx_status_t device_add_busdev(mx_device_t* dev, mx_device_t* parent,
                                       mx_device_prop_t* props, uint32_t prop_count,
                                       const char* args, mx_handle_t rsrc) {
    return API->device_add(dev, parent, props, prop_count, args, rsrc);
}

__EXPORT mx_status_t device_add(mx_device_t* dev, mx_device_t* parent) {
    return API->device_add(dev, parent, NULL, 0, NULL, 0);
}

__EXPORT mx_status_t device_add_with_props(mx_device_t* dev, mx_device_t* parent,
                                           mx_device_prop_t* props, uint32_t prop_count) {
    return API->device_add(dev, parent, props, prop_count, NULL, 0);
}

__EXPORT mx_status_t device_add_instance(mx_device_t* dev, mx_device_t* parent) {
    return API->device_add_instance(dev, parent);
}

__EXPORT mx_status_t device_remove(mx_device_t* dev) {
    return API->device_remove(dev);
}

__EXPORT mx_status_t device_rebind(mx_device_t* dev) {
    return API->device_rebind(dev);
}

__EXPORT void device_destroy(mx_device_t* dev) {
// Do nothing - this is done by devmgr automatically now
}

__EXPORT void device_set_bindable(mx_device_t* dev, bool bindable) {
    return API->device_set_bindable(dev, bindable);
}


__EXPORT mx_handle_t get_root_resource(void) {
    return API->get_root_resource();
}

__EXPORT mx_status_t load_firmware(mx_driver_t* drv, const char* path,
                                   mx_handle_t* fw, size_t* size) {
    return API->load_firmware(drv, path, fw, size);
}
