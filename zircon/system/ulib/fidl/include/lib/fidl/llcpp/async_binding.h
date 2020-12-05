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

// |AsyncBinding| objects implement the common logic for registering waits
// on channels, and unbinding. |AsyncBinding| itself composes |async_wait_t|
// which borrows the channel to wait for messages. The actual responsibilities
// of managing channel ownership falls on the various subclasses, which must
// ensure the channel is not destroyed while there are outstanding waits.
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

  AsyncBinding(async_dispatcher_t* dispatcher, const zx::unowned_channel& borrowed_channel);

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

// Base implementation shared by various specializations of
// |AsyncServerBinding<Protocol>|.
class AnyAsyncServerBinding : public AsyncBinding {
 public:
  AnyAsyncServerBinding(async_dispatcher_t* dispatcher, const zx::unowned_channel& borrowed_channel,
                        void* impl, TypeErasedServerDispatchFn dispatch_fn,
                        TypeErasedOnUnboundFn&& on_unbound_fn)
      : AsyncBinding(dispatcher, borrowed_channel),
        interface_(impl),
        dispatch_fn_(dispatch_fn),
        on_unbound_fn_(std::move(on_unbound_fn)) {}

  std::optional<UnbindInfo> Dispatch(fidl_incoming_msg_t* msg, bool* binding_released) override;

  void Close(std::shared_ptr<AsyncBinding>&& calling_ref, zx_status_t epitaph)
      __TA_EXCLUDES(lock_) {
    UnbindInternal(std::move(calling_ref), {UnbindInfo::kClose, epitaph});
  }

 protected:
  void FinishUnbindHelper(std::shared_ptr<AsyncBinding>&& calling_ref, UnbindInfo info,
                          zx::channel channel);

 private:
  friend fidl::internal::AsyncTransaction;

  void* interface_ = nullptr;
  TypeErasedServerDispatchFn dispatch_fn_ = nullptr;
  TypeErasedOnUnboundFn on_unbound_fn_ = {};
};

// The async server binding for |Protocol|.
// Contains an event sender for that protocol, which directly owns the channel.
template <typename Protocol>
class AsyncServerBinding final : public AnyAsyncServerBinding {
 public:
  static std::shared_ptr<AsyncServerBinding> Create(async_dispatcher_t* dispatcher,
                                                    zx::channel&& channel, void* impl,
                                                    TypeErasedServerDispatchFn dispatch_fn,
                                                    TypeErasedOnUnboundFn&& on_unbound_fn) {
    auto ret = std::shared_ptr<AsyncServerBinding>(new AsyncServerBinding(
        dispatcher, std::move(channel), impl, dispatch_fn, std::move(on_unbound_fn)));
    // We keep the binding alive until somebody decides to close the channel.
    ret->keep_alive_ = ret;
    return ret;
  }

  virtual ~AsyncServerBinding() = default;

  const typename Protocol::EventSender& event_sender() const { return event_sender_; }

  zx::unowned_channel channel() const { return zx::unowned_channel(event_sender_.channel()); }

 private:
  AsyncServerBinding(async_dispatcher_t* dispatcher, zx::channel&& channel, void* impl,
                     TypeErasedServerDispatchFn dispatch_fn, TypeErasedOnUnboundFn&& on_unbound_fn)
      : AnyAsyncServerBinding(dispatcher, channel.borrow(), impl, dispatch_fn,
                              std::move(on_unbound_fn)),
        event_sender_(std::move(channel)) {}

  void FinishUnbind(std::shared_ptr<AsyncBinding>&& calling_ref, UnbindInfo info) override {
    AnyAsyncServerBinding::FinishUnbindHelper(std::move(calling_ref), info,
                                              std::move(event_sender_.channel()));
  }

  // The channel is owned by AsyncServerBinding.
  typename Protocol::EventSender event_sender_;
};

// The async client binding. The client supports both synchronous and
// asynchronous calls. Because the channel lifetime must outlast the duration
// of any synchronous calls, and that synchronous calls do not yet support
// cancellation, the client binding does not own the channel directly.
// Rather, it co-owns the channel between itself and any in-flight sync
// calls, using shared pointers.
class AsyncClientBinding final : public AsyncBinding {
 public:
  static std::shared_ptr<AsyncClientBinding> Create(async_dispatcher_t* dispatcher,
                                                    std::shared_ptr<ChannelRef> channel,
                                                    std::shared_ptr<ClientBase> client,
                                                    OnClientUnboundFn&& on_unbound_fn);

  virtual ~AsyncClientBinding() = default;

 private:
  AsyncClientBinding(async_dispatcher_t* dispatcher, std::shared_ptr<ChannelRef> channel,
                     std::shared_ptr<ClientBase> client, OnClientUnboundFn&& on_unbound_fn);

  std::optional<UnbindInfo> Dispatch(fidl_incoming_msg_t* msg, bool* binding_released) override;

  void FinishUnbind(std::shared_ptr<AsyncBinding>&& calling_ref, UnbindInfo info) override;

  std::shared_ptr<ChannelRef> channel_ = nullptr;  // Strong reference to the channel.
  std::shared_ptr<ClientBase> client_;
  OnClientUnboundFn on_unbound_fn_;
};

}  // namespace internal
}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_ASYNC_BINDING_H_
