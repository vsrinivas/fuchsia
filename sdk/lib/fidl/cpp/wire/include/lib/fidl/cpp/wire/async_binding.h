// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_ASYNC_BINDING_H_
#define LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_ASYNC_BINDING_H_

#include <lib/async/dispatcher.h>
#include <lib/async/task.h>
#include <lib/async/wait.h>
#include <lib/fidl/cpp/wire/extract_resource_on_destruction.h>
#include <lib/fidl/cpp/wire/internal/client_details.h>
#include <lib/fidl/cpp/wire/internal/endpoints.h>
#include <lib/fidl/cpp/wire/internal/synchronization_checker.h>
#include <lib/fidl/cpp/wire/message.h>
#include <lib/fidl/cpp/wire/status.h>
#include <lib/fidl/cpp/wire/transaction.h>
#include <lib/fidl/cpp/wire/wire_messaging_declarations.h>
#include <lib/fidl/epitaph.h>
#include <lib/fit/function.h>
#include <lib/sync/completion.h>
#include <lib/zx/channel.h>
#include <zircon/fidl.h>

#include <mutex>
#include <optional>
#include <variant>

namespace fidl {

// TODO(fxbug.dev/85474): A formatter bug causes this enum to be formatted with
// 4 byte indent otherwise.
// clang-format off

// The return value of various TryDispatch and
// |IncomingMessageDispatcher::dispatch_message| functions, which call into the
// appropriate server message handlers based on the method ordinal.
enum class __attribute__((enum_extensibility(closed))) [[nodiscard]] DispatchResult {
  // The FIDL method ordinal was not recognized by the dispatch function.
  kNotFound = false,

  // The FIDL method ordinal matched one of the handlers.
  // Note that this does not necessarily mean the message was handled successfully.
  // For example, the message could fail to decode.
  kFound = true,
};

// clang-format on

namespace internal {

struct DispatchError {
  UnbindInfo info;
  ErrorOrigin origin;

  // Whether the bindings should disregard any unread messages in the transport
  // and directly teardown upon encountering this error.
  bool RequiresImmediateTeardown();
};

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
class AsyncBinding : public std::enable_shared_from_this<AsyncBinding> {
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

  // Notifies the binding of an |error| while servicing messages.
  // This may lead to binding teardown.
  void HandleError(std::shared_ptr<AsyncBinding>&& calling_ref, DispatchError error)
      __TA_EXCLUDES(lock_);

  void StartTeardown(std::shared_ptr<AsyncBinding>&& calling_ref) __TA_EXCLUDES(thread_checker_)
      __TA_EXCLUDES(lock_) {
    StartTeardownWithInfo(std::move(calling_ref), ::fidl::UnbindInfo::Unbind());
  }

  // Returns true if the binding will teardown at the next iteration of the
  // event loop, or has already torn down and pending deletion.
  bool IsDestructionImminent() const __TA_EXCLUDES(lock_);

 protected:
  AsyncBinding(async_dispatcher_t* dispatcher, internal::AnyUnownedTransport transport,
               ThreadingPolicy threading_policy);

  // |InitKeepAlive| must be called after a concrete subclass is constructed
  // in a shared pointer, to set up the initial circular keep-alive reference.
  void InitKeepAlive() {
    ZX_DEBUG_ASSERT(!keep_alive_.get());
    keep_alive_ = shared_from_this();
  }

  // Common message handling entrypoint shared by both client and server bindings.
  void MessageHandler(fidl::IncomingHeaderAndMessage& msg,
                      internal::MessageStorageViewBase* storage_view) __TA_EXCLUDES(thread_checker_)
      __TA_EXCLUDES(lock_);

  void WaitFailureHandler(UnbindInfo info) __TA_EXCLUDES(thread_checker_) __TA_EXCLUDES(lock_);

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
  // The caller should simply ignore the |fidl::IncomingHeaderAndMessage| object once
  // it is passed to this function, letting RAII clean up handles as needed.
  //
  // ## Return value
  //
  // If errors occur during dispatching, the function will return an
  // |DispatchError| describing the error. Otherwise, it will return
  // |std::nullopt|.
  //
  // If `*next_wait_begun_early` is set, the calling code no longer has ownership of
  // this |AsyncBinding| object and so must not access its state.
  virtual std::optional<DispatchError> Dispatch(fidl::IncomingHeaderAndMessage& msg,
                                                bool* next_wait_begun_early,
                                                internal::MessageStorageViewBase* storage_view)
      __TA_REQUIRES(thread_checker_) = 0;

  async_dispatcher_t* dispatcher() const { return dispatcher_; }

  const DebugOnlySynchronizationChecker& thread_checker() const { return thread_checker_; }

  // Initiates teardown with the provided |info| as reason.
  // This does not have to happen in the context of a dispatcher thread.
  TeardownTaskPostingResult StartTeardownWithInfo(std::shared_ptr<AsyncBinding>&& calling_ref,
                                                  UnbindInfo info) __TA_EXCLUDES(thread_checker_)
      __TA_EXCLUDES(lock_);

 private:
  // Synchronously perform teardown in the context of a dispatcher thread with
  // exclusive access of the internal binding reference.
  //
  // If |lifecycle_| is not yet in |MustTeardown|, |info| must be present to
  // specify the teardown reason.
  //
  // Always releases |thread_checker_| because the binding (the current object)
  // is destroyed during this function.
  void PerformTeardown(std::optional<UnbindInfo> info) __TA_REQUIRES(thread_checker_)
      __TA_RELEASE(thread_checker_) __TA_EXCLUDES(lock_);

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
  //
  // Always releases |thread_checker_| because the binding (the current object)
  // is destroyed during this function.
  virtual void FinishTeardown(std::shared_ptr<AsyncBinding>&& calling_ref, UnbindInfo info)
      __TA_RELEASE(thread_checker_) __TA_REQUIRES(thread_checker_) = 0;

  async_dispatcher_t* dispatcher_ = nullptr;

  // The bound transport.
  AnyUnownedTransport transport_;

  // Storage for a |TransportWaiter|, which waits for messages and calls back
  // into |AsyncBinding| when they are received, or if the channel is closed.
  AnyTransportWaiter any_transport_waiter_;

  // A circular reference that represents the dispatcher ownership of the
  // |AsyncBinding|. When |lifecycle_| is |Lifecycle::Bound|, all mutations of
  // |keep_alive_| must happen on a dispatcher thread.
  std::shared_ptr<AsyncBinding> keep_alive_ = {};

  // |thread_checker_| checks that required operations run on the appropriate
  // thread as indicated by the threading policy.
  //
  // |thread_checker_| is no-op in release builds, and may be completely
  // optimized out.
  [[no_unique_address]] DebugOnlySynchronizationChecker thread_checker_;

  // A lock protecting the binding |lifecycle|.
  //
  // It is mutable to support const accessors of the lifecycle.
  mutable std::mutex lock_;

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
    bool Is(LifecycleState state) const { return state_ == state; }

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

// A generic callback type handling the completion of server unbinding.
// Note that the first parameter is a pointer to |IncomingMessageDispatcher|,
// which is the common base interface implemented by all server protocol
// message handling interfaces.
//
// The bindings runtime need to convert this pointer to the specific server
// implementation type before invoking the public unbinding completion callback
// that is |fidl::OnUnboundFn<ServerImpl>|.
using AnyOnUnboundFn =
    fit::inline_callback<void(IncomingMessageDispatcher*, UnbindInfo, fidl::internal::AnyTransport),
                         48>;

// The async server binding. It directly owns the transport.
class AsyncServerBinding : public AsyncBinding {
 private:
  struct ConstructionKey {};

 public:
  static std::shared_ptr<AsyncServerBinding> Create(async_dispatcher_t* dispatcher,
                                                    fidl::internal::AnyTransport&& server_end,
                                                    IncomingMessageDispatcher* interface,
                                                    AnyOnUnboundFn&& on_unbound_fn);

  virtual ~AsyncServerBinding() = default;

  fidl::internal::AnyUnownedTransport transport() const { return server_end_.get().borrow(); }

  std::shared_ptr<AsyncServerBinding> shared_from_this() {
    return std::static_pointer_cast<AsyncServerBinding>(AsyncBinding::shared_from_this());
  }

  std::optional<DispatchError> Dispatch(fidl::IncomingHeaderAndMessage& msg,
                                        bool* next_wait_begun_early,
                                        internal::MessageStorageViewBase* storage_view) override;

  // Start closing the server connection with an |epitaph|.
  void Close(std::shared_ptr<AsyncBinding>&& calling_ref, zx_status_t epitaph) {
    StartTeardownWithInfo(std::move(calling_ref), fidl::UnbindInfo::Close(epitaph));
  }

  // Do not construct this object outside of this class. This constructor takes
  // a private type following the pass-key idiom to support |make_shared|.
  AsyncServerBinding(async_dispatcher_t* dispatcher, fidl::internal::AnyTransport&& server_end,
                     IncomingMessageDispatcher* interface, AnyOnUnboundFn&& on_unbound_fn,
                     ConstructionKey key)
      : AsyncBinding(dispatcher, server_end.borrow(),
                     ThreadingPolicy::kCreateAndTeardownFromAnyThread),
        interface_(interface),
        server_end_(std::move(server_end)),
        on_unbound_fn_(std::move(on_unbound_fn)) {}

  IncomingMessageDispatcher* interface() const { return interface_; }

 private:
  // Waits for all references to the binding to be released.
  // Sends epitaph and invokes |on_unbound_fn_| as required.
  void FinishTeardown(std::shared_ptr<AsyncBinding>&& calling_ref, UnbindInfo info) override;

  // The server interface that handles FIDL method calls.
  IncomingMessageDispatcher* interface_ = nullptr;

  // The transport is owned by AsyncServerBinding.
  ExtractedOnDestruction<fidl::internal::AnyTransport> server_end_;

  // The user callback to invoke after teardown has completed.
  AnyOnUnboundFn on_unbound_fn_ = {};
};

//
// Client binding specifics
//

class ClientBase;

// The async client binding. The client supports both synchronous and
// asynchronous calls. Because the transport lifetime must outlast the duration
// of any synchronous calls, and that synchronous calls do not yet support
// cancellation, the client binding does not own the transport directly.
// Rather, it co-owns the transport between itself and any in-flight sync
// calls, using shared pointers.
class AsyncClientBinding final : public AsyncBinding {
 public:
  static std::shared_ptr<AsyncClientBinding> Create(
      async_dispatcher_t* dispatcher, std::shared_ptr<fidl::internal::AnyTransport> transport,
      std::shared_ptr<ClientBase> client, AsyncEventHandler* error_handler,
      AnyTeardownObserver&& teardown_observer, ThreadingPolicy threading_policy);

  virtual ~AsyncClientBinding() = default;

  // Obtain the transport and check that the caller is on the appropriate thread.
  std::shared_ptr<fidl::internal::AnyTransport> GetTransport() const {
    ScopedThreadGuard guard(thread_checker());
    return transport_;
  }

 private:
  AsyncClientBinding(async_dispatcher_t* dispatcher,
                     std::shared_ptr<fidl::internal::AnyTransport> transport,
                     std::shared_ptr<ClientBase> client, AsyncEventHandler* error_handler,
                     AnyTeardownObserver&& teardown_observer, ThreadingPolicy threading_policy);

  std::optional<DispatchError> Dispatch(fidl::IncomingHeaderAndMessage& msg, bool* binding_released,
                                        internal::MessageStorageViewBase* storage_view) override;

  void FinishTeardown(std::shared_ptr<AsyncBinding>&& calling_ref, UnbindInfo info) override;

  std::shared_ptr<fidl::internal::AnyTransport> transport_;
  std::shared_ptr<ClientBase> client_;
  AsyncEventHandler* error_handler_;
  AnyTeardownObserver teardown_observer_;
};

}  // namespace internal
}  // namespace fidl

#endif  // LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_ASYNC_BINDING_H_
