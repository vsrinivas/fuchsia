// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_ASYNC_BINDING_H_
#define LIB_FIDL_LLCPP_ASYNC_BINDING_H_

#include <lib/async/dispatcher.h>
#include <lib/async/task.h>
#include <lib/async/wait.h>
#include <lib/fidl/llcpp/transaction.h>
#include <lib/fidl/llcpp/types.h>
#include <lib/fit/function.h>
#include <lib/fit/result.h>
#include <lib/sync/completion.h>
#include <lib/zx/channel.h>
#include <zircon/fidl.h>

#include <mutex>
#include <optional>

namespace fidl {

namespace internal {

using TypeErasedServerDispatchFn = bool (*)(void*, fidl_msg_t*, ::fidl::Transaction*);
using TypeErasedOnUnboundFn = fit::callback<void(void*, UnbindInfo, zx::channel)>;

class AsyncTransaction;
class ClientBase;

class AsyncBinding : private async_wait_t {
 public:
  using DispatchFn =
      fit::function<std::optional<UnbindInfo>(std::shared_ptr<AsyncBinding>&, fidl_msg_t*, bool*)>;

  // Creates a binding that remains bound until either it is explicitly unbound via |Unbind|,
  // a peer close is received in the channel from the remote end, or all transactions generated
  // from it are destructed and an error occurred (either Close() is called from a transaction or
  // an internal error like not being able to write to the channel occur).
  //
  // The binding is destroyed once no more references are held, including the one returned by
  // this static method.
  //
  // NOTE: AsyncBinding takes sole ownership of the channel.
  static std::shared_ptr<AsyncBinding> CreateServerBinding(async_dispatcher_t* dispatcher,
                                                           zx_handle_t channel, void* impl,
                                                           TypeErasedServerDispatchFn dispatch_fn,
                                                           TypeErasedOnUnboundFn on_unbound_fn);

  // Creates a client binding which differs from a server binding in the following:
  // * AsyncBinding borrows ownership of the channel from a higher-level client class
  //     - The borrow is returned implicitly via on_unbound_fn
  // * Close() is invoked on receipt of epitaph rather than itself sending an epitaph
  static std::shared_ptr<AsyncBinding> CreateClientBinding(async_dispatcher_t* dispatcher,
                                                           zx_handle_t channel, void* impl,
                                                           DispatchFn dispatch_fn,
                                                           TypeErasedOnUnboundFn on_unbound_fn);

  // Sends an epitaph if required. If an unbound hook was provided, posts a task to the dispatcher
  // to execute it.
  ~AsyncBinding() __TA_EXCLUDES(lock_);

  zx_status_t BeginWait() __TA_EXCLUDES(lock_);

  zx_status_t EnableNextDispatch() __TA_EXCLUDES(lock_);

  void Unbind(std::shared_ptr<AsyncBinding>&& calling_ref) __TA_EXCLUDES(lock_) {
    UnbindInternal(std::move(calling_ref), {UnbindInfo::kUnbind, ZX_OK});
  }

  void Close(std::shared_ptr<AsyncBinding>&& calling_ref, zx_status_t epitaph)
      __TA_EXCLUDES(lock_) {
    ZX_ASSERT(is_server_);
    UnbindInternal(std::move(calling_ref), {UnbindInfo::kClose, epitaph});
  }

  void InternalError(std::shared_ptr<AsyncBinding>&& calling_ref, UnbindInfo error)
      __TA_EXCLUDES(lock_) {
    if (error.status == ZX_ERR_PEER_CLOSED)
      error.reason = UnbindInfo::kPeerClosed;
    UnbindInternal(std::move(calling_ref), error);
  }

  zx::unowned_channel channel() const { return zx::unowned_channel(channel_); }
  zx_handle_t handle() const { return channel_; }

 protected:
  AsyncBinding(async_dispatcher_t* dispatcher, zx_handle_t channel, void* impl, bool is_server,
               TypeErasedOnUnboundFn on_unbound_fn, DispatchFn dispatch_fn);

 private:
  friend fidl::internal::AsyncTransaction;

  struct UnbindTask {
    async_task_t task;
    std::weak_ptr<AsyncBinding> binding;
  };

  static void OnMessage(async_dispatcher_t* dispatcher, async_wait_t* wait, zx_status_t status,
                        const zx_packet_signal_t* signal) {
    static_cast<AsyncBinding*>(wait)->MessageHandler(status, signal);
  }

  static void OnUnbindTask(async_dispatcher_t*, async_task_t* task, zx_status_t status) {
    static_assert(std::is_standard_layout<UnbindTask>::value, "Need offsetof.");
    static_assert(offsetof(UnbindTask, task) == 0, "Cast async_task_t* to UnbindTask*.");
    auto* unbind_task = reinterpret_cast<UnbindTask*>(task);
    if (auto binding = unbind_task->binding.lock())
      binding->OnUnbind(std::move(binding), {UnbindInfo::kUnbind, ZX_OK}, true);
    delete unbind_task;
  }

  void MessageHandler(zx_status_t status, const zx_packet_signal_t* signal) __TA_EXCLUDES(lock_);

  // Used by both Close() and Unbind().
  void UnbindInternal(std::shared_ptr<AsyncBinding>&& calling_ref, UnbindInfo info)
      __TA_EXCLUDES(lock_);

  // Triggered by explicit Unbind(), channel peer closed, or other channel/dispatcher error in the
  // context of a dispatcher thread with exclusive ownership of the internal binding reference. If
  // the binding is still bound, waits for all other references to be released, sends the epitaph
  // (except for Unbind()), and invokes the error handler if specified. `calling_ref` is released.
  void OnUnbind(std::shared_ptr<AsyncBinding>&& calling_ref, UnbindInfo info,
                bool is_unbind_task = false) __TA_EXCLUDES(lock_);

  async_dispatcher_t* dispatcher_ = nullptr;
  // If is_server_, channel_ is owned by AsyncBinding. Otherwise, it is shared with an external
  // entity (ChannelRefTracker).
  zx_handle_t channel_ = ZX_HANDLE_INVALID;
  void* interface_ = nullptr;
  TypeErasedOnUnboundFn on_unbound_fn_ = {};
  const DispatchFn dispatch_fn_;
  std::shared_ptr<AsyncBinding> keep_alive_ = {};
  const bool is_server_;
  sync_completion_t* on_delete_ = nullptr;

  std::mutex lock_;
  UnbindInfo unbind_info_ __TA_GUARDED(lock_) = {UnbindInfo::kUnbind, ZX_OK};
  bool unbind_ __TA_GUARDED(lock_) = false;
  bool begun_ __TA_GUARDED(lock_) = false;
  bool sync_unbind_ __TA_GUARDED(lock_) = false;
  bool canceled_ __TA_GUARDED(lock_) = false;
};

}  // namespace internal
}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_ASYNC_BINDING_H_
