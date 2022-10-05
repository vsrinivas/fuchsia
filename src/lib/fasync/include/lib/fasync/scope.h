// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FASYNC_INCLUDE_LIB_FASYNC_SCOPE_H_
#define SRC_LIB_FASYNC_INCLUDE_LIB_FASYNC_SCOPE_H_

#include <lib/fasync/internal/compiler.h>

LIB_FASYNC_CPP_VERSION_COMPAT_BEGIN

#include <assert.h>
#include <lib/fasync/future.h>
#include <lib/fit/thread_safety.h>

#include <atomic>
#include <mutex>

namespace fasync {

// |fasync::scope|
//
// Provides a mechanism for binding futures to the lifetime of another object such that they are
// destroyed before that object goes out of scope. It is particularly useful for ensuring that the
// lifetime of a future does not exceed the lifetime of any variables that it has captured by
// reference.
//
// A scope is thread-safe but non-reentrant: it must not be destroyed while any of its associated
// futures are running.
//
// EXAMPLE
//
// Define a |fasync::scope| as a member of the object to whose lifetime the futures should be bound.
//
//     // We mark this class final because its destructor has side-effects that rely on the order of
//     // destruction. If this object were subclassed there would be a possibility for futures bound
//     // to its scope to inadvertently access the subclass's state while the object was being
//     // destroyed.
//     class accumulator final {
//     public:
//         accumulator() = default;
//         ~accumulator() = default;
//
//         fasync::future<int> accumulate(int value);
//
//     private:
//         int prior_total_ = 0;
//
//         // This member is last so that the scope is exited before all other members of the object
//         // are destroyed. Alternately, we could enforce this ordering by explicitly invoking
//         // |fasync::scope::exit()| where appropriate.
//         fasync::scope scope_;
//     };
//
// Use |fasync::wrap_with()| to wrap up futures that capture pointers to the object. In this
// example, the captured pointer is "this".
//
//     fasync::future<int> accumulator::accumulate(int value) {
//         return fasync::make_future([this, value] {
//             prior_total_ += value;
//             return fit::ok(prior_total_);
//         }) | fasync::wrap_with(scope_); // Binding to scope happens here.
//     }
//
class scope final {
 public:
  // Creates a new scope.
  scope() : state_(new state()) {}

  // Exits the scope and destroys all of its wrapped futures.
  // Asserts that no futures are currently running.
  ~scope() { state_->exit(true /*scope_was_destroyed*/); }

  constexpr scope(const scope&) = delete;
  constexpr scope& operator=(const scope&) = delete;
  constexpr scope(scope&&) = delete;
  constexpr scope& operator=(scope&&) = delete;

  // Returns true if the scope has been exited.
  //
  // This method is thread-safe.
  bool exited() const { return state_->exited(); }

  // Exits the scope and destroys all of its wrapped futures.
  // Assets that no futures are currently running.
  //
  // This method is thread-safe.
  void exit() { return state_->exit(false /*scope_was_destroyed*/); }

  // Returns a future which wraps the specified |future| and binds the future to this scope.
  //
  // The specified future will automatically be destroyed when its wrapper is destroyed or when the
  // scope is exited. If the scope has already exited then the wrapped future will be immediately
  // destroyed.
  //
  // When the returned future is invoked before the scope is exited, the future that it wraps will
  // be invoked as usual. However, when the returned future is invoked after the scope is exited, it
  // immediately returns a pending result (since the future that it previously wrapped has already
  // been destroyed). By returning a pending result, the return future effectively indicates to the
  // executor that the task has been "abandoned" due to the scope being exited.
  //
  // This method is thread-safe.
  template <typename F>
  auto wrap(F&& future) {
    return fasync::make_future(
        scoped_future<F>(state_->adopt_future(new future_holder<F>(std::move(future)))));
  }

 private:
  class state;
  class future_holder_base;

  // Holds a reference to a future that is owned by the state.
  class future_handle final {
   public:
    constexpr future_handle() = default;

   private:
    // |state| and |future| belong to the state object.
    // Invariant: If |future_holder| is non-null then |state| is also non-null.
    friend state;
    constexpr future_handle(state* state, future_holder_base* future_holder)
        : state_(state), future_holder_(future_holder) {}

    state* state_ = nullptr;
    future_holder_base* future_holder_ = nullptr;
  };

  // Holds the shared state of the scope.
  // This object is destroyed once the scope and all of its futures have been destroyed.
  class state final {
   public:
    constexpr state() = default;
    ~state() {
      assert(acquired_future_count_.load(std::memory_order_relaxed) == scope_exited);
      assert(scope_was_destroyed_);
      assert(future_handle_count_ == 0);
      assert(head_future_holder_ == nullptr);
    }

    constexpr state(const state&) = delete;
    constexpr state& operator=(const state&) = delete;
    constexpr state(state&&) = delete;
    constexpr state& operator=(state&&) = delete;

    // The following methods are called from the |scope|.

    bool exited() const {
      return acquired_future_count_.load(std::memory_order_relaxed) & scope_exited;
    }

    void exit(bool scope_was_destroyed) {
      future_holder_base* release_head = nullptr;
      bool delete_self = false;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        assert(!scope_was_destroyed_);
        scope_was_destroyed_ = scope_was_destroyed;

        // Atomically exit the scope. We cannot do this safely if there are any running futures
        // since they might still be accessing state which is guarded by the scope. Worse, if a
        // future re-entrantly destroys the scope during its execution then as a side-effect the
        // future itself will be destroyed. So assert!
        uint64_t prior_count =
            acquired_future_count_.exchange(scope_exited, std::memory_order_relaxed);
        if (!(prior_count & scope_exited)) {
          // Cannot exit fasync::scope while any of its futures are running!
          assert(prior_count == 0);

          // Take the futures so they can be deleted outside of the lock.
          release_head = head_future_holder_;
          head_future_holder_ = nullptr;
        }

        // If there are no more handles then we can delete the state now.
        delete_self = should_delete_self();
      }

      // Delete aborted futures and self outside of the lock.
      while (release_head) {
        future_holder_base* release_next = release_head->next;
        delete release_head;
        release_head = release_next;
      }
      if (delete_self) {
        delete this;
      }
    }

    // The following methods are called from the |scoped_future|.

    // Links a future to the scope's lifecycle such that it will be destroyed when the scope is
    // exited. Returns a handle that may be used to access the future later.
    // The state takes ownership of the future.
    future_handle adopt_future(future_holder_base* future_holder) {
      {
        std::lock_guard<std::mutex> lock(mutex_);
        assert(!scope_was_destroyed_);  // Otherwise how did we get here?

        // If the scope hasn't been exited yet, link the future and mint a new handle. Otherwise we
        // will abort the future.
        if (!exited()) {
          if (head_future_holder_) {
            head_future_holder_->prev = future_holder;
            future_holder->next = head_future_holder_;
          }
          head_future_holder_ = future_holder;
          future_handle_count_++;
          return future_handle(this, future_holder);
        }
      }

      // Delete aborted future outside of the lock.
      delete future_holder;
      return future_handle{};
    }

    // Unlinks a future from the scope's lifecycle given its handle and causes the underlying future
    // to be destroyed if it hasn't already been destroyed due to the scope exiting.
    // Does nothing if the handle was default-initialized.
    static void drop_future(future_handle future_handle) {
      if (!future_handle.future_holder_) {
        return;  // Invalid handle, nothing to do.
      }

      {
        std::lock_guard<std::mutex> lock(future_handle.state_->mutex_);

        // If the scope hasn't been exited yet, unlink the future and prepare to destroy it.
        // Otherwise, it's already been unlinked and destroyed so release the handle but don't touch
        // the pointer!
        assert(future_handle.state_->future_handle_count_ > 0);
        future_handle.state_->future_handle_count_--;
        if (!future_handle.state_->exited()) {
          if (future_handle.future_holder_->next) {
            future_handle.future_holder_->next->prev = future_handle.future_holder_->prev;
          }
          if (future_handle.future_holder_->prev) {
            future_handle.future_holder_->prev->next = future_handle.future_holder_->next;
          } else {
            future_handle.state_->head_future_holder_ = future_handle.future_holder_->next;
          }
          // Fallthrough to delete the future.
        } else if (!future_handle.state_->should_delete_self()) {
          return;
        } else {
          // Fallthrough to delete self.
          future_handle.future_holder_ = nullptr;
        }
      }

      // Delete the future or scope outside of the lock.
      if (future_handle.future_holder_) {
        delete future_handle.future_holder_;
      } else {
        delete future_handle.state_;
      }
    }

    // Acquires a future given its handle.
    // Returns nullptr if the handle was default-initialized or if the scope exited, meaning that
    // the future was not acquired. The future must be released before it can be acquired again.
    static future_holder_base* try_acquire_future(future_handle future_handle) {
      if (future_handle.future_holder_) {
        uint64_t prior_count =
            future_handle.state_->acquired_future_count_.fetch_add(1u, std::memory_order_relaxed);
        if (!(prior_count & scope_exited)) {
          return future_handle.future_holder_;
        }
        future_handle.state_->acquired_future_count_.fetch_sub(1u, std::memory_order_relaxed);
      }
      return nullptr;
    }

    // Releases a future that was successfully acquired.
    static void release_future(future_handle future_handle) {
      future_handle.state_->acquired_future_count_.fetch_sub(1u, std::memory_order_relaxed);
    }

   private:
    constexpr bool should_delete_self() const FIT_REQUIRES(mutex_) {
      return scope_was_destroyed_ && future_handle_count_ == 0;
    }

    static constexpr uint64_t scope_exited = static_cast<uint64_t>(1u) << 63;

    // Tracks of the number of futures currently running ("acquired").
    // The top bit is set when the scope is exited, at which point no new futures can be acquired.
    // After exiting, the count can be incremented transiently but is immediately decremented again
    // until all future handles have been released. Once no future handles remain, the count will
    // equal |scope_exited| and will not change again.
    std::atomic_uint64_t acquired_future_count_{0};

    mutable std::mutex mutex_;
    bool scope_was_destroyed_ FIT_GUARDED(mutex_) = false;
    uint64_t future_handle_count_ FIT_GUARDED(mutex_) = 0;
    future_holder_base* head_future_holder_ FIT_GUARDED(mutex_) = nullptr;
  };

  // Base type for managing the lifetime of a future of any type.
  // It is owned by the state and retained indirectly by the continuation using a |future_handle|.
  class future_holder_base {
   public:
    constexpr future_holder_base() = default;
    virtual ~future_holder_base() = default;

    constexpr future_holder_base(const future_holder_base&) = delete;
    constexpr future_holder_base& operator=(const future_holder_base&) = delete;
    constexpr future_holder_base(future_holder_base&&) = delete;
    constexpr future_holder_base& operator=(future_holder_base&&) = delete;

   private:
    // |next| and |prev| belong to the state object.
    friend class state;
    future_holder_base* next = nullptr;
    future_holder_base* prev = nullptr;
  };

  // Holder for a future of a particular type.
  template <typename F>
  class future_holder final : public future_holder_base {
   public:
    explicit constexpr future_holder(F&& future) : future(std::move(future)) {}
    ~future_holder() override = default;

    F future;
  };

  // Wraps a future whose lifetime is managed by the scope.
  template <typename F>
  class scoped_future final {
   public:
    explicit constexpr scoped_future(future_handle future_handle) : future_handle_(future_handle) {}

    ~scoped_future() { state::drop_future(future_handle_); }

    constexpr scoped_future(const scoped_future&) = delete;
    constexpr scoped_future& operator=(const scoped_future&) = delete;

    constexpr scoped_future(scoped_future&& other) : future_handle_(other.future_handle_) {
      other.future_handle_ = future_handle{};
    }

    scoped_future& operator=(scoped_future&& other) {
      if (this != &other) {
        state::drop_future(future_handle_);
        future_handle_ = other.future_handle_;
        other.future_handle_ = future_handle{};
      }
      return *this;
    }

    future_poll_t<F> operator()(context& context) {
      future_poll_t<F> poll = fasync::pending();
      auto holder = static_cast<future_holder<F>*>(state::try_acquire_future(future_handle_));
      if (holder) {
        poll = holder->future(context);
        state::release_future(future_handle_);
      }
      return poll;
    }

   private:
    future_handle future_handle_;
  };

  // The scope's shared state.
  state* const state_;
};

}  // namespace fasync

LIB_FASYNC_CPP_VERSION_COMPAT_END

#endif  // SRC_LIB_FASYNC_INCLUDE_LIB_FASYNC_SCOPE_H_
