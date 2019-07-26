// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIT_SCOPE_H_
#define LIB_FIT_SCOPE_H_

#include <assert.h>

#include <atomic>
#include <mutex>

#include "promise.h"
#include "thread_safety.h"

namespace fit {

// Provides a mechanism for binding promises to the lifetime of another object
// such that they are destroyed before that object goes out of scope.  It is
// particularly useful for ensuring that the lifetime of a promise does not
// exceed the lifetime of any variables that it has captured by reference.
//
// A scope is thread-safe but non-reentrant: it must not be destroyed while
// any of its associated promises are running.
//
// EXAMPLE
//
// Define a |fit::scope| as a member of the object to whose lifetime the
// promises should be bound.
//
//     // We mark this class final because its destructor has side-effects
//     // that rely on the order of destruction.  If this object were
//     // subclassed there would be a possibility for promises bound to its
//     // scope to inadvertently access the subclass's state while the object
//     // was being destroyed.
//     class accumulator final {
//     public:
//         accumulator() = default;
//         ~accumulator() = default;
//
//         fit::promise<int> accumulate(int value);
//
//     private:
//         int prior_total_ = 0;
//
//         // This member is last so that the scope is exited before all
//         // other members of the object are destroyed.  Alternately, we
//         // could enforce this ordering by explicitly invoking
//         // |fit::scope::exit()| where appropriate.
//         fit::scope scope_;
//     };
//
// Use |fit::promise::wrap_with()| to wrap up promises that capture pointers
// to the object.  In this example, the captured pointer is "this".
//
//     fit::promise<int> accumulator::accumulate(int value) {
//         return fit::make_promise([this, value] {
//             prior_total_ += value;
//             return fit::ok(prior_total_);
//         }).wrap_with(scope_); /* binding to scope happens here */
//     }
//
class scope final {
 public:
  // Creates a new scope.
  scope();

  // Exits the scope and destroys all of its wrapped promises.
  // Asserts that no promises are currently running.
  ~scope();

  // Returns true if the scope has been exited.
  //
  // This method is thread-safe.
  bool exited() const { return state_->exited(); }

  // Exits the scope and destroys all of its wrapped promises.
  // Assets that no promises are currently running.
  //
  // This method is thread-safe.
  void exit() { return state_->exit(false /*scope_was_destroyed*/); }

  // Returns a promise which wraps the specified |promise| and binds the
  // promise to this scope.
  //
  // The specified promise will automatically be destroyed when its wrapper
  // is destroyed or when the scope is exited.  If the scope has already
  // exited then the wrapped promise will be immediately destroyed.
  //
  // When the returned promise is invoked before the scope is exited,
  // the promise that it wraps will be invoked as usual.  However, when
  // the returned promise is invoked after the scope is exited, it
  // immediately returns a pending result (since the promise that it
  // previously wrapped has already been destroyed).  By returning a
  // pending result, the return promise effectively indicates to the
  // executor that the task has been "abandoned" due to the scope being
  // exited.
  //
  // This method is thread-safe.
  template <typename Promise>
  decltype(auto) wrap(Promise promise) {
    assert(promise);
    return fit::make_promise_with_continuation(scoped_continuation<Promise>(
        state_->adopt_promise(new promise_holder<Promise>(std::move(promise)))));
  }

  scope(const scope&) = delete;
  scope(scope&&) = delete;
  scope& operator=(const scope&) = delete;
  scope& operator=(scope&&) = delete;

 private:
  class state;
  class promise_holder_base;

  // Holds a reference to a promise that is owned by the state.
  class promise_handle final {
   public:
    promise_handle() = default;

   private:
    // |state| and |promise_holder| belong to the state object.
    // Invariant: If |promise_holder| is non-null then |state| is
    // also non-null.
    friend state;
    promise_handle(state* state, promise_holder_base* promise_holder)
        : state_(state), promise_holder_(promise_holder) {}

    state* state_ = nullptr;
    promise_holder_base* promise_holder_ = nullptr;
  };

  // Holds the shared state of the scope.
  // This object is destroyed once the scope and all of its promises
  // have been destroyed.
  class state final {
   public:
    state();
    ~state();

    // The following methods are called from the |scope|.

    bool exited() const;
    void exit(bool scope_was_destroyed);

    // The following methods are called from the |scoped_continuation|.

    // Links a promise to the scope's lifecycle such that it will be
    // destroyed when the scope is exited.  Returns a handle that may
    // be used to access the promise later.
    // The state takes ownership of the promise.
    promise_handle adopt_promise(promise_holder_base* promise_holder);

    // Unlinks a promise from the scope's lifecycle given its handle
    // and causes the underlying promise to be destroyed if it hasn't
    // already been destroyed due to the scope exiting.
    // Does nothing if the handle was default-initialized.
    static void drop_promise(promise_handle promise_handle);

    // Acquires a promise given its handle.
    // Returns nullptr if the handle was default-initialized or if
    // the scope exited, meaning that the promise was not acquired.
    // The promise must be released before it can be acquired again.
    static promise_holder_base* try_acquire_promise(promise_handle promise_handle);

    // Releases a promise that was successfully acquired.
    static void release_promise(promise_handle promise_handle);

    state(const state&) = delete;
    state(state&&) = delete;
    state& operator=(const state&) = delete;
    state& operator=(state&&) = delete;

   private:
    bool should_delete_self() const FIT_REQUIRES(mutex_) {
      return scope_was_destroyed_ && promise_handle_count_ == 0;
    }

    static constexpr uint64_t scope_exited = static_cast<uint64_t>(1u) << 63;

    // Tracks of the number of promises currently running ("acquired").
    // The top bit is set when the scope is exited, at which point no
    // new promises can be acquired.  After exiting, the count can
    // be incremented transiently but is immediately decremented again
    // until all promise handles have been released.  Once no promise
    // handles remain, the count will equal |scope_exited| and will not
    // change again.
    std::atomic_uint64_t acquired_promise_count_{0};

    mutable std::mutex mutex_;
    bool scope_was_destroyed_ FIT_GUARDED(mutex_) = false;
    uint64_t promise_handle_count_ FIT_GUARDED(mutex_) = 0;
    promise_holder_base* head_promise_holder_ FIT_GUARDED(mutex_) = nullptr;
  };

  // Base type for managing the lifetime of a promise of any type.
  // It is owned by the state and retained indirectly by the continuation
  // using a |promise_handle|.
  class promise_holder_base {
   public:
    promise_holder_base() = default;
    virtual ~promise_holder_base() = default;

    promise_holder_base(const promise_holder_base&) = delete;
    promise_holder_base(promise_holder_base&&) = delete;
    promise_holder_base& operator=(const promise_holder_base&) = delete;
    promise_holder_base& operator=(promise_holder_base&&) = delete;

   private:
    // |next| and |prev| belong to the state object.
    friend class state;
    promise_holder_base* next = nullptr;
    promise_holder_base* prev = nullptr;
  };

  // Holder for a promise of a particular type.
  template <typename Promise>
  class promise_holder final : public promise_holder_base {
   public:
    explicit promise_holder(Promise promise) : promise(std::move(promise)) {}
    ~promise_holder() override = default;

    Promise promise;
  };

  // Wraps a promise whose lifetime is managed by the scope.
  template <typename Promise>
  class scoped_continuation final {
   public:
    explicit scoped_continuation(promise_handle promise_handle) : promise_handle_(promise_handle) {}

    scoped_continuation(scoped_continuation&& other) : promise_handle_(other.promise_handle_) {
      other.promise_handle_ = promise_handle{};
    }

    ~scoped_continuation() { state::drop_promise(promise_handle_); }

    typename Promise::result_type operator()(context& context) {
      typename Promise::result_type result;
      auto holder =
          static_cast<promise_holder<Promise>*>(state::try_acquire_promise(promise_handle_));
      if (holder) {
        result = holder->promise(context);
        state::release_promise(promise_handle_);
      }
      return result;
    }

    scoped_continuation& operator=(scoped_continuation&& other) {
      if (this != &other) {
        state::drop_promise(promise_handle_);
        promise_handle_ = other.promise_handle_;
        other.promise_handle_ = promise_handle{};
      }
      return *this;
    }

    scoped_continuation(const scoped_continuation&) = delete;
    scoped_continuation& operator=(const scoped_continuation&) = delete;

   private:
    promise_handle promise_handle_;
  };

  // The scope's shared state.
  state* const state_;
};

}  // namespace fit

#endif  // LIB_FIT_SCOPE_H_
