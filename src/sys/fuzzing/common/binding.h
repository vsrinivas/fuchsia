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
#include <threads.h>
#include <zircon/status.h>

#include <mutex>

#include "src/lib/fxl/synchronization/thread_annotations.h"
#include "src/sys/fuzzing/common/dispatcher.h"
#include "src/sys/fuzzing/common/signal-coordinator.h"

namespace fuzzing {

// This class wraps |fidl::Binding| in an object that can be created and destroyed on both
// dispatcher and non-dispatcher threads, allowing for easier RAII-like semantics.
template <typename T>
class Binding {
 public:
  Binding(T* t, std::shared_ptr<Dispatcher> dispatcher)
      : binding_(t), dispatcher_(std::move(dispatcher)) {
    FX_CHECK(dispatcher_);
  }

  // The HLCPP bindings are thread-hostile. In particular, they can only be safely unbound from the
  // dispatcher thread, or run the risk of racing being unbound by a peer closure.
  virtual ~Binding() { Unbind(); }

  const std::shared_ptr<Dispatcher>& dispatcher() const { return dispatcher_; }
  bool is_bound() const { return binding_.is_bound(); }

  // Creates a channel, binds it to this object, and returns the other end in an InterfaceHandle.
  // This can be called from a non-dispatch thread. Does nothing if already bound.
  fidl::InterfaceHandle<T> NewBinding() FXL_LOCKS_EXCLUDED(mutex_) {
    fidl::InterfaceHandle<T> client;
    Bind(client.NewRequest());
    return client;
  }

  // Binds the FIDL interface request to this object. This can be called from a non-dispatch thread.
  zx_status_t Bind(fidl::InterfaceRequest<T> request) FXL_LOCKS_EXCLUDED(mutex_) {
    return Bind(request.TakeChannel());
  }

  // Binds the channel to this object. This can be called from a non-dispatch thread. If this object
  // is currently bound, it will first unbind. Returns immediately with |ZX_ERR_BAD_STATE| if a call
  // to |Bind| and |Unbind| is outstanding.
  zx_status_t Bind(zx::channel channel) FXL_LOCKS_EXCLUDED(mutex_) {
    Unbind();
    bool update_posted = false;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      switch (state_) {
        case kUnbound:
          channel_ = std::move(channel);
          state_ = kBinding;
          update_posted = MaybePostUpdate();
          break;
        default:
          return ZX_ERR_BAD_STATE;
      }
    }
    return AwaitUpdate(update_posted);
  }

  // Unbinds (and closes) the underlying channel from this object. This can be called from a
  // non-dispatch thread. Does nothing if not bound. Calling |Unbind| while a call to |Bind| is
  // outstanding effectively cancels the latter.
  void Unbind() FXL_LOCKS_EXCLUDED(mutex_) {
    bool update_posted = false;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      switch (state_) {
        case kUnbound:
          return;
        case kBinding:
          // Update already queued; change what it will do.
          state_ = kUnbinding;
          break;
        case kBound:
          state_ = kUnbinding;
          update_posted = MaybePostUpdate();
          break;
        case kUnbinding:
          // Update already queued.
          break;
      }
    }
    AwaitUpdate(update_posted);
  }

 private:
  // Attempts to post a call to |Update| on the dispatcher thread. Returns false if this is being
  // called on the dispatcher thread or if the dispatcher is shutting down; otherwise returns true.
  // The return value should be passed to |AwaitUpdate|.
  bool MaybePostUpdate() FXL_REQUIRE(mutex_) {
    if (thrd_equal(dispatcher_->thrd(), thrd_current())) {
      return false;
    }
    sync_completion_reset(&sync_);
    auto status = async::PostTask(dispatcher_->get(), [this]() {
      Update();
      sync_completion_signal(&sync_);
    });
    return status == ZX_OK;
  }

  // Binds or unbinds the channel based on the current state. This should be called on the
  // dispatcher thread only to avoid racing message arrival.
  void Update() FXL_LOCKS_EXCLUDED(mutex_) {
    std::lock_guard<std::mutex> lock(mutex_);
    switch (state_) {
      case kBinding:
        result_ = binding_.Bind(std::move(channel_), dispatcher_->get());
        if (result_ != ZX_OK) {
          state_ = kUnbinding;
        }
        break;
      case kUnbinding:
        if (binding_.is_bound()) {
          binding_.Unbind();
        }
        result_ = ZX_OK;
        break;
      case kUnbound:
      case kBound:
        FX_NOTREACHED();
    }
  }

  // Ensures |Update| has been called before returning its result. If |update_posted| is false, it
  // calls |Update| directly; otherwise it waits until the task posted by |MaybePostUpdate| has
  // completed.
  zx_status_t AwaitUpdate(bool update_posted) FXL_LOCKS_EXCLUDED(mutex_) {
    if (!update_posted) {
      Update();
    } else {
      sync_completion_wait(&sync_, ZX_TIME_INFINITE);
    }
    std::lock_guard<std::mutex> lock(mutex_);
    switch (state_) {
      case kBinding:
        state_ = kBound;
        break;
      case kUnbinding:
        state_ = kUnbound;
        break;
      case kUnbound:
      case kBound:
        FX_NOTREACHED();
    }
    channel_.reset();
    return result_;
  }

  fidl::Binding<T> binding_;
  std::shared_ptr<Dispatcher> dispatcher_;

  std::mutex mutex_;
  enum : uint8_t {
    kUnbound,
    kBinding,
    kBound,
    kUnbinding,
  } state_ FXL_GUARDED_BY(mutex_) = kUnbound;
  zx::channel channel_ FXL_GUARDED_BY(mutex_);
  zx_status_t result_ FXL_GUARDED_BY(mutex_) = ZX_OK;
  sync_completion_t sync_;
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_COMMON_BINDING_H_
