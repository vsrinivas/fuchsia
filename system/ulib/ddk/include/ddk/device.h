// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>
#include <magenta/device/device.h>
#include <magenta/syscalls.h>
#include <magenta/types.h>
#include <ddk/iotxn.h>
#include <magenta/listnode.h>

__BEGIN_CDECLS;

typedef struct mx_device mx_device_t;
typedef struct mx_driver mx_driver_t;
typedef struct mx_device_prop mx_device_prop_t;

typedef struct mx_protocol_device mx_protocol_device_t;

#define MX_DEVICE_NAME_MAX 31
// DO NOT DEFINE DDK_INTERNAL!
// These macros make it harder for drivers to use fields
// that they shouldn't.  If you work around them, your
// driver WILL break in the near future.  Don't do it.
#ifndef DDK_INTERNAL
#define DDK_PRIVATE(name) __ddk_internal__##name
#else
#define DDK_PRIVATE(name) name
#endif

struct mx_device {
    uintptr_t DDK_PRIVATE(magic);

    mx_protocol_device_t* ops;

    // reserved for driver use; will not be touched by devmgr
    void* ctx;

    uint32_t DDK_PRIVATE(flags);
    uint32_t DDK_PRIVATE(refcount);

    mx_handle_t DDK_PRIVATE(event);
    mx_handle_t DDK_PRIVATE(rpc);
    mx_handle_t DDK_PRIVATE(resource);

    // most devices implement a single
    // protocol beyond the base device protocol
    uint32_t DDK_PRIVATE(protocol_id);
    void* DDK_PRIVATE(protocol_ops);

    // driver that has published this device
    mx_driver_t* DDK_PRIVATE(driver);

    // parent in the device tree
    mx_device_t* DDK_PRIVATE(parent);

    // driver that is bound to this device, NULL if unbound
    mx_driver_t* DDK_PRIVATE(owner);

    void* DDK_PRIVATE(owner_cookie);

    // for the parent's device_list
    struct list_node DDK_PRIVATE(node);

    // list of this device's children in the device tree
    struct list_node DDK_PRIVATE(children);

    // list of this device's instances
    struct list_node DDK_PRIVATE(instances);

    // iostate
    void* DDK_PRIVATE(ios);

    char DDK_PRIVATE(name)[MX_DEVICE_NAME_MAX + 1];
};

// mx_device_t objects must be created or initialized by the driver manager's
// device_create() function.  Drivers MAY NOT touch any
// fields in the mx_device_t, except for the protocol_id and protocol_ops
// fields which it may fill out after init and before device_add() is called,
// and the ctx field which may be used to store driver-specific data.



// echo -n "mx_device_ops_v0.5" | sha256sum | cut -c1-16
#define DEVICE_OPS_VERSION 0Xc9410d2a24f57424

// The Device Protocol
typedef struct mx_protocol_device {
    // Set to DEVICE_OPS_VERSION
    uint64_t version;

    // Asks if the device supports a specific protocol.
    // If it does, protocol ops returned via *protocol.
    mx_status_t (*get_protocol)(void* ctx, uint32_t proto_id, void* protocol);

    // The optional dev_out parameter allows a device to create a per-instance
    // child device on open and return that (resulting in the opener opening
    // that child device instead).  If dev_out is not modified the device itself
    // is opened.
    //
    // The per-instance child should be created with the DEVICE_ADD_INSTANCE flag set
    // in the arguments to device_add().
    //
    // open is also called whenever a device is cloned (a new handle is obtained).
    mx_status_t (*open)(void* ctx, mx_device_t** dev_out, uint32_t flags);

    // Experimental open variant where a sub-device path is specified.
    // Otherwise identical operation as open.  The default implementation
    // simply returns ERR_NOT_SUPPORTED.
    mx_status_t (*open_at)(void* ctx, mx_device_t** dev_out, const char* path, uint32_t flags);

    // close is called whenever a handle to the device is closed (or the process
    // holding it exits).  Usually there's no need for a specific close hook, just
    // handling release() which is called after the final handle is closed and the
    // device is unbound is sufficient. flags is a copy of the flags used to
    // open the device.
    mx_status_t (*close)(void* ctx, uint32_t flags);

    // Notifies the device that its parent is being removed (has been hot unplugged, etc).
    // Usually the device should then remove any children it has created.
    // When unbind() is called, the device is no longer open()able except by cloning
    // or open_at()ing existing open handles.
    void (*unbind)(void* ctx);

    // Release any resources held by the mx_device_t and free() it.
    // release is called after a device is remove()'d and its
    // refcount hits zero (all closes and unbinds complete)
    void (*release)(void* ctx);

    // attempt to read count bytes at offset off
    // off may be ignored for devices without the concept of a position
    // actual number of bytes read are returned in actual out argument
    mx_status_t (*read)(void* ctx, void* buf, size_t count, mx_off_t off, size_t* actual);

    // attempt to write count bytes at offset off
    // off may be ignored for devices without the concept of a position
    // actual number of bytes written are returned in actual out argument
    mx_status_t (*write)(void* ctx, const void* buf, size_t count, mx_off_t off, size_t* actual);

    // queue an iotxn. iotxn's are always completed by its complete() op
    void (*iotxn_queue)(void* ctx, iotxn_t* txn);

    // optional: return the size (in bytes) of the readable/writable space
    // of the device.  Will default to 0 (non-seekable) if this is unimplemented
    mx_off_t (*get_size)(void* ctx);

    // optional: do an device-specific io operation
    // number of bytes copied to out_buf are returned in out_actual
    // out_len and out_actual may be NULL if out_len is zero
    mx_status_t (*ioctl)(void* ctx, uint32_t op,
                     const void* in_buf, size_t in_len,
                     void* out_buf, size_t out_len, size_t* out_actual);

    // Stops the device and puts it in a low power mode
    mx_status_t (*suspend)(void* ctx, uint32_t flags);

    // Restarts the device after being suspended
    mx_status_t (*resume)(void* ctx, uint32_t flags);
} mx_protocol_device_t;


// Device Accessors
const char* device_get_name(mx_device_t* dev);

mx_device_t* device_get_parent(mx_device_t* dev);

mx_handle_t device_get_resource(mx_device_t* dev);

// protocols look like:
// typedef struct {
//     protocol_xyz_ops_t* ops;
//     void* ctx;
// } protocol_xyz_t;
mx_status_t device_get_protocol(mx_device_t* dev, uint32_t proto_id, void* protocol);


#ifndef DDK_INTERNAL

// Direct Device Ops Functions
mx_status_t device_op_read(mx_device_t* dev, void* buf, size_t count,
                           mx_off_t off, size_t* actual);

mx_status_t device_op_write(mx_device_t* dev, const void* buf, size_t count,
                            mx_off_t off, size_t* actual);

mx_off_t device_op_get_size(mx_device_t* dev);

mx_status_t device_op_ioctl(mx_device_t* dev, uint32_t op,
                            const void* in_buf, size_t in_len,
                            void* out_buf, size_t out_len, size_t* out_actual);

// return MX_ERR_NOT_SUPPORTED if this device does not support the iotxn_queue op
// otherwise returns MX_OK aftering queuing the iotxn
mx_status_t device_op_iotxn_queue(mx_device_t* dev, iotxn_t* txn);


// Device State Change Functions
#define DEV_STATE_READABLE DEVICE_SIGNAL_READABLE
#define DEV_STATE_WRITABLE DEVICE_SIGNAL_WRITABLE
#define DEV_STATE_ERROR DEVICE_SIGNAL_ERROR
#define DEV_STATE_HANGUP DEVICE_SIGNAL_HANGUP
#define DEV_STATE_OOB DEVICE_SIGNAL_OOB

void device_state_clr_set(mx_device_t* dev, mx_signals_t clearflag, mx_signals_t setflag);

static inline void device_state_set(mx_device_t* dev, mx_signals_t stateflag) {
    device_state_clr_set(dev, 0, stateflag);
}
static inline void device_state_clr(mx_device_t* dev, mx_signals_t stateflag) {
    device_state_clr_set(dev, stateflag, 0);
}

//TODO: deprecate and remove this version
//      (it does not reflect that the clear is applied and then the set)
static inline void device_state_set_clr(mx_device_t* dev, mx_signals_t setflag, mx_signals_t clearflag) {
    device_state_clr_set(dev, clearflag, setflag);
}

#endif

#undef DDK_PRIVATE

__END_CDECLS;
