// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <err.h>
#include <magenta/futex_node.h>
#include <magenta/magenta.h>
#include <trace.h>

#define LOCAL_TRACE 0

FutexNode::FutexNode() : next_(nullptr), tail_(nullptr) {
    LTRACE_ENTRY;

    cond_init(&condvar_);
}

FutexNode::~FutexNode() {
    LTRACE_ENTRY;

    cond_destroy(&condvar_);
}

void FutexNode::AppendList(FutexNode* head) {
    // tail of blocked thread list must be non-null and must have null next_ pointer
    DEBUG_ASSERT(tail_ != nullptr);
    DEBUG_ASSERT(tail_->next_ == nullptr);
    tail_->next_ = head;
    tail_ = head->tail();
}

// remove up to |count| nodes from our head and return new head
// the removed nodes remain a valid list after this operation
FutexNode* FutexNode::RemoveFromHead(uint32_t count, uintptr_t old_hash_key,
                                     uintptr_t new_hash_key) {
    if (count == 0) return this;

    FutexNode* node = this;
    FutexNode* last = this;
    for (uint32_t i = 0; i < count && node != nullptr; i++) {
        DEBUG_ASSERT(node->GetKey() == old_hash_key);
        // For requeuing, update the key so that FutexWait() can remove the
        // thread from its current queue if the wait operation times out.
        node->set_hash_key(new_hash_key);

        last = node;
        node = node->next_;
    }

    if (node != nullptr) node->tail_ = tail_;
    last->next_ = nullptr;
    tail_ = last;
    return node;
}

status_t FutexNode::BlockThread(mutex_t* mutex, mx_time_t timeout) {
    lk_time_t t = mx_time_to_lk(timeout);

    return cond_wait_timeout(&condvar_, mutex, t);
}

void FutexNode::WakeThreads(FutexNode* head) {
    while (head != nullptr) {
        cond_signal(&head->condvar_);
        head = head->next_;
    }
}
