// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_ASYNC_BINDING_H_
#define LIB_FIDL_LLCPP_ASYNC_BINDING_H_

#include <lib/async/dispatcher.h>
#include <lib/async/task.h>
#include <lib/async/wait.h>
#include <lib/fidl/epitaph.h>
#include <lib/fidl/llcpp/extract_resource_on_destruction.h>
#include <lib/fidl/llcpp/internal/client_details.h>
#include <lib/fidl/llcpp/internal/thread_checker.h>
#include <lib/fidl/llcpp/message.h>
#include <lib/fidl/llcpp/result.h>
#include <lib/fidl/llcpp/server_end.h>
#include <lib/fidl/llcpp/transaction.h>
#include <lib/fidl/llcpp/wire_messaging.h>
#include <lib/fit/function.h>
#include <lib/stdcompat/variant.h>
#include <lib/sync/completion.h>
#include <lib/zx/channel.h>
#include <zircon/fidl.h>

#include <mutex>
#include <optional>

namespace fidl {

// The return value of various Dispatch, TryDispatch, or
// |IncomingMessageDispatcher::dispatch_message| functions, which call into the
// appropriate server message handlers based on the method ordinal.
enum class __attribute__((enum_extensibility(closed))) DispatchResult {
  // The FIDL method ordinal was not recognized by the dispatch function.
  kNotFound = false,

  // The FIDL method ordinal matched one of the handlers.
  // Note that this does not necessarily mean the message was handled successfully.
  // For example, the message could fail to decode.
  kFound = true
};

namespace internal {

// |AsyncBinding| objects implement the common logic for registering waits
// on channels, and teardown. |AsyncBinding| itself composes |async_wait_t|
// which borrows the channel to wait for messages. The actual responsibilities
// of managing channel ownership falls on the various subclasses, which must
// ensure the channel is not destroyed while there are outstanding waits.
//
// |AsyncBinding| objects are always managed by a |std::shared_ptr|. Messaging
// APIs typically promote a corresponding |std::weak_ptr| briefly when they need
// to write to the transport, and gracefully report an *unbound* error if the
// binding has been destroyed.
class AsyncBinding : private async_wait_t {
 public:
  ~AsyncBinding() __TA_EXCLUDES(lock_) = default;

  void BeginFirstWait() __TA_EXCLUDES(lock_);

  // Checks for the need to teardown and registers the next wait in one critical
  // section:
  //
  // - If we are already in |Lifecycle::MustTeardown|, early return an error.
  // - Otherwise, adds the next wait to the dispatcher, recording any error in
  //   |lifecycle_|.
  //
  // When used from the message handler, the message handler should immediately
  // perform teardown when this method returns an error.
  zx_status_t CheckForTeardownAndBeginNextWait() __TA_EXCLUDES(lock_);

  // |StartTeardownWithInfo| attempts to post exactly one task to drive the
  // teardown process. This enum reflects the result of posting the task.
  enum class TeardownTaskPostingResult {
    kOk,

    // The binding is already tearing down, so we should not post another.
    kRacedWithInProgressTeardown,

    // Failed to post the task to the dispatcher. This is usually due to
    // the dispatcher already shutting down.
    //
    // If the user shuts down the dispatcher when the binding is already
    // established and monitoring incoming messages, then whichever thread
    // that was monitoring incoming messages would drive the teardown
    // process.
    //
    // If the user calls |BindServer| on a shut-down dispatcher, there is
    // no available thread to drive the teardown process and report errors.
    // We consider it a programming error, and panic right away. Note that
    // this is inherently racy i.e. shutting down dispatchers while trying
    // to also bind new channels to the same dispatcher, so we may want to
    // reevaluate whether shutting down the dispatcher is an error whenever
    // there is any active binding (fxbug.dev/NNNNN).
    kDispatcherError,
  };

  // Initiates teardown with the provided |info| as reason.
  TeardownTaskPostingResult StartTeardownWithInfo(std::shared_ptr<AsyncBinding>&& calling_ref,
                                                  UnbindInfo info) __TA_EXCLUDES(thread_checker_)
      __TA_EXCLUDES(lock_);

  void StartTeardown(std::shared_ptr<AsyncBinding>&& calling_ref) __TA_EXCLUDES(thread_checker_)
      __TA_EXCLUDES(lock_) {
    StartTeardownWithInfo(std::move(calling_ref), ::fidl::UnbindInfo::Unbind());
  }

  zx::unowned_channel channel() const { return zx::unowned_channel(handle()); }
  zx_handle_t handle() const { return async_wait_t::object; }

 protected:
  AsyncBinding(async_dispatcher_t* dispatcher, const zx::unowned_channel& borrowed_channel,
               ThreadingPolicy threading_policy);

  static void OnMessage(async_dispatcher_t* dispatcher, async_wait_t* wait, zx_status_t status,
                        const zx_packet_signal_t* signal) {
    static_cast<AsyncBinding*>(wait)->MessageHandler(status, signal);
  }

  // Common message handling entrypoint shared by both client and server bindings.
  void MessageHandler(zx_status_t status, const zx_packet_signal_t* signal)
      __TA_EXCLUDES(thread_checker_) __TA_EXCLUDES(lock_);

  // Dispatches a generic incoming message.
  //
  // ## Message ownership
  //
  // The client async binding should invoke the matching response handler or
  // event handler, if one is found. |msg| is then consumed, regardless of
  // decoding error.
  //
  // The server async binding should invoke the matching request handler if
  // one is found. |msg| is then consumed, regardless of decoding error.
  //
  // In other cases (e.g. unknown message, epitaph), |msg| is not consumed.
  //
  // The caller should simply ignore the |fidl::IncomingMessage| object once
  // it is passed to this function, letting RAII clean up handles as needed.
  //
  // ## Return value
  //
  // If errors occur during dispatching, the function will return an
  // |UnbindInfo| describing the error. Otherwise, it will return
  // |std::nullopt|.
  //
  // If `*binding_released` is set, the calling code no longer has ownership of
  // this |AsyncBinding| object and so must not access its state.
  virtual std::optional<UnbindInfo> Dispatch(fidl::IncomingMessage& msg, bool* binding_released)
      __TA_REQUIRES(thread_checker_) = 0;

  async_dispatcher_t* dispatcher_ = nullptr;

  // A circular reference that represents the dispatcher ownership of the
  // |AsyncBinding|. When |lifecycle_| is |Lifecycle::Bound|, all mutations of
  // |keep_alive_| must happen on a dispatcher thread.
  std::shared_ptr<AsyncBinding> keep_alive_ = {};

 private:
  // Synchronously perform teardown in the context of a dispatcher thread with
  // exclusive access of the internal binding reference.
  //
  // If |lifecycle_| is not yet in |MustTeardown|, |info| must be present to
  // specify the teardown reason.
  void PerformTeardown(cpp17::optional<UnbindInfo> info) __TA_REQUIRES(thread_checker_)
      __TA_EXCLUDES(lock_);

  // Override |FinishTeardown| to perform cleanup work at the final stage of
  // binding teardown.
  //
  // An important guarantee of this function is up-call exclusion: there will be
  // no parallel up-calls to user objects at the point of invocation.
  //
  // Proof that |AsyncBinding| upholds this property:
  //
  // The runtime arranges |AsyncBinding::MessageHandler| to be run when an
  // incoming message arrives, where it would make up-calls to handle the
  // message. There will be at most one pending handler registration at any
  // time. |StartTeardownWithInfo| attempts to de-register this interest for a
  // new message (`async_cancel_wait`). There are two possible outcomes:
  //
  // - If the cancellation succeeds, it follows that there no up-calls since the
  //   |MessageHandler| will no longer run.
  //
  // - If the cancellation fails, the |MessageHandler| may already be running,
  //   or has entered an imminent state where it is too late to cancel. In
  //   either case, |MessageHandler| will detect that teardown is in order when
  //   it is re-registering the wait, and will run the teardown task right away.
  //   There is no parallel up-calls because the |MessageHandler| itself is
  //   synchronously preoccupied with teardown.
  //
  // |FinishTeardown| will be invoked on a dispatcher thread if the dispatcher is
  // running, and will be invoked on the thread that is calling shutdown if the
  // dispatcher is shutting down.
  virtual void FinishTeardown(std::shared_ptr<AsyncBinding>&& calling_ref, UnbindInfo info)
      __TA_REQUIRES(thread_checker_) = 0;

  // |thread_checker_| records the thread ID of constructing thread and checks
  // that required operations run on that thread when the threading policy calls
  // for it.
  //
  // |thread_checker_| is no-op in release builds, and may be completely
  // optimized out.
  [[no_unique_address]] ThreadChecker thread_checker_;

  // A lock protecting the binding |lifecycle|.
  std::mutex lock_;

  // |Lifecycle| is a state machine that captures the lifecycle of a binding.
  //
  // A binding transitions through the states in their listed order, and may
  // be allowed to skip forward certain states as noted below.
  class Lifecycle {
   public:
    enum LifecycleState {
      // The binding is created, but message dispatch has not started.
      //
      // A binding always starts in this state.
      kCreated = 0,

      // The first |async_wait_t| has been registered with the dispatcher
      // i.e. the first wait has begun.
      kBound,

      // A fatal error happened or the user explicitly requested teardown.
      // The binding must stop message processing at its earliest convenience.
      kMustTeardown,

      // The last stage of the binding before its destruction. The only
      // allowed operation is to call |FinishTeardown| to notify the user.
      kTorndown
    };

    // Transitions to the |kBound| state.
    //
    // One may only transition from |kCreated| to this state.
    void TransitionToBound();

    // Indicates that waits should no longer be added to the dispatcher.
    //
    // |info| contains the reason for teardown.
    //
    // One may transition to this state from |kCreated|, |kBound|, or
    // |kMustTeardown|. When transitioning from |kMustTeardown| to itself, the
    // previous |info| value is preserved. In other words, the earliest error is
    // propagated to the user.
    void TransitionToMustTeardown(fidl::UnbindInfo info);

    // Transitions to the |kTorndown| state.
    //
    // One may only transition to this state from |kMustTeardown|.
    //
    // Returns the stored reason for teardown.
    fidl::UnbindInfo TransitionToTorndown();

    // Returns whether the binding _ever_ entered the |kBound| state.
    bool DidBecomeBound() const { return did_enter_bound_; }

    // Checks if the binding is in the specified |state|.
    bool Is(LifecycleState state) { return state_ == state; }

    // Returns the current state as an enumeration.
    LifecycleState state() const { return state_; }

   private:
    LifecycleState state_ = kCreated;
    bool did_enter_bound_ = false;

    // The reason for teardown. Only valid when |state_| is |kMustTeardown|.
    fidl::UnbindInfo info_ = {};
  } lifecycle_ __TA_GUARDED(lock_) = {};
};

//
// Server binding specifics
//

class IncomingMessageDispatcher;
class AsyncTransaction;

// A generic callback type handling the completion of server unbinding.
// Note that the first parameter is a pointer to |IncomingMessageDispatcher|,
// which is the common base interface implemented by all server protocol
// message handling interfaces.
//
// The bindings runtime need to convert this pointer to the specific server
// implementation type before invoking the public unbinding completion callback
// that is |fidl::OnUnboundFn<ServerImpl>|.
using AnyOnUnboundFn = fit::callback<void(IncomingMessageDispatcher*, UnbindInfo, zx::channel)>;

// The async server binding. It directly owns the channel.
class AsyncServerBinding : public AsyncBinding {
 private:
  struct ConstructionKey {};

 public:
  static std::shared_ptr<AsyncServerBinding> Create(async_dispatcher_t* dispatcher,
                                                    zx::channel&& server_end,
                                                    IncomingMessageDispatcher* interface,
                                                    AnyOnUnboundFn&& on_unbound_fn) {
    auto ret = std::make_shared<AsyncServerBinding>(dispatcher, std::move(server_end), interface,
                                                    std::move(on_unbound_fn), ConstructionKey{});
    // We keep the binding alive until somebody decides to close the channel.
    ret->keep_alive_ = ret;
    return ret;
  }

  virtual ~AsyncServerBinding() = default;

  zx::unowned_channel channel() const { return server_end_.get().borrow(); }

  std::optional<UnbindInfo> Dispatch(fidl::IncomingMessage& msg, bool* binding_released) override;

  // Start closing the server connection with an |epitaph|.
  void Close(std::shared_ptr<AsyncBinding>&& calling_ref, zx_status_t epitaph) {
    StartTeardownWithInfo(std::move(calling_ref), fidl::UnbindInfo::Close(epitaph));
  }

  // Do not construct this object outside of this class. This constructor takes
  // a private type following the pass-key idiom to support |make_shared|.
  AsyncServerBinding(async_dispatcher_t* dispatcher, zx::channel&& server_end,
                     IncomingMessageDispatcher* interface, AnyOnUnboundFn&& on_unbound_fn,
                     ConstructionKey key)
      : AsyncBinding(dispatcher, server_end.borrow(),
                     ThreadingPolicy::kCreateAndTeardownFromAnyThread),
        interface_(interface),
        server_end_(std::move(server_end)),
        on_unbound_fn_(std::move(on_unbound_fn)) {}

  IncomingMessageDispatcher* interface() const { return interface_; }

 private:
  friend fidl::internal::AsyncTransaction;

  // Waits for all references to the binding to be released.
  // Sends epitaph and invokes |on_unbound_fn_| as required.
  void FinishTeardown(std::shared_ptr<AsyncBinding>&& calling_ref, UnbindInfo info) override;

  // The server interface that handles FIDL method calls.
  IncomingMessageDispatcher* interface_ = nullptr;

  // The channel is owned by AsyncServerBinding.
  ExtractedOnDestruction<zx::channel> server_end_;

  // The user callback to invoke after teardown has completed.
  AnyOnUnboundFn on_unbound_fn_ = {};
};

//
// Client binding specifics
//

class ClientBase;

// The async client binding. The client supports both synchronous and
// asynchronous calls. Because the channel lifetime must outlast the duration
// of any synchronous calls, and that synchronous calls do not yet support
// cancellation, the client binding does not own the channel directly.
// Rather, it co-owns the channel between itself and any in-flight sync
// calls, using shared pointers.
class AsyncClientBinding final : public AsyncBinding {
 public:
  static std::shared_ptr<AsyncClientBinding> Create(async_dispatcher_t* dispatcher,
                                                    std::shared_ptr<zx::channel> channel,
                                                    std::shared_ptr<ClientBase> client,
                                                    AsyncEventHandler* event_handler,
                                                    AnyTeardownObserver&& teardown_observer,
                                                    ThreadingPolicy threading_policy);

  virtual ~AsyncClientBinding() = default;

  std::shared_ptr<zx::channel> GetChannel() const { return channel_; }

 private:
  AsyncClientBinding(async_dispatcher_t* dispatcher, std::shared_ptr<zx::channel> channel,
                     std::shared_ptr<ClientBase> client, AsyncEventHandler* event_handler,
                     AnyTeardownObserver&& teardown_observer, ThreadingPolicy threading_policy);

  std::optional<UnbindInfo> Dispatch(fidl::IncomingMessage& msg, bool* binding_released) override;

  void FinishTeardown(std::shared_ptr<AsyncBinding>&& calling_ref, UnbindInfo info) override;

  std::shared_ptr<zx::channel> channel_;
  std::shared_ptr<ClientBase> client_;
  AsyncEventHandler* event_handler_;
  AnyTeardownObserver teardown_observer_;
};

}  // namespace internal
}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_ASYNC_BINDING_H_
