// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FASYNC_INCLUDE_LIB_FASYNC_INTERNAL_BRIDGE_H_
#define SRC_LIB_FASYNC_INCLUDE_LIB_FASYNC_INTERNAL_BRIDGE_H_

#include <lib/fasync/internal/compiler.h>

LIB_FASYNC_CPP_VERSION_COMPAT_BEGIN

#include <lib/fasync/future.h>
#include <lib/fit/result.h>
#include <lib/fit/thread_safety.h>

#include <atomic>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>

namespace fasync {
namespace internal {

// State shared between one completer and one consumer.
// This object is somewhat unusual in that it has dual-ownership represented
// by a pair of single-ownership references: a |completion_ref| and a
// |consumption_ref|.
//
// The bridge's state evolves as follows:
// - Initially the bridge's disposition is "pending".
// - When the completer produces a result, the bridge's disposition
//   becomes "completed".
// - When the completer drops its ref without producing a result,
//   the bridge's disposition becomes "abandoned".
// - When the consumer drops its ref without consuming the result,
//   the bridge's disposition becomes "canceled".
// - When a full rendezvous between completer and consumer takes place,
//   the bridge's disposition becomes "returned".
// - When both refs are dropped, the bridge state is destroyed.
template <typename E, typename... Ts>
class bridge_state final {
 public:
  class completion_ref;
  class consumption_ref;
  class future_continuation;

  using result_type = ::fit::result<E, Ts...>;

  ~bridge_state() = default;

  static void create(completion_ref& out_completion_ref, consumption_ref& out_consumption_ref);

  bool was_canceled() const;
  bool was_abandoned() const;
  void complete(completion_ref ref, result_type result);

  bridge_state(const bridge_state&) = delete;
  bridge_state(bridge_state&&) = delete;
  bridge_state& operator=(const bridge_state&) = delete;
  bridge_state& operator=(bridge_state&&) = delete;

 private:
  enum class disposition { pending, abandoned, completed, canceled, returned };

  bridge_state() = default;

  void drop_completion_ref(bool was_completed);
  void drop_consumption_ref(bool was_consumed);
  void drop_ref_and_maybe_delete_self();
  void set_result_if_abandoned(result_type result_if_abandoned);
  ::fasync::poll<result_type> await_result(consumption_ref& ref, ::fasync::context& context);

  mutable std::mutex mutex_;

  // There can only be one of each ref type so the initial count is 2.
  static constexpr uint32_t initial_refs = 2;

  // Ref-count for completion and consumption.
  std::atomic<uint32_t> ref_count_{initial_refs};

  // The disposition of the bridge.
  // TODO(fxbug.dev/4139): It should be possible to implement a lock-free algorithm
  // so as to eliminate the re-entrance hazards by introducing additional
  // intermediate dispositions such that |task_| and |result| could be
  // safely accessed while in those states.
  disposition disposition_ FIT_GUARDED(mutex_) = {disposition::pending};

  // The suspended task.
  // Invariant: Only valid when disposition is |pending|.
  suspended_task task_ FIT_GUARDED(mutex_);

  // The result in flight.
  // Invariant: Only valid when disposition is |pending|, |completed|,
  // or |abandoned|.
  fasync::poll<result_type> try_poll_ FIT_GUARDED(mutex_) = ::fasync::pending();
};

// The unique capability held by a bridge's completer.
template <typename E, typename... Ts>
class bridge_state<E, Ts...>::completion_ref final {
 public:
  completion_ref() : state_(nullptr) {}

  explicit completion_ref(bridge_state& state) : state_(&state) {}  // Adopts existing reference.

  completion_ref(const completion_ref& other) = delete;
  completion_ref& operator=(const completion_ref& other) = delete;

  completion_ref(completion_ref&& other) : state_(other.state_) { other.state_ = nullptr; }

  completion_ref& operator=(completion_ref&& other) {
    if (&other != this) {
      if (state_) {
        state_->drop_completion_ref(false /*was_completed*/);
      }
      state_ = other.state_;
      other.state_ = nullptr;
    }
    return *this;
  }

  ~completion_ref() {
    if (state_) {
      state_->drop_completion_ref(false /*was_completed*/);
    }
  }

  explicit operator bool() const { return !!state_; }

  bridge_state& get() const { return *state_; }

  void drop_after_completion() {
    state_->drop_completion_ref(true /*was_completed*/);
    state_ = nullptr;
  }

 private:
  bridge_state* state_;
};

// The unique capability held by a bridge's consumer.
template <typename E, typename... Ts>
class bridge_state<E, Ts...>::consumption_ref final {
 public:
  consumption_ref() : state_(nullptr) {}

  explicit consumption_ref(bridge_state& state) : state_(&state) {}  // Adopts existing reference.

  consumption_ref(const consumption_ref& other) = delete;
  consumption_ref& operator=(const consumption_ref& other) = delete;

  consumption_ref(consumption_ref&& other) : state_(other.state_) { other.state_ = nullptr; }

  consumption_ref& operator=(consumption_ref&& other) {
    if (&other != this) {
      if (state_) {
        state_->drop_consumption_ref(false /*was_consumed*/);
      }
      state_ = other.state_;
      other.state_ = nullptr;
    }
    return *this;
  }

  ~consumption_ref() {
    if (state_) {
      state_->drop_consumption_ref(false /*was_consumed*/);
    }
  }

  explicit operator bool() const { return !!state_; }

  bridge_state& get() const { return *state_; }

  void drop_after_consumption() {
    state_->drop_consumption_ref(true /*was_consumed*/);
    state_ = nullptr;
  }

 private:
  bridge_state* state_;
};

// The continuation produced by |consumer::future()| and company.
template <typename E, typename... Ts>
class bridge_state<E, Ts...>::future_continuation final {
 public:
  explicit future_continuation(consumption_ref ref) : ref_(std::move(ref)) {}

  future_continuation(consumption_ref ref, result_type result_if_abandoned) : ref_(std::move(ref)) {
    ref_.get().set_result_if_abandoned(std::move(result_if_abandoned));
  }

  ::fasync::poll<result_type> operator()(::fasync::context& context) {
    return ref_.get().await_result(ref_, context);
  }

 private:
  consumption_ref ref_;
};

// The callback produced by |completer::bind()|.
template <typename E, typename... Ts>
class bridge_bind_callback final {
  using callback_bridge_state = bridge_state<E, Ts...>;

 public:
  explicit bridge_bind_callback(typename callback_bridge_state::completion_ref ref)
      : ref_(std::move(ref)) {}

  template <typename TT = first<Ts...>, requires_conditions<cpp17::negation<has_type<TT>>> = true>
  void operator()() {
    callback_bridge_state& state = ref_.get();
    state.complete(std::move(ref_), ::fit::ok());
  }

  template <typename TT = first<Ts...>, typename T = typename TT::type>
  void operator()(T&& value) {
    callback_bridge_state& state = ref_.get();
    state.complete(std::move(ref_), ::fit::ok(std::forward<T>(value)));
  }

  template <
      typename TT = first<Ts...>, typename T = typename TT::type, typename... Params,
      requires_conditions<is_tuple<T>, std::is_constructible<T, std::tuple<Params...>>> = true>
  void operator()(Params&&... params) {
    callback_bridge_state& state = ref_.get();
    state.complete(std::move(ref_), ::fit::ok(std::make_tuple(std::forward<Params>(params)...)));
  }

 private:
  typename callback_bridge_state::completion_ref ref_;
};

template <typename E, typename... Ts>
void bridge_state<E, Ts...>::create(completion_ref& out_completion_ref,
                                    consumption_ref& out_consumption_ref) {
  bridge_state& state = *new bridge_state();
  out_completion_ref = completion_ref(state);
  out_consumption_ref = consumption_ref(state);
}

template <typename E, typename... Ts>
bool bridge_state<E, Ts...>::was_canceled() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return disposition_ == disposition::canceled;
}

template <typename E, typename... Ts>
bool bridge_state<E, Ts...>::was_abandoned() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return disposition_ == disposition::abandoned;
}

template <typename E, typename... Ts>
void bridge_state<E, Ts...>::drop_completion_ref(bool was_completed) {
  suspended_task task_to_notify;
  bool should_resume_task = false;
  if (!was_completed) {
    // The task was abandoned.
    std::lock_guard<std::mutex> lock(mutex_);
    assert(disposition_ == disposition::pending || disposition_ == disposition::canceled);
    if (disposition_ == disposition::pending) {
      disposition_ = disposition::abandoned;
      task_to_notify.swap(task_);
      should_resume_task = !try_poll_.is_pending();
    }
  }

  // Drop or resume |task_to_notify| and drop the ref outside of the lock.
  // This guards against re-entrance in case the consumption ref is
  // dropped as a side-effect of these operations.
  if (task_to_notify && should_resume_task) {
    task_to_notify.resume();
  }
  drop_ref_and_maybe_delete_self();
}

template <typename E, typename... Ts>
void bridge_state<E, Ts...>::drop_consumption_ref(bool was_consumed) {
  suspended_task task_to_drop;
  ::fasync::poll<result_type> result_to_drop = ::fasync::pending();
  if (!was_consumed) {
    // The task was canceled.
    std::lock_guard<std::mutex> lock(mutex_);
    assert(disposition_ == disposition::pending || disposition_ == disposition::completed ||
           disposition_ == disposition::abandoned);
    if (disposition_ == disposition::pending) {
      disposition_ = disposition::canceled;
      task_to_drop.swap(task_);
      using std::swap;
      swap(result_to_drop, try_poll_);
    }
  }

  // Drop |task_to_drop|, drop |result_to_drop|, and drop the ref
  // outside of the lock.
  // This guards against re-entrance in case the completion ref is
  // dropped as a side-effect of these operations.
  drop_ref_and_maybe_delete_self();
}

template <typename E, typename... Ts>
void bridge_state<E, Ts...>::drop_ref_and_maybe_delete_self() {
  // We're using release-acquire semantics to order with the implied release-acquire of our
  // |std::mutex|. Otherwise this |.fetch_sub()| would be fine with |std::memory_order_relaxed|.
  uint32_t count = ref_count_.fetch_sub(1u, std::memory_order_release);
  assert(count > 0);
  if (count == 1) {
    std::atomic_thread_fence(std::memory_order_acquire);
    delete this;
  }
}

template <typename E, typename... Ts>
void bridge_state<E, Ts...>::complete(completion_ref ref, result_type result) {
  assert(&ref.get() == this);
  ::fasync::poll<result_type> poll = ::fasync::done(std::move(result));
  suspended_task task_to_notify;
  bool should_resume_task = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    assert(disposition_ == disposition::pending || disposition_ == disposition::canceled);
    if (disposition_ == disposition::pending) {
      disposition_ = disposition::completed;
      poll.swap(try_poll_);
      task_to_notify.swap(task_);
      should_resume_task = !try_poll_.is_pending();
    }
  }

  // Drop or resume |task_to_notify|, drop any prior result that
  // was swapped into |result|, and drop the ref outside of the lock.
  // This guards against re-entrance in case the consumption ref is
  // dropped as a side-effect of these operations.
  if (task_to_notify && should_resume_task) {
    task_to_notify.resume();
  }
  ref.drop_after_completion();
}

template <typename E, typename... Ts>
void bridge_state<E, Ts...>::set_result_if_abandoned(result_type result_if_abandoned) {
  ::fasync::poll<result_type> poll_if_abandoned = ::fasync::done(std::move(result_if_abandoned));
  std::lock_guard<std::mutex> lock(mutex_);
  assert(disposition_ == disposition::pending || disposition_ == disposition::completed ||
         disposition_ == disposition::abandoned);
  if (disposition_ == disposition::pending || disposition_ == disposition::abandoned) {
    poll_if_abandoned.swap(try_poll_);
  }

  // Drop any prior value that was swapped into |result_if_abandoned|
  // outside of the lock.
}

template <typename E, typename... Ts>
::fasync::poll<typename bridge_state<E, Ts...>::result_type> bridge_state<E, Ts...>::await_result(
    consumption_ref& ref, ::fasync::context& context) {
  assert(&ref.get() == this);
  suspended_task task_to_drop;
  ::fasync::poll<result_type> result = ::fasync::pending();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    assert(disposition_ == disposition::pending || disposition_ == disposition::completed ||
           disposition_ == disposition::abandoned);
    if (disposition_ == disposition::pending) {
      task_to_drop.swap(task_);
      task_ = context.suspend_task();  // Assuming this isn't re-entrant.
      return ::fasync::pending();
    }
    disposition_ = disposition::returned;
    result.swap(try_poll_);
  }

  // Drop |task_to_drop| and the ref outside of the lock.
  ref.drop_after_consumption();
  return result;
}

}  // namespace internal

template <typename E, typename... Ts>
class bridge;
template <typename E, typename... Ts>
class completer;
template <typename E, typename... Ts>
class consumer;

}  // namespace fasync

LIB_FASYNC_CPP_VERSION_COMPAT_END

#endif  // SRC_LIB_FASYNC_INCLUDE_LIB_FASYNC_INTERNAL_BRIDGE_H_
