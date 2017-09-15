// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/types.h>
#include <zircon/listnode.h>
#include <zircon/compiler.h>
#include <stdint.h>

__BEGIN_CDECLS;

typedef struct zx_device zx_device_t;
typedef struct zx_driver zx_driver_t;
typedef struct zx_protocol_device zx_protocol_device_t;
typedef struct zx_device_prop zx_device_prop_t;
typedef struct zx_driver_rec zx_driver_rec_t;

typedef struct zx_bind_inst zx_bind_inst_t;
typedef struct zx_driver_binding zx_driver_binding_t;

// echo -n "zx_driver_ops_v0.5" | sha256sum | cut -c1-16
#define DRIVER_OPS_VERSION 0x2b3490fa40d9f452

typedef struct zx_driver_ops {
    uint64_t version;   // DRIVER_OPS_VERSION

    // Opportunity to do on-load work.
    // Called ony once, before any other ops are called.
    // The driver may optionally return a context pointer to be passed
    // to the other driver ops.
    zx_status_t (*init)(void** out_ctx);

    // Requests that the driver bind to the provided device,
    // initialize it, and publish and children.
    // On success, the cookie is remembered and passed back on unbind.
    zx_status_t (*bind)(void* ctx, zx_device_t* device, void** cookie);

    // Notifies driver that the device which the driver bound to
    // is being removed.  Called after the unbind() op of any devices
    // that are children of that device.
    void (*unbind)(void* ctx, zx_device_t* device, void* cookie);

    // Only provided by bus manager drivers, create() is invoked to
    // instantiate a bus device instance in a new device host process
    zx_status_t (*create)(void* ctx, zx_device_t* parent,
                          const char* name, const char* args,
                          zx_handle_t rpc_channel);

    // Last call before driver is unloaded.
    void (*release)(void* ctx);
} zx_driver_ops_t;

// echo -n "device_add_args_v0.5" | sha256sum | cut -c1-16
#define DEVICE_ADD_ARGS_VERSION 0x96a64134d56e88e3

enum {
    DEVICE_ADD_NON_BINDABLE = (1 << 0),
    DEVICE_ADD_INSTANCE     = (1 << 1),
    DEVICE_ADD_MUST_ISOLATE = (1 << 2),
};

// Device Manager API
typedef struct device_add_args {
    uint64_t version;   // DEVICE_ADD_ARGS_VERSION
    // driver name is copied to internal structure
    // max length is ZX_DEVICE_NAME_MAX
    const char* name;
    // context pointer for use by the driver
    // and passed to driver in all zx_protocol_device_t callbacks
    void* ctx;
    // pointer to device's device protocol operations
    zx_protocol_device_t* ops;
    // optional list of device properties
    zx_device_prop_t* props;
    // number of device properties
    uint32_t prop_count;
    // optional custom protocol for this device
    uint32_t proto_id;
    // optional custom protocol operations for this device
    void* proto_ops;
    // arguments used with DEVICE_ADD_BUSDEV
    const char* busdev_args;
    // resource handle used with DEVICE_ADD_BUSDEV
     zx_handle_t rsrc;
    // one or more of DEVICE_ADD_*
    uint32_t flags;
} device_add_args_t;

struct zx_driver_rec {
    const zx_driver_ops_t* ops;
    zx_driver_t* driver;
    uint32_t log_flags;
};

// This global symbol is initialized by the driver loader in devhost
extern zx_driver_rec_t __zircon_driver_rec__;

zx_status_t device_add_from_driver(zx_driver_t* drv, zx_device_t* parent,
                              device_add_args_t* args, zx_device_t** out);

// Creates a device and adds it to the devmgr.
// device_add_args_t contains all "in" arguments.
// All device_add_args_t values are copied, so device_add_args_t can be stack allocated.
// The device_add_args_t.name string value is copied.
// All other pointer fields are copied as pointers.
// The newly added device will be active before this call returns, so be sure to have
// the "out" pointer point to your device-local structure so callbacks can access
// it immediately.
static inline zx_status_t device_add(zx_device_t* parent, device_add_args_t* args, zx_device_t** out) {
    return device_add_from_driver(__zircon_driver_rec__.driver, parent, args, out);
}

zx_status_t device_remove(zx_device_t* device);
zx_status_t device_rebind(zx_device_t* device);

void device_unbind(zx_device_t* dev);

#define ROUNDUP(a, b)   (((a) + ((b)-1)) & ~((b)-1))
#define ROUNDDOWN(a, b) ((a) & ~((b)-1))
#define ALIGN(a, b) ROUNDUP(a, b)

// temporary accessor for root resource handle
zx_handle_t get_root_resource(void);

// Drivers may need to load firmware for a device, typically during the call to
// bind the device. The devmgr will look for the firmware at the given path
// relative to system-defined locations for device firmware. The file will be
// loaded into a vmo pointed to by fw. The actual size of the firmware will be
// returned in size.
zx_status_t load_firmware(zx_device_t* device, const char* path,
                          zx_handle_t* fw, size_t* size);

// panic is for handling non-recoverable, non-reportable fatal
// errors in a way that will get logged.  Right now this just
// does a bogus write to unmapped memory.
static inline void panic(void) {
    for (;;) {
        *((int*) 0xdead) = 1;
    }
}

// Protocol Identifiers
#define DDK_PROTOCOL_DEF(tag, val, name, flags) ZX_PROTOCOL_##tag = val,
enum {
#include <ddk/protodefs.h>
};

__END_CDECLS;
