// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIT_BRIDGE_INTERNAL_H_
#define LIB_FIT_BRIDGE_INTERNAL_H_

#include <atomic>
#include <mutex>
#include <tuple>
#include <type_traits>
#include <utility>

#include "promise.h"
#include "result.h"
#include "thread_safety.h"

namespace fit {
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
template <typename V, typename E>
class bridge_state final {
 public:
  class completion_ref;
  class consumption_ref;
  class promise_continuation;

  using result_type = result<V, E>;

  ~bridge_state() = default;

  static void create(completion_ref* out_completion_ref, consumption_ref* out_consumption_ref);

  bool was_canceled() const;
  bool was_abandoned() const;
  void complete_or_abandon(completion_ref ref, result_type result);

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
  result_type await_result(consumption_ref* ref, ::fit::context& context);

  mutable std::mutex mutex_;

  // Ref-count for completion and consumption.
  // There can only be one of each ref type so the initial count is 2.
  std::atomic<uint32_t> ref_count_{2};

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
  result_type result_ FIT_GUARDED(mutex_);
};

// The unique capability held by a bridge's completer.
template <typename V, typename E>
class bridge_state<V, E>::completion_ref final {
 public:
  completion_ref() : state_(nullptr) {}

  explicit completion_ref(bridge_state* state) : state_(state) {}  // adopts existing reference

  completion_ref(completion_ref&& other) : state_(other.state_) { other.state_ = nullptr; }

  ~completion_ref() {
    if (state_)
      state_->drop_completion_ref(false /*was_completed*/);
  }

  completion_ref& operator=(completion_ref&& other) {
    if (&other == this)
      return *this;
    if (state_)
      state_->drop_completion_ref(false /*was_completed*/);
    state_ = other.state_;
    other.state_ = nullptr;
    return *this;
  }

  explicit operator bool() const { return !!state_; }

  bridge_state* get() const { return state_; }

  void drop_after_completion() {
    state_->drop_completion_ref(true /*was_completed*/);
    state_ = nullptr;
  }

  completion_ref(const completion_ref& other) = delete;
  completion_ref& operator=(const completion_ref& other) = delete;

 private:
  bridge_state* state_;
};

// The unique capability held by a bridge's consumer.
template <typename V, typename E>
class bridge_state<V, E>::consumption_ref final {
 public:
  consumption_ref() : state_(nullptr) {}

  explicit consumption_ref(bridge_state* state) : state_(state) {}  // adopts existing reference

  consumption_ref(consumption_ref&& other) : state_(other.state_) { other.state_ = nullptr; }

  ~consumption_ref() {
    if (state_)
      state_->drop_consumption_ref(false /*was_consumed*/);
  }

  consumption_ref& operator=(consumption_ref&& other) {
    if (&other == this)
      return *this;
    if (state_)
      state_->drop_consumption_ref(false /*was_consumed*/);
    state_ = other.state_;
    other.state_ = nullptr;
    return *this;
  }

  explicit operator bool() const { return !!state_; }

  bridge_state* get() const { return state_; }

  void drop_after_consumption() {
    state_->drop_consumption_ref(true /*was_consumed*/);
    state_ = nullptr;
  }

  consumption_ref(const consumption_ref& other) = delete;
  consumption_ref& operator=(const consumption_ref& other) = delete;

 private:
  bridge_state* state_;
};

// The continuation produced by |consumer::promise()| and company.
template <typename V, typename E>
class bridge_state<V, E>::promise_continuation final {
 public:
  explicit promise_continuation(consumption_ref ref) : ref_(std::move(ref)) {}

  promise_continuation(consumption_ref ref, result_type result_if_abandoned)
      : ref_(std::move(ref)) {
    ref_.get()->set_result_if_abandoned(std::move(result_if_abandoned));
  }

  result_type operator()(::fit::context& context) {
    return ref_.get()->await_result(&ref_, context);
  }

 private:
  consumption_ref ref_;
};

// The callback produced by |completer::bind()|.
template <typename V, typename E>
class bridge_bind_callback final {
  using callback_bridge_state = bridge_state<V, E>;

 public:
  explicit bridge_bind_callback(typename callback_bridge_state::completion_ref ref)
      : ref_(std::move(ref)) {}

  template <typename VV = V, typename = std::enable_if_t<std::is_void<VV>::value>>
  void operator()() {
    callback_bridge_state* state = ref_.get();
    state->complete_or_abandon(std::move(ref_), ::fit::ok());
  }

  template <typename VV = V, typename = std::enable_if_t<!std::is_void<VV>::value>>
  void operator()(VV value) {
    callback_bridge_state* state = ref_.get();
    state->complete_or_abandon(std::move(ref_), ::fit::ok<V>(std::forward<VV>(value)));
  }

 private:
  typename callback_bridge_state::completion_ref ref_;
};

// The callback produced by |completer::bind_tuple()|.
template <typename V, typename E>
class bridge_bind_tuple_callback;
template <typename... Args, typename E>
class bridge_bind_tuple_callback<std::tuple<Args...>, E> final {
  using tuple_callback_bridge_state = bridge_state<std::tuple<Args...>, E>;

 public:
  explicit bridge_bind_tuple_callback(typename tuple_callback_bridge_state::completion_ref ref)
      : ref_(std::move(ref)) {}

  void operator()(Args... args) {
    tuple_callback_bridge_state* state = ref_.get();
    state->complete_or_abandon(std::move(ref_),
                               ::fit::ok(std::make_tuple<Args...>(std::forward<Args>(args)...)));
  }

 private:
  typename tuple_callback_bridge_state::completion_ref ref_;
};

template <typename V, typename E>
void bridge_state<V, E>::create(completion_ref* out_completion_ref,
                                consumption_ref* out_consumption_ref) {
  bridge_state* state = new bridge_state();
  *out_completion_ref = completion_ref(state);
  *out_consumption_ref = consumption_ref(state);
}

template <typename V, typename E>
bool bridge_state<V, E>::was_canceled() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return disposition_ == disposition::canceled;
}

template <typename V, typename E>
bool bridge_state<V, E>::was_abandoned() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return disposition_ == disposition::abandoned;
}

template <typename V, typename E>
void bridge_state<V, E>::drop_completion_ref(bool was_completed) {
  suspended_task task_to_notify;
  bool should_resume_task = false;
  if (!was_completed) {
    // The task was abandoned.
    std::lock_guard<std::mutex> lock(mutex_);
    assert(disposition_ == disposition::pending || disposition_ == disposition::canceled);
    if (disposition_ == disposition::pending) {
      disposition_ = disposition::abandoned;
      task_to_notify.swap(task_);
      should_resume_task = !result_.is_pending();
    }
  }

  // Drop or resume |task_to_notify| and drop the ref outside of the lock.
  // This guards against re-entrance in case the consumption ref is
  // dropped as a side-effect of these operations.
  if (task_to_notify && should_resume_task) {
    task_to_notify.resume_task();
  }
  drop_ref_and_maybe_delete_self();
}

template <typename V, typename E>
void bridge_state<V, E>::drop_consumption_ref(bool was_consumed) {
  suspended_task task_to_drop;
  result_type result_to_drop;
  if (!was_consumed) {
    // The task was canceled.
    std::lock_guard<std::mutex> lock(mutex_);
    assert(disposition_ == disposition::pending || disposition_ == disposition::completed ||
           disposition_ == disposition::abandoned);
    if (disposition_ == disposition::pending) {
      disposition_ = disposition::canceled;
      task_to_drop.swap(task_);
      result_to_drop.swap(result_);
    }
  }

  // Drop |task_to_drop|, drop |result_to_drop|, and drop the ref
  // outside of the lock.
  // This guards against re-entrance in case the completion ref is
  // dropped as a side-effect of these operations.
  drop_ref_and_maybe_delete_self();
}

template <typename V, typename E>
void bridge_state<V, E>::drop_ref_and_maybe_delete_self() {
  uint32_t count = ref_count_.fetch_sub(1u, std::memory_order_release);
  assert(count > 0);
  if (count == 1) {
    std::atomic_thread_fence(std::memory_order_acquire);
    delete this;
  }
}

template <typename V, typename E>
void bridge_state<V, E>::complete_or_abandon(completion_ref ref, result_type result) {
  assert(ref.get() == this);
  if (result.is_pending())
    return;  // let the ref go out of scope to abandon the task

  suspended_task task_to_notify;
  bool should_resume_task = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    assert(disposition_ == disposition::pending || disposition_ == disposition::canceled);
    if (disposition_ == disposition::pending) {
      disposition_ = disposition::completed;
      result.swap(result_);
      task_to_notify.swap(task_);
      should_resume_task = !result_.is_pending();
    }
  }

  // Drop or resume |task_to_notify|, drop any prior result that
  // was swapped into |result|, and drop the ref outside of the lock.
  // This guards against re-entrance in case the consumption ref is
  // dropped as a side-effect of these operations.
  if (task_to_notify && should_resume_task) {
    task_to_notify.resume_task();
  }
  ref.drop_after_completion();
}

template <typename V, typename E>
void bridge_state<V, E>::set_result_if_abandoned(result_type result_if_abandoned) {
  if (result_if_abandoned.is_pending())
    return;  // nothing to do

  std::lock_guard<std::mutex> lock(mutex_);
  assert(disposition_ == disposition::pending || disposition_ == disposition::completed ||
         disposition_ == disposition::abandoned);
  if (disposition_ == disposition::pending || disposition_ == disposition::abandoned) {
    result_if_abandoned.swap(result_);
  }

  // Drop any prior value that was swapped into |result_if_abandoned|
  // outside of the lock.
}

template <typename V, typename E>
typename bridge_state<V, E>::result_type bridge_state<V, E>::await_result(consumption_ref* ref,
                                                                          ::fit::context& context) {
  assert(ref->get() == this);
  suspended_task task_to_drop;
  result_type result;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    assert(disposition_ == disposition::pending || disposition_ == disposition::completed ||
           disposition_ == disposition::abandoned);
    if (disposition_ == disposition::pending) {
      task_to_drop.swap(task_);
      task_ = context.suspend_task();  // assuming this isn't re-entrant
      return ::fit::pending();
    }
    disposition_ = disposition::returned;
    result = std::move(result_);
  }

  // Drop |task_to_drop| and the ref outside of the lock.
  ref->drop_after_consumption();
  return result;
}

}  // namespace internal

template <typename V = void, typename E = void>
class bridge;
template <typename V = void, typename E = void>
class completer;
template <typename V = void, typename E = void>
class consumer;

}  // namespace fit

#endif  // LIB_FIT_BRIDGE_INTERNAL_H_
