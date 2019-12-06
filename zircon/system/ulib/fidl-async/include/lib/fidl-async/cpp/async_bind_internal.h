// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_ASYNC_CPP_ASYNC_BIND_INTERNAL_H_
#define LIB_FIDL_ASYNC_CPP_ASYNC_BIND_INTERNAL_H_

#include <lib/async/dispatcher.h>
#include <lib/async/task.h>
#include <lib/async/wait.h>
#include <lib/fidl-async/cpp/async_bind.h>
#include <lib/sync/completion.h>

#include <mutex>

namespace fidl {

namespace internal {

class AsyncTransaction;
class AsyncBinding;

using TypeErasedDispatchFn = bool (*)(void*, fidl_msg_t*, ::fidl::Transaction*);
using TypeErasedOnUnboundFn = fit::callback<void(void*, UnboundReason, zx::channel)>;

class AsyncBinding {
 public:
  // Creates a binding that remains bound until either it is explicitly unbound via |Unbind|,
  // a peer close is received in the channel from the remote end, or all transactions generated
  // from it are destructed and an error occurred (either Close() is called from a transaction or
  // an internal error like not being able to write to the channel occur).
  // The binding is destroyed once no more references are held, including the one returned by
  // this static method.
  static std::shared_ptr<AsyncBinding> CreateSelfManagedBinding(
      async_dispatcher_t* dispatcher, zx::channel channel, void* impl,
      TypeErasedDispatchFn dispatch_fn, TypeErasedOnUnboundFn on_unbound_fn);

  ~AsyncBinding();

  zx_status_t BeginWait() __TA_EXCLUDES(lock_);

  zx_status_t EnableNextDispatch() __TA_EXCLUDES(lock_);

  void Unbind(std::shared_ptr<AsyncBinding>&& calling_ref) __TA_EXCLUDES(lock_) {
    UnbindInternal(std::move(calling_ref), nullptr);
  }

  void Close(std::shared_ptr<AsyncBinding>&& calling_ref,
             zx_status_t epitaph) __TA_EXCLUDES(lock_) {
    UnbindInternal(std::move(calling_ref), &epitaph);
  }

 protected:
  explicit AsyncBinding(async_dispatcher_t* dispatcher, zx::channel channel, void* impl,
                        TypeErasedDispatchFn dispatch_fn,
                        TypeErasedOnUnboundFn on_unbound_fn);

 private:
  friend fidl::internal::AsyncTransaction;
  friend fidl::BindingRef;

  struct UnboundTask {
    async_task_t task;
    TypeErasedOnUnboundFn on_unbound_fn;
    void* intf;
    zx::channel channel;
    UnboundReason reason;
  };

  static void OnMessage(async_dispatcher_t* dispatcher, async_wait_t* wait, zx_status_t status,
                        const zx_packet_signal_t* signal) {
    static_assert(std::is_standard_layout<AsyncBinding>::value, "Need offsetof.");
    static_assert(offsetof(AsyncBinding, wait_) == 0, "Cast async_wait_t* to AsyncBinding*.");
    reinterpret_cast<AsyncBinding*>(wait)->MessageHandler(status, signal);
  }

  static void OnUnboundTask(async_dispatcher_t* dispatcher, async_task_t* task,
                              zx_status_t status) {
    static_assert(std::is_standard_layout<UnboundTask>::value, "Need offsetof.");
    static_assert(offsetof(UnboundTask, task) == 0, "Cast async_task_t* to UnboundTask*.");
    auto* unbound_task = reinterpret_cast<UnboundTask*>(task);
    unbound_task->on_unbound_fn(unbound_task->intf, unbound_task->reason,
                                std::move(unbound_task->channel));
    delete unbound_task;
  }

  zx::unowned_channel channel() const { return zx::unowned_channel(channel_); }

  void MessageHandler(zx_status_t status, const zx_packet_signal_t* signal) __TA_EXCLUDES(lock_);

  // Used by both Close() and Unbind().
  void UnbindInternal(std::shared_ptr<AsyncBinding>&& calling_ref,
                      zx_status_t* epitaph) __TA_EXCLUDES(lock_);

  // Triggered by explicit Unbind(), channel peer closed, or other channel/dispatcher error in the
  // context of a dispatcher thread with exclusive ownership of the internal binding reference. If
  // the binding is still bound, waits for all other references to be released, sends the epitaph
  // (except for Unbind()), and invokes the error handler if specified.
  void OnUnbind(zx_status_t epitaph, UnboundReason reason) __TA_EXCLUDES(lock_);

  // Destroys calling_ref and waits for the release of any other outstanding references to the
  // binding. Recovers the channel endpoint if requested.
  zx::channel WaitForDelete(std::shared_ptr<AsyncBinding>&& calling_ref, bool get_channel);

  // Invokes OnUnbind() with the appropriate arguments based on the status.
  void OnEnableNextDispatchError(zx_status_t error);

  // First member of struct so an async_wait_t* can be casted to its containing AsyncBinding*.
  async_wait_t wait_ __TA_GUARDED(lock_);

  async_dispatcher_t* dispatcher_ = nullptr;
  sync_completion_t* on_delete_ = nullptr;
  zx::channel channel_ = {};
  void* interface_ = nullptr;
  TypeErasedDispatchFn dispatch_fn_ = {};
  TypeErasedOnUnboundFn on_unbound_fn_ = {};
  std::shared_ptr<AsyncBinding> keep_alive_ = {};
  zx::channel* out_channel_ = nullptr;

  std::mutex lock_;
  struct { zx_status_t status; bool send; } epitaph_ __TA_GUARDED(lock_) = {ZX_OK, false};
  bool unbind_ __TA_GUARDED(lock_) = false;
  bool begun_ __TA_GUARDED(lock_) = false;
};

}  // namespace internal

}  // namespace fidl

#endif  // LIB_FIDL_ASYNC_CPP_ASYNC_BIND_INTERNAL_H_
