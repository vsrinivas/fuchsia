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

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_WORKQUEUE_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_WORKQUEUE_H_

#define _ALL_SOURCE

#include <lib/sync/completion.h>
#include <string.h>
#include <threads.h>
#include <zircon/listnode.h>
#include <zircon/syscalls.h>
#include <zircon/threads.h>

#include <memory>
#include <mutex>

class WorkQueue;
class WorkItem;
typedef void (*work_handler_t)(WorkItem* work);

class WorkItem {
 public:
  explicit WorkItem(void (*handler)(WorkItem* work));
  // Default constructor for structs which contain it, might be removed after all the things get
  // into C++.
  WorkItem() {}
  // If work isn't started, deletes it. If it was started, waits for it to finish. Thus, this may
  // block. Either way, the work is guaranteed not to be running after workqueue_cancel_work
  // returns.
  void Cancel();

  // The callback function.
  void (*handler)(WorkItem*);
  // signaler: If not ZX_HANDLE_INVALID, will be signaled WORKQUEUE_SIGNAL on completion of work.
  zx_handle_t signaler;
  // item: If work is queued, item is the link to the work list.
  list_node_t item;
  // workqueue: The work queue currently queued or executing on.
  WorkQueue* workqueue;
};

class WorkQueue {
 public:
  explicit WorkQueue(const char* name);

  // This is a static member which can be accessed globally.
  static WorkQueue& DefaultInstance();
  static void FlushDefault(void) { DefaultInstance().Flush(); }
  static void ScheduleDefault(WorkItem* work);

  // Waits for any work on workqueue at time of call to complete. Jobs scheduled after flush starts,
  // including work scheduled by pre-flush work, will not be waited for.
  void Flush();
  void Schedule(WorkItem* work);

  ~WorkQueue();

  static constexpr int kWorkqueueNameMaxlen = 64;
  std::mutex lock_;
  list_node_t list_;
  WorkItem* current_;

 private:
  sync_completion_t work_ready_;
  char name_[kWorkqueueNameMaxlen];
  thrd_t thread_;

  // Thread body for the WorkQueue execution thread.
  int Runner();

  // Start the WorkQueue thread.
  void StartWorkQueue();
};

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_WORKQUEUE_H_
