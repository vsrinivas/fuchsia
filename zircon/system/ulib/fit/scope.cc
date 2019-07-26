// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fit/scope.h>

namespace fit {

scope::scope() : state_(new state()) {}

scope::~scope() { state_->exit(true /*scope_was_destroyed*/); }

scope::state::state() = default;

scope::state::~state() {
  assert(acquired_promise_count_.load(std::memory_order_relaxed) == scope_exited);
  assert(scope_was_destroyed_);
  assert(promise_handle_count_ == 0);
  assert(head_promise_holder_ == nullptr);
}

bool scope::state::exited() const {
  return acquired_promise_count_.load(std::memory_order_relaxed) & scope_exited;
}

void scope::state::exit(bool scope_was_destroyed) {
  promise_holder_base* release_head = nullptr;
  bool delete_self = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    assert(!scope_was_destroyed_);
    scope_was_destroyed_ = scope_was_destroyed;

    // Atomically exit the scope.  We cannot do this safely if there are
    // any running promises since they might still be accessing state which
    // is guarded by the scope.  Worse, if a promise re-entrantly destroys
    // the scope during its execution then as a side-effect the promise
    // itself will be destroyed.  So assert!
    uint64_t prior_count =
        acquired_promise_count_.exchange(scope_exited, std::memory_order_relaxed);
    if (!(prior_count & scope_exited)) {
      // Cannot exit fit::scope while any of its promises are running!
      assert(prior_count == 0);

      // Take the promises so they can be deleted outside of the lock.
      release_head = head_promise_holder_;
      head_promise_holder_ = nullptr;
    }

    // If there are no more handles then we can delete the state now.
    delete_self = should_delete_self();
  }

  // Delete aborted promises and self outside of the lock.
  while (release_head) {
    promise_holder_base* release_next = release_head->next;
    delete release_head;
    release_head = release_next;
  }
  if (delete_self) {
    delete this;
  }
}

scope::promise_handle scope::state::adopt_promise(promise_holder_base* promise_holder) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    assert(!scope_was_destroyed_);  // otherwise how did we get here?

    // If the scope hasn't been exited yet, link the promise and mint
    // a new handle.  Otherwise we will abort the promise.
    if (!exited()) {
      if (head_promise_holder_) {
        head_promise_holder_->prev = promise_holder;
        promise_holder->next = head_promise_holder_;
      }
      head_promise_holder_ = promise_holder;
      promise_handle_count_++;
      return promise_handle(this, promise_holder);
    }
  }

  // Delete aborted promise outside of the lock.
  delete promise_holder;
  return promise_handle{};
}

void scope::state::drop_promise(promise_handle promise_handle) {
  if (!promise_handle.promise_holder_) {
    return;  // invalid handle, nothing to do
  }

  {
    std::lock_guard<std::mutex> lock(promise_handle.state_->mutex_);

    // If the scope hasn't been exited yet, unlink the promise and
    // prepare to destroy it.  Otherwise, it's already been unlinked
    // and destroyed so release the handle but don't touch the pointer!
    assert(promise_handle.state_->promise_handle_count_ > 0);
    promise_handle.state_->promise_handle_count_--;
    if (!promise_handle.state_->exited()) {
      if (promise_handle.promise_holder_->next) {
        promise_handle.promise_holder_->next->prev = promise_handle.promise_holder_->prev;
      }
      if (promise_handle.promise_holder_->prev) {
        promise_handle.promise_holder_->prev->next = promise_handle.promise_holder_->next;
      } else {
        promise_handle.state_->head_promise_holder_ = promise_handle.promise_holder_->next;
      }
      // Fallthrough to delete the promise.
    } else if (!promise_handle.state_->should_delete_self()) {
      return;
    } else {
      // Fallthrough to delete self.
      promise_handle.promise_holder_ = nullptr;
    }
  }

  // Delete the promise or scope outside of the lock.
  if (promise_handle.promise_holder_) {
    delete promise_handle.promise_holder_;
  } else {
    delete promise_handle.state_;
  }
}

scope::promise_holder_base* scope::state::try_acquire_promise(promise_handle promise_handle) {
  if (promise_handle.promise_holder_) {
    uint64_t prior_count =
        promise_handle.state_->acquired_promise_count_.fetch_add(1u, std::memory_order_relaxed);
    if (!(prior_count & scope_exited)) {
      return promise_handle.promise_holder_;
    }
    promise_handle.state_->acquired_promise_count_.fetch_sub(1u, std::memory_order_relaxed);
  }
  return nullptr;
}

void scope::state::release_promise(promise_handle promise_handle) {
  promise_handle.state_->acquired_promise_count_.fetch_sub(1u, std::memory_order_relaxed);
}

}  // namespace fit
