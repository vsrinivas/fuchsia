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

// echo -n "mx_device_ops_v0.5" | sha256sum | cut -c1-16
#define DEVICE_OPS_VERSION 0Xc9410d2a24f57424

//@doc(docs/ddk/device-ops.md)

//@ # The Device Protocol
//
// Device drivers implement a set of hooks (methods) to support the
// operations that may be done on the devices that they publish.
//
// These are described below, including the action that is taken
// by the default implementation that is used for each hook if the
// driver does not provide its own implementation.

typedef struct mx_protocol_device {
    //@ ## version
    // This field must be set to `DEVICE_OPS_VERSION`
    uint64_t version;

    mx_status_t (*get_protocol)(void* ctx, uint32_t proto_id, void* protocol);

    //@ ## open
    //
    // The open hook is called when a device is opened via the device filesystem,
    // or when an existing open connection to a device is cloned (for example,
    // when a device fd is shared with another process).  The default open hook,
    // if a driver does not implement one, simply returns **MX_OK**.
    //
    // Drivers may want to implement open to disallow simultaneous access (by
    // failing if the device is already open), or to return a new **device instance**
    // instead.
    //
    // The optional *dev_out* parameter allows a device to create and return a
    // **device instance** child device, which can be used to manage per-instance
    // state instead of all client connections interacting with the device itself.
    // A child created for return as an instance **must** be created with the
    // **DEVICE_ADD_INSTANCE** flag set in the arguments to **device_add()**.
    //
    mx_status_t (*open)(void* ctx, mx_device_t** dev_out, uint32_t flags);

    //@ ## open_at
    // The open_at hook is called in the event that the open path to the device
    // contains segments after the device name itself.  For example, if a device
    // exists as `/dev/misc/foo` and an attempt is made to `open("/dev/misc/foo/bar",...)`,
    // the open_at hook would be invoked with a *path* of `"bar"`.
    //
    // The default open_at implementation returns **MX_ERR_NOT_SUPPORTED**
    //
    mx_status_t (*open_at)(void* ctx, mx_device_t** dev_out, const char* path, uint32_t flags);

    //@ ## close
    // The close hook is called when a connection to a device is closed.  These
    // calls will balance the calls to open or open_at.
    //
    // **Note:** If open or open_at return a **device instance**, the balancing close
    // hook that is called is the close hook on the **instance**, not the parent.
    //
    // The default close implementation returns **MX_OK**.
    mx_status_t (*close)(void* ctx, uint32_t flags);

    //@ ## unbind
    // The unbind hook is called when the parent of this device is being removed (due
    // to hot unplug, fatal error, etc).  At the point unbind is called, it is not
    // possible for further open or open_at calls to occur, but io operations, etc
    // may continue until those client connections are closed.
    //
    // The driver should adjust its state to encourage its client connections to close
    // (cause IO to error out, etc), and call **device_remove()** on itself when ready.
    //
    // The driver must continue to handle all device hooks until the **release** hook
    // is invoked.
    //
    void (*unbind)(void* ctx);

    //@ ## release
    // The release hook is called after this device has been removed by **device_remove()**
    // and all open client connections have been closed, and all child devices have been
    // removed and released.
    //
    // At the point release is invoked, the driver will not receive any further calls
    // and absolutely must not use the underlying **mx_device_t** once this method
    // returns.
    //
    // The driver must free all memory and release all resources related to this device
    // before returning.
    void (*release)(void* ctx);

    //@ ## read
    // The read hook is an attempt to do a non-blocking read operation.
    //
    // On success *actual* must be set to the number of bytes read (which may be less
    // than the number requested in *count*), and return **MX_OK**.
    //
    // A successful read of 0 bytes is generally treated as an End Of File notification
    // by clients.
    //
    // If no data is available now, **MX_ERR_SHOULD_WAIT** must be returned and when
    // data becomes available `device_state_set(DEVICE_STATE_READABLE)` may be used to
    // signal waiting clients.
    //
    // This hook **must not block**.  Use `iotxn_queue` to handle IO which
    // requires processing and delayed status.
    //
    // The default read implementation returns **MX_ERR_NOT_SUPPORTED**.
    //
    mx_status_t (*read)(void* ctx, void* buf, size_t count,
                        mx_off_t off, size_t* actual);

    //@ ## write
    // The write hook is an attempt to do a non-blocking write operation.
    //
    // On success *actual* must be set to the number of bytes written (which may be
    // less than the number requested in *count*), and **MX_OK** should be returned.
    //
    // If it is not possible to write data at present **MX_ERR_SHOULD_WAIT** must
    // be returned and when it is again possible to write,
    // `device_state_set(DEVICE_STATE_WRITABLE)` may be used to signal waiting clients.
    //
    // This hook **must not block**.  Use `iotxn_queue` to handle IO which
    // requires processing and delayed status.
    //
    // The default write implementation returns **MX_ERR_NOT_SUPPORTED**.
    //
    mx_status_t (*write)(void* ctx, const void* buf, size_t count,
                         mx_off_t off, size_t* actual);

    //@ ## iotxn_queue
    // The iotxn_queue hook is the core mechanism for asynchronous IO.  A driver that
    // implements iotxn_queue should not implement read or write, as iotxn_queue takes
    // precedence over them.
    //
    // The iotxn_queue hook may not block.  It is expected to start the IO operation
    // and return immediately, having taken ownership of *txn*.
    //
    // When the operation succeeds or fails, the completion callback on *txn* must be
    // called to finish the operation.  If the request is invalid, the completion
    // callback may be called from within the iotxn_queue hook.  Otherwise it is
    // usually called from an irq handler or worker thread that observes the success
    // or failure of the requested IO operation.
    //
    // There is no default iotxn_queue implementation.
    //
    void (*iotxn_queue)(void* ctx, iotxn_t* txn);

    //@ ## get_size
    // If the device is seekable, the get_size hook should return the size of the device.
    //
    // This is the offset at which no more reads or writes are possible.
    //
    // The default implementation returns 0.
    mx_off_t (*get_size)(void* ctx);

    //@ ## ioctl
    // The ioctl hook allows support for device-specific operations.
    //
    // These, like read, write, and iotxn_queue, must not block.
    //
    // On success, **MX_OK** must be returned and *out_actual* must be set
    // to the number of output bytes provided (0 if none).
    //
    // The default ioctl implementation returns **MX_ERR_NOT_SUPPORTED**.
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


// Direct Device Ops Functions
mx_status_t device_read(mx_device_t* dev, void* buf, size_t count,
                        mx_off_t off, size_t* actual);

mx_status_t device_write(mx_device_t* dev, const void* buf, size_t count,
                         mx_off_t off, size_t* actual);

mx_off_t device_get_size(mx_device_t* dev);

mx_status_t device_ioctl(mx_device_t* dev, uint32_t op,
                         const void* in_buf, size_t in_len,
                         void* out_buf, size_t out_len, size_t* out_actual);

// return MX_ERR_NOT_SUPPORTED if this device does not support the iotxn_queue op
// otherwise returns MX_OK aftering queuing the iotxn
mx_status_t device_iotxn_queue(mx_device_t* dev, iotxn_t* txn);


// Device State Change Functions
//@ #### Device State Bits
//{
#define DEV_STATE_READABLE DEVICE_SIGNAL_READABLE
#define DEV_STATE_WRITABLE DEVICE_SIGNAL_WRITABLE
#define DEV_STATE_ERROR DEVICE_SIGNAL_ERROR
#define DEV_STATE_HANGUP DEVICE_SIGNAL_HANGUP
#define DEV_STATE_OOB DEVICE_SIGNAL_OOB
//}

void device_state_clr_set(mx_device_t* dev, mx_signals_t clearflag, mx_signals_t setflag);

//@ #### device_state_set
static inline void device_state_set(mx_device_t* dev, mx_signals_t stateflag) {
    device_state_clr_set(dev, 0, stateflag);
}
static inline void device_state_clr(mx_device_t* dev, mx_signals_t stateflag) {
    device_state_clr_set(dev, stateflag, 0);
}

__END_CDECLS;
