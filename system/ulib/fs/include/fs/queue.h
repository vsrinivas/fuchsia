// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/intrusive_single_list.h>
#include <fbl/macros.h>

namespace fs {

// A wrapper around a singly linked list to make it appear as a queue.
// We pop from the front (moving the head forward) and push onto the tail.
template <typename PtrType>
class Queue {
public:
    using QueueType = fbl::SinglyLinkedList<PtrType>;
    Queue() : next_(queue_.end()) {}
    ~Queue() { ZX_DEBUG_ASSERT(is_empty()); }

    template <typename T>
    void push(T&& ptr) {
        if (queue_.is_empty()) {
            queue_.push_front(fbl::forward<T>(ptr));
            next_ = queue_.begin();
        } else {
            auto to_be_next = queue_.make_iterator(*ptr);
            queue_.insert_after(next_, fbl::forward<T>(ptr));
            next_ = to_be_next;
        }
    }

    typename QueueType::PtrTraits::RefType      front()       { return queue_.front(); }
    typename QueueType::PtrTraits::ConstRefType front() const { return queue_.front(); }

    PtrType pop() { return queue_.pop_front(); }

    bool is_empty() const { return queue_.is_empty(); }

private:
    // Add work to the front of the queue, remove work from the back
    QueueType queue_;
    typename QueueType::iterator next_;
};

}
