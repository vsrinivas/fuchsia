// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_COMMON_BINDING_H_
#define SRC_SYS_FUZZING_COMMON_BINDING_H_

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
  Binding(T* t) : binding_(t), dispatcher_(std::make_shared<Dispatcher>()) {}

  // The HLCPP bindings are thread-hostile. In particular, they can only be safely unbound from the
  // dispatcher thread, or run the risk of racing being unbound by a peer closure.
  virtual ~Binding() { Unbind(); }

  const std::shared_ptr<Dispatcher>& dispatcher() const { return dispatcher_; }
  bool is_dispatcher_thread() const { return thrd_equal(dispatcher_->thrd(), thrd_current()); }
  bool is_bound() const { return binding_.is_bound(); }

  // Posts a task to the binding's associated FIDL dispatcher.
  void PostTask(fit::closure&& task) {
    dispatcher_->PostTask([task = std::move(task)]() { task(); });
  }

  // Creates a channel, binds it to this object, and returns the other end in an InterfaceHandle.
  // This can be called from a non-dispatch thread. Does nothing if already bound.
  fidl::InterfaceHandle<T> NewBinding() {
    fidl::InterfaceHandle<T> client;
    Bind(client.NewRequest());
    return client;
  }

  // Binds the FIDL interface request to this object. This can be called from a non-dispatch thread.
  void Bind(fidl::InterfaceRequest<T> request) { Bind(request.TakeChannel()); }

  // Binds the channel to this object. This can be called from a non-dispatch thread.
  void Bind(zx::channel channel) {
    if (is_dispatcher_thread()) {
      auto status = binding_.Bind(std::move(channel), dispatcher_->get());
      FX_CHECK(status == ZX_OK) << "Bind: " << zx_status_get_string(status);
      return;
    }
    SyncWait sync;
    PostTask([this, &sync, channel = std::move(channel)]() mutable {
      auto status = binding_.Bind(std::move(channel));
      FX_CHECK(status == ZX_OK) << "Bind: " << zx_status_get_string(status);
      sync.Signal();
    });
    sync.WaitFor("dispatcher to complete binding");
  }

  // Unbinds (and closes) the underlying channel from this object. This can be called from a
  // non-dispatch thread. Does nothing if not bound.
  void Unbind() {
    if (!binding_.is_bound()) {
      return;
    }
    if (is_dispatcher_thread()) {
      binding_.Unbind();
      return;
    }
    SyncWait sync;
    PostTask([this, &sync]() {
      binding_.Unbind();
      sync.Signal();
    });
    sync.WaitFor("dispatcher to complete unbinding");
  }

  // Blocks until the underlying channel is unbound and closed.
  zx_status_t AwaitClose() {
    Waiter waiter = [this](zx::time deadline) {
      const auto& channel = binding_.channel();
      return channel.wait_one(ZX_CHANNEL_PEER_CLOSED, deadline, nullptr);
    };
    auto status = WaitFor("binding to close", &waiter);
    switch (status) {
      case ZX_ERR_BAD_HANDLE:
      case ZX_ERR_CANCELED:
        // The local end of the channel was closed before or during this call.
        return ZX_OK;
      default:
        return status;
    }
  }

 private:
  fidl::Binding<T> binding_;
  std::shared_ptr<Dispatcher> dispatcher_;
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_COMMON_BINDING_H_
