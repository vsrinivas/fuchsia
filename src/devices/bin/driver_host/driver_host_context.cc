// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "driver_host_context.h"

#include <stdio.h>
#include <zircon/status.h>

#include <fbl/auto_lock.h>
#include <fs/vfs.h>

#include "src/devices/lib/log/log.h"

void DriverHostContext::PushWorkItem(const fbl::RefPtr<zx_device_t>& dev, Callback callback) {
  auto work_item = std::make_unique<WorkItem>(dev, std::move(callback));

  fbl::AutoLock al(&lock_);
  work_items_.push_back(std::move(work_item));

  // TODO(surajmalhotra): Only signal if not being run in main driver_host thread as a slight
  // optimization (assuming we will run work items before going back to waiting on the port).
  if (!event_waiter_->signaled()) {
    event_waiter_->signal();
  }
}

void DriverHostContext::InternalRunWorkItems(size_t how_many_to_run) {
  {
    fbl::AutoLock al(&lock_);
    if (event_waiter_->signaled()) {
      event_waiter_->designal();
    }
  }

  size_t work_items_run = 0;
  auto keep_running = [&]() { return work_items_run < how_many_to_run || how_many_to_run == 0; };
  do {
    fbl::DoublyLinkedList<std::unique_ptr<WorkItem>> work_items;
    {
      fbl::AutoLock al(&lock_);
      work_items = std::move(work_items_);
    }

    if (work_items.is_empty()) {
      return;
    }

    std::unique_ptr<WorkItem> work_item;
    while (keep_running() && (work_item = work_items.pop_front())) {
      work_item->callback();
      work_items_run++;
    }

    if (!work_items.is_empty()) {
      fbl::AutoLock al(&lock_);
      work_items_.splice(work_items_.begin(), work_items);
    }
  } while (keep_running());

  fbl::AutoLock al(&lock_);
  if (!work_items_.is_empty() && !event_waiter_->signaled()) {
    event_waiter_->signal();
  }
}

void DriverHostContext::RunWorkItems(size_t how_many_to_run) {
  std::unique_ptr<EventWaiter> event_waiter;
  {
    fbl::AutoLock al(&lock_);
    ZX_DEBUG_ASSERT(event_waiter_ != nullptr);
    if (work_items_.is_empty()) {
      return;
    }
    event_waiter = event_waiter_->Cancel();
  }

  InternalRunWorkItems(how_many_to_run);
  EventWaiter::BeginWait(std::move(event_waiter), loop_.dispatcher());
}

zx_status_t DriverHostContext::SetupEventWaiter() {
  zx::event event;
  if (zx_status_t status = zx::event::create(0, &event); status != ZX_OK) {
    return status;
  }
  // TODO(surajmalhotra): Tune this value.
  constexpr uint32_t kBatchSize = 5;
  auto event_waiter = std::make_unique<EventWaiter>(std::move(event),
                                                    [this]() { InternalRunWorkItems(kBatchSize); });
  {
    fbl::AutoLock al(&lock_);
    event_waiter_ = event_waiter.get();
  }

  return EventWaiter::BeginWait(std::move(event_waiter), loop_.dispatcher());
}

void DriverHostContext::EventWaiter::HandleEvent(std::unique_ptr<EventWaiter> event_waiter,
                                                 async_dispatcher_t* dispatcher,
                                                 async::WaitBase* wait, zx_status_t status,
                                                 const zx_packet_signal_t* signal) {
  if (status != ZX_OK) {
    LOGF(ERROR, "Failed to wait for event: %s", zx_status_get_string(status));
    return;
  }

  if (signal->observed & ZX_USER_SIGNAL_0) {
    event_waiter->InvokeCallback();
    BeginWait(std::move(event_waiter), dispatcher);
  } else {
    LOGF(FATAL, "Unexpected signal state %#08x", signal->observed);
  }
}

zx_status_t DriverHostContext::DeviceConnect(const fbl::RefPtr<zx_device_t>& dev, uint32_t flags,
                                             zx::channel c) {
  auto options = fs::VnodeConnectionOptions::FromIoV1Flags(flags);

  fbl::RefPtr<fs::Vnode> target;
  zx_status_t status = dev->vnode->OpenValidating(options, &target);
  if (status != ZX_OK) {
    return status;
  }
  if (target == nullptr) {
    target = dev->vnode;
  }

  return vfs_.Serve(std::move(target), std::move(c), options);
}
