// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_DRIVER_RUNTIME_INCLUDE_LIB_FDF_CHANNEL_H_
#define LIB_DRIVER_RUNTIME_INCLUDE_LIB_FDF_CHANNEL_H_

#include <lib/fdf/arena.h>
#include <lib/fdf/dispatcher.h>
#include <lib/fdf/handle.h>
#include <lib/fdf/types.h>

__BEGIN_CDECLS

// A bi-directional in-process message transport capable of sending arena-managed raw data bytes
// as well as handles from one side to the other.
//
// # Thread safety
//
// Most operations on channel objects are thread-safe, except for |fdf_channel_cancel_wait|.
//
// # Example
//
//   // Read handler for asynchronous channel reads.
//   void read_handler(fdf_dispatcher_t* dispatcher, fdf_channel_read_t* read, zx_status_t status);
//
//   // Sends a request to the peer of |channel| and asynchronously waits for a response.
//   void send_request(fdf_handle_t channel, fdf_dispatcher_t* dispatcher dispatcher) {
//     fdf_arena_t* arena;
//     zx_status_t status = fdf_arena_create(0, 'exam', 0, &arena);
//
//     void* data = fdf_arena_allocate(arena, 0x1000);
//     // Set the data to transfer
//     ...
//
//     // Write the request to the channel.
//     status = fdf_channel_write(ch0, 0, arena, data, 0x1000, NULL, 0);
//
//     // Asynchronously wait for a response.
//     fdf_channel_read_t* channel_read = calloc(1, sizeof(fdf_channel_read_t));
//     channel_read->handler = read_handler;
//     channel_read->channel = ch0;
//     status = fdf_channel_wait_async(dispatcher, channel_read, 0);
//
//     // We are done with the arena.
//     fdf_arena_destroy(arena);
//   }
//
//   void read_handler(fdf_dispatcher_t* dispatcher, fdf_channel_read_t* read,
//                     zx_status_t status) {
//     fdf_arena_t* arena;
//     void* data data;
//     uint32_t data_size;
//     zx_handle_t* handles;
//     uint32_t num_handles;
//     zx_status_t status = fdf_channel_read(read->channel, 0, &arena, &data, &data_size,
//                                            &handles, &num_handles);
//     // Process the read data.
//     ...
//
//     // Read provides you with an arena which you may reuse for other requests.
//     // You are in charge of destroying it.
//     fdf_arena_destroy(arena);
//     free(read);
//   }
//

// Defined in <lib/fdf/channel_read.h>
struct fdf_channel_read;

// Creates a FDF channel, a bi-directional in-process message transport capable of
// sending arena-managed raw data bytes as well as handles from one side to the other.
//
// Two handles are returned on success, providing access to both sides of the channel.
// Messages written to one handle may be read from the opposite.
//
// # Errors
//
// ZX_ERR_INVALID_ARGS: |out0| or |out1| is an invalid pointer or NULL or |options| is any value
// other than 0.
//
// ZX_ERR_NO_MEMORY: Failed due to a lack of memory.
//
// ZX_ERR_NO_RESOURCES: No handles are available.
zx_status_t fdf_channel_create(uint32_t options, fdf_handle_t* out0, fdf_handle_t* out1);

// Attempts to write a message to the channel specified by |channel|.
//
// The caller retains ownership of |arena|, which must be destroyed via |fdf_arena_destroy|.
// It is okay to destroy the arena as soon as the write call returns as the lifetime of
// the arena is extended until the data is read.
//
// The pointers |data| and |handles| may be NULL if their respective sizes are zero.
// |data| and |handles| must be pointers managed by |arena| if they are not NULL.
// |handles| may be a mix of zircon handles and fdf handles.
//
// Handles with a pending callback registered via |fdf_channel_wait_async| cannot be transferred.
//
// On success, all |num_handles| of the handles in the handles array are attached to the
// message and will become available to the reader of that message from the opposite end of the
// channel.
//
// All handles are consumed and are no longer available to the caller, on success or failure.
//
// # Errors
//
// ZX_ERR_INVALID_ARGS: |data| or |handles| are not pointers managed by |arena|,
// or any element in |handles| is not a valid handle,
// or at least one of |handles| has a pending callback registered via |fdf_channel_wait_async|.
//
// ZX_ERR_NO_MEMORY: Failed due to a lack of memory.
//
// ZX_ERR_PEER_CLOSED: The other side of the channel is closed.
//
// This operation is thread-safe.
zx_status_t fdf_channel_write(fdf_handle_t channel, uint32_t options, fdf_arena_t* arena,
                              void* data, uint32_t num_bytes, zx_handle_t* handles,
                              uint32_t num_handles);

// Attempts to read the first message from the channel specified by |channel| into
// the |data| and |handles| buffers.
//
// Provides ownership of |arena|, which must be destroyed via |fdf_arena_destroy|.
//
// The lifetime of |data| and |handles| are tied to the lifetime of |arena|.
// |handles| may be a mix of zircon handles and fdf handles.
//
// The length of bytes, in bytes, is stored in the location pointed to by |out_num_bytes|.
// The number of handles is stored in the location pointed to by |out_num_handles|.
// Either |out_num_bytes| or |out_num_handles| may be NULL, in which case they will be ignored.
//
// # Errors
//
// ZX_ERR_INVALID_ARGS: |arena| is NULL when |data| or |handles| are non-NULL.
//
// ZX_ERR_SHOULD_WAIT: The channel contained no messages to read.
//
// ZX_ERR_PEER_CLOSED: There are no available messages and the other
// side of the channel is closed.
//
// This operation is thread-safe.
zx_status_t fdf_channel_read(fdf_handle_t channel, uint32_t options, fdf_arena_t** out_arena,
                             void** out_data, uint32_t* out_num_bytes, zx_handle_t** out_handles,
                             uint32_t* out_num_handles);

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
// # Errors
//
// ZX_ERR_INVALID_ARGS: |dispatcher| or |channel_read| is an invalid pointer or NULL
// or |options| is any value other than 0.
//
// ZX_ERR_PEER_CLOSED: There are no available messages and the other
// side of the channel is closed.
//
// ZX_ERR_BAD_STATE: There is already a dispatcher waiting on this channel.
//
// ZX_ERR_UNAVAILABLE: |dispatcher| is shutting down.
//
// This operation is thread-safe.
zx_status_t fdf_channel_wait_async(struct fdf_dispatcher* dispatcher,
                                   struct fdf_channel_read* channel_read, uint32_t options);

// Cancels any pending callback registered via |fdf_channel_wait_async|.
//
// Whether the wait handler will run depends on whether the dispatcher it
// was registered with is synchronized.
//
// If the dispatcher is synchronized, this must only be called from a dispatcher
// thread, and any pending callback will be canceled synchronously.
//
// If the dispatcher is unsynchronized, the callback will be scheduled to be called
// with status |ZX_ERR_CANCELED|.
//
// # Errors
//
// ZX_ERR_NOT_FOUND: There was no pending wait either because it is currently running
// (perhaps in a different thread), already scheduled to be run, already completed,
// or had not been started.
zx_status_t fdf_channel_cancel_wait(fdf_handle_t handle);

// fdf_channel_call() is like a combined fdf_channel_write(), fdf_channel_wait_async(),
// and fdf_channel_read(), with the addition of a feature where a transaction id at
// the front of the message payload bytes is used to match reply messages with send messages,
// enabling multiple calling threads to share a channel without any additional client-side
// bookkeeping.
//
// The first four bytes of the written and read back messages are treated as a
// transaction ID of type fdf_txid_t. The runtime generates a txid for the
// written message, replacing that part of the message as read from the user.
// The runtime generated txid will be between 0x80000000 and 0xFFFFFFFF,
// and will not collide with any txid from any other fdf_channel_call()
// in progress against this channel endpoint. If the written message has a
// length of fewer than four bytes, an error is reported.
//
// While |deadline| has not passed, if an inbound message arrives with a matching txid,
// instead of being added to the tail of the general inbound message queue,
// it is delivered directly to the thread waiting in fdf_channel_call().
//
// If such a reply arrives after |deadline| has passed, it will arrive in the
// general inbound message queue.
//
// All written handles are consumed and are no longer available to the caller,
// on success or failure.
//
// # Errors
//
// ZX_ERR_INVALID_ARGS: |args| is NULL,
// or |wr_data| or |wr_handles| are non-NULL and not pointers managed by |wr_arena|,
// or any element in |handles| is not a valid handle,
// or |wr_num_bytes| is less than four,
// or at least one of |wr_handles| has a pending callback registered via |fdf_channel_wait_async|,
// or |rd_arena| is NULL when |rd_data| or |rd_handles| are non-NULL.
//
// ZX_ERR_PEER_CLOSED: The other side of the channel is closed.
//
// ZX_ERR_TIMED_OUT: |deadline| passed before a reply matching
// the correct txid was received.
//
// ZX_ERR_BAD_STATE: This is called from a driver runtime managed thread
// that does not allow sync calls.
//
// This operation is thread-safe.
zx_status_t fdf_channel_call(fdf_handle_t handle, uint32_t options, zx_time_t deadline,
                             const fdf_channel_call_args_t* args);

__END_CDECLS

#endif  // LIB_DRIVER_RUNTIME_INCLUDE_LIB_FDF_CHANNEL_H_
