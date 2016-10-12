// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/types.h>
#include <magenta/listnode.h>
#include <magenta/compiler.h>
#include <stdint.h>

__BEGIN_CDECLS;

typedef struct mx_device mx_device_t;
typedef struct mx_protocol_device mx_protocol_device_t;

typedef struct mx_driver mx_driver_t;
typedef struct mx_bind_inst mx_bind_inst_t;
typedef struct mx_driver_binding mx_driver_binding_t;

typedef struct mx_driver_ops {
    mx_status_t (*init)(mx_driver_t* driver);
    // Opportunity to do on-load work.
    // Called ony once, before any other ops are called.

    mx_status_t (*bind)(mx_driver_t* driver, mx_device_t* device);
    // Requests that the driver bind to the provided device,
    // initialize it, and publish and children.

    mx_status_t (*release)(mx_driver_t* driver);
    // Last call before driver is unloaded.
} mx_driver_ops_t;

#define DRV_FLAG_NO_AUTOBIND 0x00000001

struct mx_driver {
    const char* name;

    mx_driver_ops_t ops;

    uint32_t flags;

    struct list_node node;

    const mx_bind_inst_t* binding;
    uint32_t binding_size;
    // binding instructions
};

struct mx_driver_binding {
    uint32_t protocol_id;
    void* protocol_info;
};

// Device Manager API
mx_status_t device_create(mx_device_t** device, mx_driver_t* driver,
                          const char* name, mx_protocol_device_t* ops);
void device_init(mx_device_t* device, mx_driver_t* driver,
                 const char* name, mx_protocol_device_t* ops);
// Devices are created or (if embedded in a driver-specific structure)
// initialized with the above functions.  The mx_device_t will be completely
// written during initialization, and after initialization and before calling
// device_add() they driver may only modify the protocol_id and protocol_ops
// fields of the mx_device_t.

mx_status_t device_add(mx_device_t* device, mx_device_t* parent);
mx_status_t device_add_instance(mx_device_t* device, mx_device_t* parent);
mx_status_t device_remove(mx_device_t* device);
mx_status_t device_rebind(mx_device_t* device);

// These are only for the use of core platform drivers and may return
// NULL for non-approved callers.

mx_device_t* driver_get_root_device(void);
mx_device_t* driver_get_misc_device(void);

// Devices are bindable by drivers by default.
// This can be used to prevent a device from being bound by a driver
void device_set_bindable(mx_device_t* dev, bool bindable);

void driver_add(mx_driver_t* driver);
void driver_remove(mx_driver_t* driver);
void driver_unbind(mx_driver_t* driver, mx_device_t* dev);

// temporary accessor for root resource handle
mx_handle_t get_root_resource(void);

// panic is for handling non-recoverable, non-reportable fatal
// errors in a way that will get logged.  Right now this just
// does a bogus write to unmapped memory.
static inline void panic(void) {
    for (;;) {
        *((int*) 0xdead) = 1;
    }
}

// Protocol Identifiers
#define DDK_PROTOCOL_DEF(tag, val, name) MX_PROTOCOL_##tag = val,
enum {
#include <ddk/protodefs.h>
};

__END_CDECLS;
