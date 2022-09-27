// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_DRIVER_RUNTIME_INCLUDE_LIB_FDF_CPP_CHANNEL_H_
#define LIB_DRIVER_RUNTIME_INCLUDE_LIB_FDF_CPP_CHANNEL_H_

#include <lib/fdf/channel.h>
#include <lib/fdf/cpp/arena.h>
#include <lib/fdf/cpp/unowned.h>
#include <lib/stdcompat/span.h>
#include <lib/zx/status.h>
#include <lib/zx/time.h>

namespace fdf {

// C++ wrapper for a channel, with RAII semantics. Automatically closes
// the channel when it goes out of scope.
//
// # Thread safety
//
// This class is thread-unsafe.
//
// # Example
//
//   constexpr uint32_t kTag = 'EXAM';
//   fdf::Arena arena(kTag);
//
//   void* data = arena.Allocate(0x1000);
//   // Set the data to transfer
//   ...
//
//   auto channels = fdf::ChannelPair::Create(0);
//   // Transfer end1 of the channel pair elsewhere.
//   ...
//
//   auto write_status = channels->end0.Write(0, arena, data, 0x1000,
//                                            cpp20::span<zx_handle_t>());
//
//   auto channel_read = std::make_unique<fdf::ChannelRead>(
//       channels->end0, 0,
//       [&](fdf_dispatcher_t* dispatcher, fdf::ChannelRead* channel_read, zx_status_t status) {
//         fdf::Channel channel(channel_read->channel());
//         auto read_return = channel.Read(0);
//         ...
//  });
//  zx_status_t status = channel_read->Begin(dispatcher_.get());
class Channel {
 public:
  using HandleType = fdf_handle_t;

  Channel() : channel_(FDF_HANDLE_INVALID) {}
  explicit Channel(fdf_handle_t channel) : channel_(channel) {}

  // Channel cannot be copied.
  Channel(const Channel& to_copy) = delete;
  Channel& operator=(const Channel& other) = delete;

  // Channel can be moved. Once moved, invoking a method on an instance will
  // yield undefined behavior.
  Channel(Channel&& other) noexcept : Channel(other.release()) {}
  Channel& operator=(Channel&& other) noexcept {
    reset(other.release());
    return *this;
  }

  // Closes the handle, causing the underlying object to be reclaimed by the runtime
  // if no other handles to it exist.
  //
  // If there is a pending callback registered via |fdf_channel_wait_async|,
  // it must be cancelled before this is called. For unsynchronized dispatchers,
  // cancellation is not considered complete until the callback is invoked.
  //
  // It is not an error to close the special "never a valid handle" FDF_HANDLE_INVALID,
  // similar to free(NULL) being a valid call.
  //
  // Closing the last handle to a peered object using |fdf_handle_close| can affect the
  // state of the object's peer (if any).
  ~Channel() { close(); }

  // Attempts to write a message to the channel.
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
  zx::status<> Write(uint32_t options, const Arena& arena, void* data, uint32_t num_bytes,
                     cpp20::span<zx_handle_t> handles) const {
    uint32_t handles_size = static_cast<uint32_t>(handles.size());
    return zx::make_status(fdf_channel_write(channel_, options, arena.get(), data, num_bytes,
                                             handles.data(), handles_size));
  }

  struct ReadReturn {
    // Holds ownership of |arena|, which will be destroyed when it goes out of scope.
    Arena arena;
    // The lifetime of |data| and |handles| are tied to the lifetime of |arena|.
    void* data;
    // The length of |data| in bytes.
    uint32_t num_bytes;
    // |handles| may be a mix of zircon handles and fdf handles.
    cpp20::span<zx_handle_t> handles;
  };

  // Attempts to read the first message from the channel and returns a |ReadReturn|.
  //
  // # Errors
  //
  // ZX_ERR_SHOULD_WAIT: The channel contained no messages to read.
  //
  // ZX_ERR_PEER_CLOSED: There are no available messages and the other
  // side of the channel is closed.
  //
  // This operation is thread-safe.
  zx::status<ReadReturn> Read(uint32_t options) const {
    fdf_arena_t* arena;
    void* data;
    uint32_t num_bytes;
    zx_handle_t* handles;
    uint32_t num_handles;
    auto status =
        fdf_channel_read(channel_, options, &arena, &data, &num_bytes, &handles, &num_handles);
    if (status != ZX_OK) {
      return zx::error(status);
    }
    cpp20::span<zx_handle_t> out_handles{handles, num_handles};
    return zx::ok(ReadReturn{Arena(arena), data, num_bytes, out_handles});
  }

  // Channel::Call() is like a combined Channel::Write(), ChannelRead::Begin(),
  // and Channel::Call(), with the addition of a feature where a transaction id at
  // the front of the message payload bytes is used to match reply messages with send messages,
  // enabling multiple calling threads to share a channel without any additional client-side
  // bookkeeping.
  //
  // The first four bytes of the written and read back messages are treated as a
  // transaction ID of type fdf_txid_t. The runtime generates a txid for the
  // written message, replacing that part of the message as read from the user.
  // The runtime generated txid will be between 0x80000000 and 0xFFFFFFFF,
  // and will not collide with any txid from any other Channel::Call()
  // in progress against this channel endpoint. If the written message has a
  // length of fewer than four bytes, an error is reported.
  //
  // While |deadline| has not passed, if an inbound message arrives with a matching txid,
  // instead of being added to the tail of the general inbound message queue,
  // it is delivered directly to the thread waiting in Channel::Call().
  //
  // If such a reply arrives after |deadline| has passed, it will arrive in the
  // general inbound message queue.
  //
  // All written handles are consumed and are no longer available to the caller,
  // on success or failure.
  //
  // # Errors
  //
  // ZX_ERR_INVALID_ARGS: |data| or |handles| are not pointers managed by |arena|,
  // are not managed by |arena|, or element in |handles| is not a valid handle,
  // or |num_bytes| is less than four,
  // or at least one of |handles| has a pending callback registered via a ChannelRead.
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
  zx::status<ReadReturn> Call(uint32_t options, zx::time deadline, const Arena& arena, void* data,
                              uint32_t num_bytes, cpp20::span<zx_handle_t> handles) const {
    fdf_arena_t* rd_arena;
    void* rd_data;
    uint32_t rd_num_bytes;
    zx_handle_t* rd_handles;
    uint32_t rd_num_handles;

    fdf_channel_call_args_t args = {
        .wr_arena = arena.get(),
        .wr_data = data,
        .wr_num_bytes = num_bytes,
        .wr_handles = handles.data(),
        .wr_num_handles = static_cast<uint32_t>(handles.size()),
        .rd_arena = &rd_arena,
        .rd_data = &rd_data,
        .rd_num_bytes = &rd_num_bytes,
        .rd_handles = &rd_handles,
        .rd_num_handles = &rd_num_handles,
    };
    zx_status_t status = fdf_channel_call(channel_, options, deadline.get(), &args);
    if (status != ZX_OK) {
      return zx::error(status);
    }
    cpp20::span<zx_handle_t> out_handles{rd_handles, rd_num_handles};
    return zx::ok(ReadReturn{Arena(rd_arena), rd_data, rd_num_bytes, out_handles});
  }

  bool is_valid() const { return channel_ != FDF_HANDLE_INVALID; }

  fdf_handle_t get() const { return channel_; }

  void reset(fdf_handle_t channel = FDF_HANDLE_INVALID) {
    close();
    channel_ = channel;
  }

  void close() {
    if (channel_ != FDF_HANDLE_INVALID) {
      fdf_handle_close(channel_);
      channel_ = FDF_HANDLE_INVALID;
    }
  }

  fdf_handle_t release() {
    fdf_handle_t ret = channel_;
    channel_ = FDF_HANDLE_INVALID;
    return ret;
  }

 private:
  fdf_handle_t channel_;
};

class ChannelPair {
 public:
  ChannelPair(Channel channel0, Channel channel1)
      : end0(std::move(channel0)), end1(std::move(channel1)) {}

  static zx::status<ChannelPair> Create(uint32_t options) {
    fdf_handle_t channel0, channel1;
    zx_status_t status = fdf_channel_create(options, &channel0, &channel1);
    if (status != ZX_OK) {
      return zx::error(status);
    }
    return zx::ok(ChannelPair{Channel(channel0), Channel(channel1)});
  }

  Channel end0;
  Channel end1;
};

using UnownedChannel = fdf::Unowned<Channel>;

}  // namespace fdf

#endif  // LIB_DRIVER_RUNTIME_INCLUDE_LIB_FDF_CPP_CHANNEL_H_
