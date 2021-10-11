// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_DRIVER_RUNTIME_INCLUDE_LIB_FDF_CHANNEL_H_
#define LIB_DRIVER_RUNTIME_INCLUDE_LIB_FDF_CHANNEL_H_

#include <lib/fdf/arena.h>
#include <lib/fdf/dispatcher.h>
#include <lib/fdf/types.h>

__BEGIN_CDECLS

// Usage Notes:
//
// fdf_channel_t is a bi-directional message transport capable of sending
// arena-managed raw data bytes and handles from one side to the other.
//
// Example:
//
// // Read handler for asynchronous channel reads.
// void handler(fdf_dispatcher_t* dispatcher, fdf_channel_read_t* read, fdf_status_t status);
//
// // Sends a request to the peer of |channel| and asynchronously waits for a response.
// void send_request(fdf_handle_t channel, fdf_dispatcher_t* dispatcher dispatcher) {
//   fdf_arena_t* arena;
//   fdf_status_t status = fdf_arena_create(&arena);
//
//   void* data = fdf_arena_allocate(arena, 0x1000);
//   // Set the data to transfer
//   ...
//
//   // Write the request to the channel.
//   status = fdf_channel_write(ch0, 0, arena, data, 0x1000, NULL, 0);
//
//   // Asynchronously wait for a response.
//   fdf_channel_read_t* channel_read = calloc(1, sizeof(fdf_channel_read_t));
//   channel_read->handler = handler;
//   channel_read->channel = ch0;
//   status = fdf_channel_wait_async(dispatcher, channel_read, 0);
//
//   // We are done with the arena.
//   fdf_arena_destroy(arena);
// }
//
// void handler(fdf_dispatcher_t* dispatcher, fdf_channel_read_t* read, fdf_status_t status) {
//   fdf_arena_t* arena;
//   void* data data;
//   uint32_t data_size;
//   zx_handle_t* handles;
//   uint32_t num_handles;
//   fdf_status_t status = fdf_channel_read(read->channel, 0, &arena, &data, &data_size,
//                                          &handles, &num_handles);
//   // Process the read data.
//   ...
//
//   // Read provides you with an arena which you may reuse for other requests.
//   // You are in charge of destroying it.
//   fdf_arena_destroy(arena);
//   free(read);
// }
//
typedef struct fdf_channel fdf_channel_t;

// Defined in <lib/fdf/channel_read.h>
struct fdf_channel_read;

fdf_status_t fdf_channel_create(uint32_t options, fdf_handle_t* out0, fdf_handle_t* out1);

// Attempts to write a message to the channel specified by |channel|.
// The pointers |data| and |handles| may be NULL if their respective sizes are zero.
//
// |data| and |handles| must be a pointers managed by |arena| if they are not NULL.
// |handles| may be a mix of zircon handles and fdf handles.
// The caller retains ownership of |arena|, which must be destroyed via |fdf_arena_destroy|.
// It is okay to destroy the arena as soon as the write call returns as the lifetime of
// the arena is extended until the data is read.
//
// Returns |ZX_OK| if the write was successful.
// Returns |ZX_ERR_BAD_HANDLE| if |channel| is not a valid handle.
// Returns |ZX_ERR_INVALID_ARGS| if |data| or |handles| are not pointers managed by |arena|.
// Returns |ZX_ERR_PEER_CLOSED| if the other side of the channel is closed.
//
// This operation is thread-safe.
fdf_status_t fdf_channel_write(fdf_handle_t channel, uint32_t options, fdf_arena_t* arena,
                               void* data, uint32_t num_bytes, zx_handle_t* handles,
                               uint32_t num_handles);

// Attempts to read the first message from the channel specified by |channel| into
// the |data| and |handles| buffers.
//
// The lifetime of |data| and |handles| are tied to the lifetime of |arena|.
// |handles| may be a mix of zircon handles and fdf handles.
//
// Provides ownership of |arena|, which must be destroyed via |fdf_arena_destroy|.
//
// Returns |ZX_OK| if the read was successful.
// Returns |ZX_ERR_BAD_HANDLE| if |channel| is not a valid handle.
// Returns |ZX_ERR_INVALID_ARGS| if |arena| is NULL.
// Returns |ZX_ERR_SHOULD_WAIT| if the channel contained no messages to read.
// Returns |ZX_ERR_PEER_CLOSED| if the other side of the channel is closed.
//
// This operation is thread-safe.
fdf_status_t fdf_channel_read(fdf_handle_t channel, uint32_t options, fdf_arena_t** arena,
                              void** data, uint32_t* num_bytes, zx_handle_t** handles,
                              uint32_t* num_handles);

// Begins asynchronously waiting for the channel set in |channel_read| to be readable.
// The |dispatcher| invokes the handler when the wait completes.
// Only one dispatcher can be registered at a time. The dispatcher will
// be considered unregistered immediately before the read handler is invoked.
//
// After successfully scheduling the read, the client is responsible for retaining
// the |channel_read| structure in memory (and unmodified) until the read handler runs,
// or the dispatcher shuts down.  Thereafter, the |channel_read| may be started again
// or destroyed.
//
// The read handler will be invoked exactly once. When the dispatcher is
// shutting down (being destroyed), the handlers of any remaining wait
// may be invoked with a status of |ZX_ERR_CANCELED|.
//
// Returns |ZX_OK| if the wait was successfully begun.
// Returns |ZX_ERR_PEER_CLOSED| if the peer channel is closed.
// Returns |ZX_ERR_BAD_STATE| if there is already a dispatcher waiting
// on this channel, or if the dispatcher is shutting down.
//
// This operation is thread-safe.
fdf_status_t fdf_channel_wait_async(struct fdf_dispatcher* dispatcher,
                                    struct fdf_channel_read* channel_read, uint32_t options);

void fdf_handle_close(fdf_handle_t handle);

__END_CDECLS

#endif  // LIB_DRIVER_RUNTIME_INCLUDE_LIB_FDF_CHANNEL_H_
