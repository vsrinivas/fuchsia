// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_COMMON_BINDING_H_
#define SRC_SYS_FUZZING_COMMON_BINDING_H_

#include <lib/async/cpp/task.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/sync/completion.h>
#include <lib/syslog/cpp/macros.h>

#include "src/sys/fuzzing/common/signal-coordinator.h"

namespace fuzzing {

// This class wraps |fidl::Binding| in an object that can be created and destroyed on a
// non-dispatcher thread, allowing for RAII-like semantics.
template <typename T>
class Binding final {
 public:
  explicit Binding(T* t) : binding_(t) {}

  // The HLCPP bindings are thread-hostile. In particular, they can only be safely unbound from the
  // dispatcher thread, or run the risk of racing being unbound by a peer closure.
  ~Binding() { Unbind(); }

  async_dispatcher_t* dispatcher() const { return dispatcher_; }
  void set_dispatcher(async_dispatcher_t* dispatcher) { dispatcher_ = dispatcher; }
  void set_error_handler(fit::function<void(zx_status_t)> error_handler) {
    binding_.set_error_handler(std::move(error_handler));
  }

  bool is_bound() const { return binding_.is_bound(); }

  // Creates a channel, binds it to this object, and returns the other end in an InterfaceHandle.
  // This can be called from a non-dispatch thread. Does nothing if already bound.
  fidl::InterfaceHandle<T> NewBinding() {
    fidl::InterfaceHandle<T> client;
    Bind(client.NewRequest());
    return client;
  }

  // Binds the FIDL interface request to this object. This can be called from a non-dispatch thread.
  // Does nothing if already bound.
  zx_status_t Bind(fidl::InterfaceRequest<T> request) { return Bind(request.TakeChannel()); }

  // Binds the channel to this object. This can be called from a non-dispatch thread.
  // Does nothing if already bound.
  zx_status_t Bind(zx::channel channel) {
    Unbind();
    return RunSyncIf(kUnbound, [this, channel = std::move(channel)]() mutable {
      return binding_.Bind(std::move(channel), dispatcher_);
    });
  }

  // Unbinds (and closes) the underlying channel from this object. This can be called from a
  // non-dispatch thread. Does nothing if not bound.
  zx_status_t Unbind() {
    return RunSyncIf(kBound, [this]() {
      if (binding_.is_bound()) {
        binding_.Unbind();
      }
      return ZX_OK;
    });
  }

 private:
  enum State : uint8_t {
    kUnbound,
    kBinding,
    kBound,
    kUnbinding,
  };

  // Get the next expected state for a given |state|.
  State NextState(State state) {
    switch (state) {
      case kUnbound:
        return kBinding;
      case kBinding:
        return kBound;
      case kBound:
        return kUnbinding;
      case kUnbinding:
        return kUnbound;
    }
  }

  // If the current state matches |initial|, runs a |task| synchronously on this object's
  // asynchronous dispatcher; otherwise, does nothing.
  zx_status_t RunSyncIf(State initial, fit::function<zx_status_t()> task) {
    if (!dispatcher_) {
      return ZX_ERR_BAD_STATE;
    }
    uint8_t expected = initial;
    auto next = NextState(initial);
    if (!state_.compare_exchange_strong(expected, next)) {
      return ZX_ERR_BAD_STATE;
    }
    sync_completion_t sync;
    zx_status_t task_result;
    auto status = async::PostTask(dispatcher_, [&sync, &task_result, task = std::move(task)]() {
      task_result = task();
      sync_completion_signal(&sync);
    });
    if (status != ZX_OK) {
      state_ = initial;
      return status;
    }
    sync_completion_wait(&sync, ZX_TIME_INFINITE);
    state_ = NextState(next);
    return task_result;
  }

  fidl::Binding<T> binding_;
  std::atomic<uint8_t> state_ = kUnbound;
  async_dispatcher_t* dispatcher_ = nullptr;
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_COMMON_BINDING_H_
