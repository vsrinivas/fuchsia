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

// The return value of various Dispatch, TryDispatch, or TypeErasedDispatch functions,
// which call into the appropriate server message handlers based on the method ordinal.
enum class __attribute__((enum_extensibility(closed))) DispatchResult {
  // The FIDL method ordinal was not recognized by the dispatch function.
  kNotFound = false,

  // The FIDL method ordinal matched one of the handlers.
  // Note that this does not necessarily mean the message was handled successfully.
  // For example, the message could fail to decode.
  kFound = true
};

namespace internal {

using TypeErasedServerDispatchFn = DispatchResult (*)(void*, fidl_incoming_msg_t*,
                                                      ::fidl::Transaction*);
using TypeErasedOnUnboundFn = fit::callback<void(void*, UnbindInfo, zx::channel)>;

class AsyncTransaction;
class ChannelRef;
class ClientBase;

class AsyncBinding : private async_wait_t {
 public:
  ~AsyncBinding() __TA_EXCLUDES(lock_);

  zx_status_t BeginWait() __TA_EXCLUDES(lock_);

  zx_status_t EnableNextDispatch() __TA_EXCLUDES(lock_);

  void Unbind(std::shared_ptr<AsyncBinding>&& calling_ref) __TA_EXCLUDES(lock_) {
    UnbindInternal(std::move(calling_ref), {UnbindInfo::kUnbind, ZX_OK});
  }

  void InternalError(std::shared_ptr<AsyncBinding>&& calling_ref, UnbindInfo error)
      __TA_EXCLUDES(lock_) {
    if (error.status == ZX_ERR_PEER_CLOSED)
      error.reason = UnbindInfo::kPeerClosed;
    UnbindInternal(std::move(calling_ref), error);
  }

  zx::unowned_channel channel() const { return zx::unowned_channel(handle()); }
  zx_handle_t handle() const { return async_wait_t::object; }

 protected:
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

  AsyncBinding(async_dispatcher_t* dispatcher, zx::unowned_channel channel);

  void MessageHandler(zx_status_t status, const zx_packet_signal_t* signal) __TA_EXCLUDES(lock_);

  // Implemented separately for client and server. If `*binding_released` is set, the calling code
  // no longer has ownership of `this` and so must not access state.
  virtual std::optional<UnbindInfo> Dispatch(fidl_incoming_msg_t* msg, bool* binding_released) = 0;

  // Used by both Close() and Unbind().
  void UnbindInternal(std::shared_ptr<AsyncBinding>&& calling_ref, UnbindInfo info)
      __TA_EXCLUDES(lock_);

  // Triggered by explicit Unbind(), channel peer closed, or other channel/dispatcher error in the
  // context of a dispatcher thread with exclusive ownership of the internal binding reference. If
  // the binding is still bound, waits for all other references to be released, sends the epitaph
  // (except for Unbind()), and invokes the error handler if specified. `calling_ref` is released.
  void OnUnbind(std::shared_ptr<AsyncBinding>&& calling_ref, UnbindInfo info,
                bool is_unbind_task = false) __TA_EXCLUDES(lock_);

  // Waits for all references to the binding to be released. Sends epitaph and invokes
  // on_unbound_fn_ as required. Behavior differs between server and client.
  virtual void FinishUnbind(std::shared_ptr<AsyncBinding>&& calling_ref, UnbindInfo info) = 0;

  async_dispatcher_t* dispatcher_ = nullptr;
  std::shared_ptr<AsyncBinding> keep_alive_ = {};
  sync_completion_t* on_delete_ = nullptr;

  std::mutex lock_;
  UnbindInfo unbind_info_ __TA_GUARDED(lock_) = {UnbindInfo::kUnbind, ZX_OK};
  bool unbind_ __TA_GUARDED(lock_) = false;
  bool begun_ __TA_GUARDED(lock_) = false;
  bool sync_unbind_ __TA_GUARDED(lock_) = false;
  bool canceled_ __TA_GUARDED(lock_) = false;
};

class AsyncServerBinding final : public AsyncBinding {
 public:
  static std::shared_ptr<AsyncServerBinding> Create(async_dispatcher_t* dispatcher,
                                                    zx::channel channel, void* impl,
                                                    TypeErasedServerDispatchFn dispatch_fn,
                                                    TypeErasedOnUnboundFn on_unbound_fn);

  virtual ~AsyncServerBinding() = default;

  void Close(std::shared_ptr<AsyncBinding>&& calling_ref, zx_status_t epitaph)
      __TA_EXCLUDES(lock_) {
    UnbindInternal(std::move(calling_ref), {UnbindInfo::kClose, epitaph});
  }

 private:
  friend fidl::internal::AsyncTransaction;

  AsyncServerBinding(async_dispatcher_t* dispatcher, zx::channel channel, void* impl,
                     TypeErasedServerDispatchFn dispatch_fn, TypeErasedOnUnboundFn on_unbound_fn);

  std::optional<UnbindInfo> Dispatch(fidl_incoming_msg_t* msg, bool* binding_released) override;

  void FinishUnbind(std::shared_ptr<AsyncBinding>&& calling_ref, UnbindInfo info) override;

  zx::channel channel_;  // The channel is owned by AsyncServerBinding.
  void* interface_ = nullptr;
  TypeErasedServerDispatchFn dispatch_fn_ = nullptr;
  TypeErasedOnUnboundFn on_unbound_fn_ = {};
};

class AsyncClientBinding final : public AsyncBinding {
 public:
  static std::shared_ptr<AsyncClientBinding> Create(async_dispatcher_t* dispatcher,
                                                    std::shared_ptr<ChannelRef> channel,
                                                    std::shared_ptr<ClientBase> client,
                                                    OnClientUnboundFn on_unbound_fn);

  virtual ~AsyncClientBinding() = default;

 private:
  AsyncClientBinding(async_dispatcher_t* dispatcher, std::shared_ptr<ChannelRef> channel,
                     std::shared_ptr<ClientBase> client, OnClientUnboundFn on_unbound_fn);

  std::optional<UnbindInfo> Dispatch(fidl_incoming_msg_t* msg, bool* binding_released) override;

  void FinishUnbind(std::shared_ptr<AsyncBinding>&& calling_ref, UnbindInfo info) override;

  std::shared_ptr<ChannelRef> channel_ = nullptr;  // Strong reference to the channel.
  std::shared_ptr<ClientBase> client_;
  OnClientUnboundFn on_unbound_fn_;
};

}  // namespace internal
}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_ASYNC_BINDING_H_
