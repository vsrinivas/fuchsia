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

namespace fit {
namespace internal {

// State shared between one completer and one consumer.
// This object is somewhat unusual in that it has dual-ownership represented
// by a pair of single-ownership references: a |completion_ref| and a
// |consumption_ref|.
//
// The bridge's state evolves as follows:
// - Initially the bridge's disposition is "pending".
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

    static void create(completion_ref* out_completion_ref,
                       consumption_ref* out_consumption_ref);

    // This method is thread-safe.
    void complete_or_abandon(completion_ref ref, result_type result);

    // This method is thread-safe.
    promise_continuation promise_or(
        consumption_ref ref, result_type result_if_abandoned);

    bridge_state(const bridge_state&) = delete;
    bridge_state(bridge_state&&) = delete;
    bridge_state& operator=(const bridge_state&) = delete;
    bridge_state& operator=(bridge_state&&) = delete;

private:
    enum class disposition {
        pending,
        abandoned,
        canceled,
        returned
    };

    bridge_state() = default;

    void drop_completion_ref();
    void drop_consumption_ref();

    result_type await_result(::fit::context& context);
    void deliver_result();

    std::mutex mutex_;

    // Combined ref-count for completion and consumption.
    // There can only be one of each ref type so they each get a bit.
    // We increment by two for completion and by one for consumption.
    // That makes the initial value three.
    std::atomic<int32_t> ref_mask_{1 | 2};

    // The disposition of the bridge.
    disposition disposition_{disposition::pending}; // guarded by mutex_

    // The suspended task.
    // Invariant: Only valid when disposition is |pending|.
    suspended_task task_; // guarded by mutex_

    // The result in flight.
    // Invariant: Only valid when disposition is |pending| or |abandoned|.
    result_type result_; // guarded by mutex_
};

// The unique capability held by a bridge's completer.
template <typename V, typename E>
class bridge_state<V, E>::completion_ref final {
public:
    completion_ref()
        : state_(nullptr) {}
    explicit completion_ref(bridge_state* state)
        : state_(state) {} // adopts existing reference
    completion_ref(completion_ref&& other)
        : state_(other.state_) {
        other.state_ = nullptr;
    }
    ~completion_ref() {
        if (state_)
            state_->drop_completion_ref();
    }
    completion_ref& operator=(completion_ref&& other) {
        if (&other == this)
            return *this;
        if (state_)
            state_->drop_completion_ref();
        state_ = other.state_;
        other.state_ = nullptr;
        return *this;
    }
    explicit operator bool() const { return !!state_; }
    bridge_state* get() const { return state_; }

    completion_ref(const completion_ref& other) = delete;
    completion_ref& operator=(const completion_ref& other) = delete;

private:
    bridge_state* state_;
};

// The unique capability held by a bridge's consumer.
template <typename V, typename E>
class bridge_state<V, E>::consumption_ref final {
public:
    consumption_ref()
        : state_(nullptr) {}
    explicit consumption_ref(bridge_state* state)
        : state_(state) {} // adopts existing reference
    consumption_ref(consumption_ref&& other)
        : state_(other.state_) {
        other.state_ = nullptr;
    }
    ~consumption_ref() {
        if (state_)
            state_->drop_consumption_ref();
    }
    consumption_ref& operator=(consumption_ref&& other) {
        if (&other == this)
            return *this;
        if (state_)
            state_->drop_consumption_ref();
        state_ = other.state_;
        other.state_ = nullptr;
        return *this;
    }
    explicit operator bool() const { return !!state_; }
    bridge_state* get() const { return state_; }

    consumption_ref(const consumption_ref& other) = delete;
    consumption_ref& operator=(const consumption_ref& other) = delete;

private:
    bridge_state* state_;
};

// The continuation produced by |consumer::promise_or()|.
template <typename V, typename E>
class bridge_state<V, E>::promise_continuation final {
public:
    explicit promise_continuation(consumption_ref ref)
        : ref_(std::move(ref)) {}

    result_type operator()(::fit::context& context) {
        return ref_.get()->await_result(context);
    }

private:
    consumption_ref ref_;
};

// The callback produced by |completer::bind()|.
template <typename V, typename E>
class bridge_bind_callback final {
    using bridge_state = bridge_state<V, E>;

public:
    explicit bridge_bind_callback(typename bridge_state::completion_ref ref)
        : ref_(std::move(ref)) {}

    template <typename VV = V,
              typename = std::enable_if_t<std::is_void<VV>::value>>
    void operator()() {
        bridge_state* state = ref_.get();
        state->complete_or_abandon(std::move(ref_), ::fit::ok());
    }

    template <typename VV = V,
              typename = std::enable_if_t<!std::is_void<VV>::value>>
    void operator()(VV value) {
        bridge_state* state = ref_.get();
        state->complete_or_abandon(std::move(ref_),
                                   ::fit::ok<V>(std::forward<VV>(value)));
    }

private:
    typename bridge_state::completion_ref ref_;
};

// The callback produced by |completer::bind_tuple()|.
template <typename V, typename E>
class bridge_bind_tuple_callback;
template <typename... Args, typename E>
class bridge_bind_tuple_callback<std::tuple<Args...>, E> final {
    using bridge_state = bridge_state<std::tuple<Args...>, E>;

public:
    explicit bridge_bind_tuple_callback(typename bridge_state::completion_ref ref)
        : ref_(std::move(ref)) {}

    void operator()(Args... args) {
        bridge_state* state = ref_.get();
        state->complete_or_abandon(
            std::move(ref_),
            ::fit::ok(std::make_tuple<Args...>(std::forward<Args>(args)...)));
    }

private:
    typename bridge_state::completion_ref ref_;
};

template <typename V, typename E>
void bridge_state<V, E>::create(completion_ref* out_completion_ref,
                                consumption_ref* out_consumption_ref) {
    bridge_state* state = new bridge_state();
    *out_completion_ref = completion_ref(state);
    *out_consumption_ref = consumption_ref(state);
}

template <typename V, typename E>
void bridge_state<V, E>::drop_completion_ref() {
    int32_t count = ref_mask_.fetch_sub(2, std::memory_order_release) - 2;
    // assert(count >= 0);
    if (count != 0) {
        // The task has been abandoned.
        std::lock_guard<std::mutex> lock(mutex_);
        disposition_ = disposition::abandoned;
        deliver_result();
    } else {
        // Both parties gone.
        std::atomic_thread_fence(std::memory_order_acquire);
        delete this;
    }
}

template <typename V, typename E>
void bridge_state<V, E>::drop_consumption_ref() {
    int32_t count = ref_mask_.fetch_sub(1, std::memory_order_release) - 1;
    // assert(count >= 0);
    if (count != 0) {
        // The task has been canceled.
        std::lock_guard<std::mutex> lock(mutex_);
        disposition_ = disposition::canceled;
        result_ = ::fit::pending();
        task_.reset(); // there is no task to wake up anymore
    } else {
        // Both parties gone.
        std::atomic_thread_fence(std::memory_order_acquire);
        delete this;
    }
}

template <typename V, typename E>
void bridge_state<V, E>::complete_or_abandon(completion_ref ref,
                                             result_type result) {
    // assert(ref.get() == this);
    if (result.is_pending())
        return; // abandoned, let the ref go out of scope to clean up
    std::lock_guard<std::mutex> lock(mutex_);
    assert(disposition_ == disposition::pending ||
           disposition_ == disposition::canceled);
    if (disposition_ == disposition::pending) {
        result_ = std::move(result);
        deliver_result();
    }
}

template <typename V, typename E>
typename bridge_state<V, E>::promise_continuation
bridge_state<V, E>::promise_or(
    consumption_ref ref, result_type result_if_abandoned) {
    // assert(ref.get() == this);
    if (!result_if_abandoned.is_pending()) {
        std::lock_guard<std::mutex> lock(mutex_);
        assert(disposition_ == disposition::pending ||
               disposition_ == disposition::abandoned);
        if (result_.is_pending())
            result_ = std::move(result_if_abandoned);
    }
    return promise_continuation(std::move(ref));
}

template <typename V, typename E>
typename bridge_state<V, E>::result_type bridge_state<V, E>::await_result(
    ::fit::context& context) {
    std::lock_guard<std::mutex> lock(mutex_);
    assert(disposition_ == disposition::pending ||
           disposition_ == disposition::abandoned);
    if (disposition_ == disposition::pending) {
        task_ = context.suspend_task();
        return ::fit::pending();
    }
    disposition_ = disposition::returned;
    return std::move(result_);
}

template <typename V, typename E>
void bridge_state<V, E>::deliver_result() {
    if (result_.is_pending()) {
        task_.reset(); // the task has been canceled
    } else {
        task_.resume_task(); // we have a result so wake up the task
    }
}

} // namespace internal

template <typename V = void, typename E = void>
class bridge;
template <typename V = void, typename E = void>
class completer;
template <typename V = void, typename E = void>
class consumer;

} // namespace fit

#endif // LIB_FIT_BRIDGE_INTERNAL_H_
