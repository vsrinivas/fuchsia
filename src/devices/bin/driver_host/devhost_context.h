// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_HOST_DEVHOST_CONTEXT_H_
#define SRC_DEVICES_BIN_DRIVER_HOST_DEVHOST_CONTEXT_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/fit/function.h>
#include <lib/zx/event.h>

#include <fbl/intrusive_double_list.h>
#include <fbl/mutex.h>
#include <fbl/ref_ptr.h>

#include "async_loop_owned_event_handler.h"
#include "zx_device.h"

class DevhostContext {
 public:
  using Callback = fit::inline_callback<void(void), 2 * sizeof(void*)>;

  explicit DevhostContext(const async_loop_config_t* config) : loop_(config) {}

  // Sets up event on async loop which gets triggered once
  zx_status_t SetupEventWaiter();

  // Queues up work item, and signals event to run it.
  void PushWorkItem(const fbl::RefPtr<zx_device_t>& dev, Callback callback);

  // Runs |how_many_to_run| work items. 0 Indicates that caller wishes to run all work items in
  // queue.
  void RunWorkItems(size_t how_many_to_run);

  async::Loop& loop() { return loop_; }

 private:
  struct WorkItem : public fbl::DoublyLinkedListable<std::unique_ptr<WorkItem>> {
    WorkItem(const fbl::RefPtr<zx_device_t>& dev, Callback callback)
        : dev(dev), callback(std::move(callback)) {}

    fbl::RefPtr<zx_device_t> dev;
    Callback callback;
  };

  class EventWaiter : public AsyncLoopOwnedEventHandler<EventWaiter> {
   public:
    EventWaiter(zx::event event, fit::closure callback)
        : AsyncLoopOwnedEventHandler<EventWaiter>(std::move(event)),
          callback_(std::move(callback)) {}

    static void HandleEvent(std::unique_ptr<EventWaiter> event, async_dispatcher_t* dispatcher,
                            async::WaitBase* wait, zx_status_t status,
                            const zx_packet_signal_t* signal);

    bool signaled() { return signaled_; }

    void signal() {
      ZX_ASSERT(event()->signal(0, ZX_USER_SIGNAL_0) == ZX_OK);
      signaled_ = true;
    }

    void designal() {
      ZX_ASSERT(event()->signal(ZX_USER_SIGNAL_0, 0) == ZX_OK);
      signaled_ = false;
    }

    void InvokeCallback() { callback_(); }

   private:
    bool signaled_ = false;
    fit::closure callback_;
  };

  // Runs |how_many_to_run| work items. 0 Indicates that caller wishes to run all work items in
  // queue.
  void InternalRunWorkItems(size_t how_many_to_run);

  async::Loop loop_;

  fbl::Mutex lock_;
  // Owned by `loop_`;
  EventWaiter* event_waiter_ TA_GUARDED(lock_) = nullptr;
  fbl::DoublyLinkedList<std::unique_ptr<WorkItem>> work_items_ TA_GUARDED(lock_);
};

#endif  // SRC_DEVICES_BIN_DRIVER_HOST_DEVHOST_CONTEXT_H_
