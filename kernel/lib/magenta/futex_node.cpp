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

// This removes up to |count| nodes from |list_head|.  It returns the new
// list head (i.e. the list of remaining nodes), which may be null (empty).
// On return, |list_head| is the list of nodes that were removed --
// |list_head| remains a valid list.
//
// This will always remove at least one node, because it requires that
// |count| is non-zero and |list_head| is a non-empty list.
FutexNode* FutexNode::RemoveFromHead(FutexNode* list_head, uint32_t count,
                                     uintptr_t old_hash_key,
                                     uintptr_t new_hash_key) {
    ASSERT(list_head);
    ASSERT(count != 0);

    FutexNode* node = list_head;
    FutexNode* last = nullptr;
    for (uint32_t i = 0; i < count && node != nullptr; i++) {
        DEBUG_ASSERT(node->GetKey() == old_hash_key);
        // For requeuing, update the key so that FutexWait() can remove the
        // thread from its current queue if the wait operation times out.
        node->set_hash_key(new_hash_key);

        last = node;
        node = node->next_;
    }

    if (node != nullptr) node->tail_ = list_head->tail_;
    last->next_ = nullptr;
    list_head->tail_ = last;
    return node;
}

status_t FutexNode::BlockThread(Mutex* mutex, mx_time_t timeout) {
    lk_time_t t = mx_time_to_lk(timeout);

    return cond_wait_timeout(&condvar_, mutex->GetInternal(), t);
}

void FutexNode::WakeKilledThread() {
    cond_signal(&condvar_);
}

void FutexNode::WakeThreads(FutexNode* head) {
    while (head != nullptr) {
        cond_signal(&head->condvar_);
        head = head->next_;
    }
}
