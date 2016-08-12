// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef _MSD_H_
#define _MSD_H_

#include "msd_defs.h"
#include "msd_platform_buffer.h"

#if defined(__cplusplus)
extern "C" {
#endif

// The magma system driver... driver :)
struct msd_driver {
    int32_t magic_;
};

// The magma system driver device.
struct msd_device {
    int32_t magic_;
};

// A driver defined connection, owned by the MagmaSystemConnection
struct msd_connection {
    int32_t magic_;
};

// A driver defined buffer that owns a reference to an msd_platform_buffer
struct msd_buffer {
    int32_t magic_;
};

// A driver defined context, owned by the magma system context
struct msd_context {
    int32_t magic_;
};

// Instantiates a driver instance.
struct msd_driver* msd_driver_create(void);

// Destroys a driver instance.
void msd_driver_destroy(struct msd_driver* drv);

// Creates a device at system startup.
struct msd_device* msd_driver_create_device(struct msd_driver* drv, void* device);

// Destroys a device at system shutdown.
void msd_driver_destroy_device(struct msd_device* dev);

// Returns the device id.  0 is an invalid device id.
uint32_t msd_device_get_id(struct msd_device* dev);

// Opens a device for the given client. Returns null on failure
struct msd_connection* msd_device_open(struct msd_device* dev, msd_client_id client_id);

// Closes the given connection to the device.
void msd_connection_close(struct msd_connection* connection);

// Creates a context for the given connection. returns null on failure.
struct msd_context* msd_connection_create_context(struct msd_connection* connection);

// Destroys the given context.
void msd_connection_destroy_context(struct msd_connection* connection, struct msd_context* ctx);

// Creates a buffer that owns a reference to the provided platform buffer
// The resulting msd_buffer is owned by the caller and must be destroyed
// Returns NULL on failure.
struct msd_buffer* msd_buffer_import(struct msd_platform_buffer* platform_buf);

// Destroys |buf|
// This releases buf's reference to the underlying platform buffer
void msd_buffer_destroy(struct msd_buffer* buf);

#if defined(__cplusplus)
}
#endif

#endif // _MSD_H_
