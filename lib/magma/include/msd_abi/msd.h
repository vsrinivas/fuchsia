// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef _MSD_H_
#define _MSD_H_

#include "msd_defs.h"

#if defined(__cplusplus)
extern "C" {
#endif

// Instantiates a driver instance.
struct msd_driver* msd_driver_create(void);

// Configures the driver according to |flags|.
void msd_driver_configure(struct msd_driver* drv, uint32_t flags);

// Destroys a driver instance.
void msd_driver_destroy(struct msd_driver* drv);

// Creates a device at system startup.
struct msd_device* msd_driver_create_device(struct msd_driver* drv, void* device);

// Destroys a device at system shutdown.
void msd_device_destroy(struct msd_device* dev);

// Returns the device id.  0 is an invalid device id.
uint32_t msd_device_get_id(struct msd_device* dev);

void msd_device_dump_status(struct msd_device* dev);

// Opens a device for the given client. Returns null on failure
struct msd_connection* msd_device_open(struct msd_device* dev, msd_client_id client_id);

// Provides a buffer to be scanned out on the next vblank event.
// |callback| will be called with |data| as its second argument when |buf| is
// no longer being displayed and is safe to be reused. The first argument to
// |callback| indicates an error with the page flip, where 0 indicates success
void msd_device_page_flip(struct msd_device* dev, struct msd_buffer* buf,
                          struct magma_system_image_descriptor* image_desc,
                          magma_system_pageflip_callback_t callback, void* data);

// Closes the given connection to the device.
void msd_connection_close(struct msd_connection* connection);

// Creates a context for the given connection. returns null on failure.
struct msd_context* msd_connection_create_context(struct msd_connection* connection);

// Returns 0 on success.
// Blocks until all currently outstanding work on the given buffer completes.
// If more work that references this buffer is queued while waiting, this may return before that
// work is completed.
magma_status_t msd_connection_wait_rendering(struct msd_connection* connection,
                                             struct msd_buffer* buf);

// Destroys the given context.
void msd_context_destroy(struct msd_context* ctx);

// returns 0 on success
// |ctx| is the context in which to execute the command buffer
// |cmd_buf| is the command buffer to be executed
// |exec_resources| is all of the buffers referenced by the handles in command_buf->exec_resources
// in the same order
magma_status_t msd_context_execute_command_buffer(struct msd_context* ctx,
                                                  struct msd_buffer* cmd_buf,
                                                  struct msd_buffer** exec_resources);

// Creates a buffer that owns the provided handle
// The resulting msd_buffer is owned by the caller and must be destroyed
// Returns NULL on failure.
struct msd_buffer* msd_buffer_import(uint32_t handle);

// Destroys |buf|
// This releases buf's reference to the underlying platform buffer
void msd_buffer_destroy(struct msd_buffer* buf);

#if defined(__cplusplus)
}
#endif

#endif // _MSD_H_
