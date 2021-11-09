// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_DRIVER_RUNTIME_INCLUDE_LIB_FDF_CPP_DISPATCHER_H_
#define LIB_DRIVER_RUNTIME_INCLUDE_LIB_FDF_CPP_DISPATCHER_H_

#include <lib/fdf/dispatcher.h>
#include <lib/stdcompat/string_view.h>
#include <lib/zx/status.h>

#include <string>

namespace fdf {

// Usage Notes:
//
// C++ wrapper for a dispatcher, with RAII semantics. Automatically shuts down
// the dispatcher when it goes out of scope.
//
// Example:
//   TODO(fxb/85946): update this once scheduler_role is supported.
//   const char* scheduler_role = "";
//
//   auto dispatcher = fdf::Dispatcher::Create(0, scheduler_role);
//
//   fdf::ChannelRead channel_read;
//   ...
//    zx_status_t status = channel_read->Begin(dispatcher.get());
//
//   // The dispatcher will call the channel_read handler when ready.
//
class Dispatcher {
 public:
  // Creates a dispatcher.
  //
  // |options| provides configuration for the dispatcher.
  // See also |FDF_DISPATCHER_OPTION_UNSYNCHRONIZED| and |FDF_DISPATCHER_OPTION_ALLOW_SYNC_CALLS|.
  //
  // |scheduler_role| is a hint. It may or not impact the priority the work scheduler against the
  // dispatcher is handled at. It may or may not impact the ability for other drivers to share
  // zircon threads with the dispatcher.
  //
  // This must be called from a thread managed by the driver runtime.
  static zx::status<Dispatcher> Create(uint32_t options, cpp17::string_view scheduler_role) {
    fdf_dispatcher_t* dispatcher;
    zx_status_t status =
        fdf_dispatcher_create(options, scheduler_role.data(), scheduler_role.size(), &dispatcher);
    if (status != ZX_OK) {
      return zx::error(status);
    }
    return zx::ok(Dispatcher(dispatcher));
  }

  explicit Dispatcher(fdf_dispatcher_t* dispatcher = nullptr) : dispatcher_(dispatcher) {}

  Dispatcher(const Dispatcher& to_copy) = delete;
  Dispatcher& operator=(const Dispatcher& other) = delete;

  Dispatcher(Dispatcher&& other) noexcept : Dispatcher(other.release()) {}
  Dispatcher& operator=(Dispatcher&& other) noexcept {
    dispatcher_ = other.release();
    return *this;
  }

  ~Dispatcher() {
    if (dispatcher_) {
      fdf_dispatcher_destroy(dispatcher_);
    }
  }

  fdf_dispatcher_t* get() { return dispatcher_; }

  fdf_dispatcher_t* release() {
    fdf_dispatcher_t* ret = dispatcher_;
    dispatcher_ = nullptr;
    return ret;
  }

  // Gets the dispatcher's asynchronous dispatch interface.
  async_dispatcher_t* async_dispatcher() {
    return dispatcher_ ? fdf_dispatcher_get_async_dispatcher(dispatcher_) : nullptr;
  }

 private:
  fdf_dispatcher_t* dispatcher_;
};

}  // namespace fdf

#endif  // LIB_DRIVER_RUNTIME_INCLUDE_LIB_FDF_CPP_DISPATCHER_H_
