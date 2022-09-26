// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_DRIVER_RUNTIME_INCLUDE_LIB_FDF_CPP_CHANNEL_READ_H_
#define LIB_DRIVER_RUNTIME_INCLUDE_LIB_FDF_CPP_CHANNEL_READ_H_

#include <lib/fdf/channel_read.h>
#include <lib/fdf/cpp/channel.h>
#include <lib/fit/function.h>
#include <lib/stdcompat/span.h>

#include <mutex>

namespace fdf {

// Holds context for an asynchronous read and its handler.
//
// After successfully beginning the read, the client is responsible for retaining
// the structure in memory (and unmodified) until the read's handler runs,
// or the dispatcher shuts down.  Thereafter, the read may be begun again or destroyed.
//
// This class must only be accessed on the dispatch thread since it lacks internal
// synchronization of its state.
//
// Concrete implementations: |fdf::ChannelRead|.
class ChannelReadBase {
 protected:
  explicit ChannelReadBase(fdf_handle_t channel, uint32_t options,
                           fdf_channel_read_handler_t* handler);

  ~ChannelReadBase();

  // ChannelReadBase cannot be moved or copied.
  ChannelReadBase(const ChannelReadBase&) = delete;
  ChannelReadBase(ChannelReadBase&&) = delete;
  ChannelReadBase& operator=(const ChannelReadBase&) = delete;
  ChannelReadBase& operator=(ChannelReadBase&&) = delete;

 public:
  fdf_handle_t channel() const { return channel_read_.channel; }
  void set_channel(fdf_handle_t channel) { channel_read_.channel = channel; }

  uint32_t options() const { return channel_read_.options; }
  void set_options(uint32_t options) { channel_read_.options = options; }

  // Returns true if the wait has begun and not yet completed or been canceled.
  bool is_pending() {
    std::lock_guard<std::mutex> lock(lock_);
    return dispatcher_ != nullptr;
  }

  // Begins asynchronously waiting for |channel| to be readable.
  // The |dispatcher| invokes the handler when the wait completes.
  // Only one dispatcher can be registered at a time. The dispatcher will
  // be considered unregistered immediately before the read handler is invoked.
  //
  // The read handler will be invoked exactly once. When the dispatcher is
  // shutting down (being destroyed), the handlers of any remaining wait
  // may be invoked with a status of |ZX_ERR_CANCELED|.
  //
  // # Errors
  //
  // ZX_ERR_INVALID_ARGS: |dispatcher| is an invalid pointer or NULL,
  // or |options| is any value other than 0.
  //
  // ZX_ERR_PEER_CLOSED: There are no available messages and the other
  // side of the channel is closed.
  //
  // ZX_ERR_BAD_STATE: There is already a dispatcher waiting on this channel.
  //
  // ZX_ERR_UNAVAILABLE: |dispatcher| is shutting down.
  zx_status_t Begin(fdf_dispatcher_t* dispatcher);

  // Cancels the wait.
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
  zx_status_t Cancel();

 protected:
  template <typename T>
  static T* Dispatch(fdf_channel_read_t* channel_read) {
    static_assert(offsetof(ChannelReadBase, channel_read_) == 0);
    auto self = reinterpret_cast<ChannelReadBase*>(channel_read);
    {
      std::lock_guard<std::mutex> lock(self->lock_);
      self->dispatcher_ = nullptr;
    }
    return static_cast<T*>(self);
  }

 private:
  fdf_channel_read_t channel_read_;

  std::mutex lock_;
  fdf_dispatcher_t* dispatcher_ __TA_GUARDED(lock_) = nullptr;
};

// An asynchronous read whose handler is bound to a |fdf::ChannelRead::Handler| function.
class ChannelRead final : public ChannelReadBase {
 public:
  // Handles completion of asynchronous read operations.
  //
  // The |status| is |ZX_OK| if the channel is ready to be read.
  // The |status| is |ZX_ERR_CANCELED| if the dispatcher was shut down before
  // the handler ran.
  using Handler = fit::function<void(fdf_dispatcher_t* dispatcher, fdf::ChannelRead* channel_read,
                                     zx_status_t status)>;

  explicit ChannelRead(fdf_handle_t channel = ZX_HANDLE_INVALID, uint32_t options = 0,
                       Handler handler = nullptr);

  ~ChannelRead();

  void set_handler(Handler handler) { handler_ = std::move(handler); }
  bool has_handler() const { return !!handler_; }

 private:
  static void CallHandler(fdf_dispatcher_t* dispatcher, fdf_channel_read_t* read,
                          zx_status_t status);

  Handler handler_;
};

}  // namespace fdf

#endif  // LIB_DRIVER_RUNTIME_INCLUDE_LIB_FDF_CPP_CHANNEL_READ_H_
