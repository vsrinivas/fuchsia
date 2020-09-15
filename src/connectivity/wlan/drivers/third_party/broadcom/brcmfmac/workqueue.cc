/*
 * Copyright (c) 2019 The Fuchsia Authors
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "workqueue.h"

#include <zircon/assert.h>

#include "debug.h"

#define WORKQUEUE_SIGNAL ZX_USER_SIGNAL_0

WorkQueue::WorkQueue(const char* name) : list_{}, current_(nullptr), work_ready_{}, thread_{} {
  if (name == nullptr) {
    strlcpy(this->name_, "nameless", sizeof(name_));
  } else {
    strlcpy(this->name_, name, sizeof(name_));
  }
  StartWorkQueue();
}

WorkQueue::~WorkQueue() {
  auto work = WorkItem([](WorkItem* work) { thrd_exit(0); });
  Schedule(&work);
  thrd_join(thread_, nullptr);
}

WorkQueue& WorkQueue::DefaultInstance() {
  static WorkQueue default_workqueue_("default_workqueue");
  return default_workqueue_;
}

void WorkQueue::ScheduleDefault(WorkItem* work) {
  if (work == nullptr) {
    return;
  }
  DefaultInstance().Schedule(work);
}

void WorkQueue::Flush() {
  auto work = WorkItem([](WorkItem* work) {});
  zx_status_t result;
  result = zx_event_create(0, &work.signaler);
  if (result != ZX_OK) {
    BRCMF_ERR("Failed to create signal (work not canceled)");
    return;
  }
  Schedule(&work);
  zx_signals_t observed;
  result = zx_object_wait_one(work.signaler, WORKQUEUE_SIGNAL, ZX_TIME_INFINITE, &observed);
  if (result != ZX_OK || (observed & WORKQUEUE_SIGNAL) == 0) {
    BRCMF_ERR("Bad return from wait (work likely not flushed): result %d, observed %x", result,
              observed);
  }
  zx_handle_close(work.signaler);
}

void WorkQueue::Schedule(WorkItem* work) {
  if (work == nullptr) {
    return;
  }
  list_node_t* node;
  lock_.lock();

  if (current_ == work) {
    lock_.unlock();
    return;
  }
  list_for_every(&list_, node) {
    if (node == &work->item) {
      lock_.unlock();
      return;
    }
  }
  work->workqueue = this;
  list_add_tail(&list_, &work->item);
  sync_completion_signal(&work_ready_);
  lock_.unlock();
}

int WorkQueue::Runner() {
  while (true) {
    // When all the works are consumed, these two lines will block the thread.
    sync_completion_wait(&work_ready_, ZX_TIME_INFINITE);
    sync_completion_reset(&work_ready_);
    list_node_t* item;
    lock_.lock();
    item = list_remove_head(&list_);
    current_ = (item == nullptr) ? nullptr : containerof(item, WorkItem, item);
    lock_.unlock();
    while (current_ != nullptr) {
      current_->handler(current_);
      lock_.lock();
      if (current_->signaler != ZX_HANDLE_INVALID) {
        zx_object_signal(current_->signaler, 0, WORKQUEUE_SIGNAL);
      }
      item = list_remove_head(&list_);
      current_ = (item == nullptr) ? nullptr : containerof(item, WorkItem, item);
      lock_.unlock();
    }
  }
}

void WorkQueue::StartWorkQueue() {
  work_ready_ = {};
  list_initialize(&list_);
  auto thread_func = [](void* arg) { return reinterpret_cast<WorkQueue*>(arg)->Runner(); };
  int status = thrd_create_with_name(&thread_, thread_func, this, name_);
  ZX_ASSERT_MSG(status == thrd_success, "Failed to create WorkQ thread name: %s status: %d", name_,
                status);
}

WorkItem::WorkItem() : WorkItem(nullptr) {}

WorkItem::WorkItem(void (*handler)(WorkItem* work))
    : handler(handler), signaler(ZX_HANDLE_INVALID), workqueue(nullptr) {
  list_initialize(&item);
}

void WorkItem::Cancel() {
  WorkQueue* wq = workqueue;
  if (wq == nullptr) {
    return;
  }
  zx_status_t result;
  wq->lock_.lock();
  if (wq->current_ == this) {
    result = zx_event_create(0, &signaler);
    wq->lock_.unlock();
    if (result != ZX_OK) {
      BRCMF_ERR("Failed to create signal (work not canceled)");
      return;
    }
    zx_signals_t observed;
    result = zx_object_wait_one(signaler, WORKQUEUE_SIGNAL, ZX_TIME_INFINITE, &observed);
    if (result != ZX_OK || (observed & WORKQUEUE_SIGNAL) == 0) {
      BRCMF_ERR("Bad return from wait (work likely not canceled): result %d, observed %x", result,
                observed);
    }
    wq->lock_.lock();
    zx_handle_close(signaler);
    signaler = ZX_HANDLE_INVALID;
    wq->lock_.unlock();
    return;
  } else {
    list_node_t* node;
    list_node_t* temp_node;
    list_for_every_safe(&(wq->list_), node, temp_node) {
      if (node == &item) {
        list_delete(node);
        wq->lock_.unlock();
        return;
      }
    }
    wq->lock_.unlock();
    BRCMF_DBG(TEMP, "Work to be canceled not found");
  }
}
