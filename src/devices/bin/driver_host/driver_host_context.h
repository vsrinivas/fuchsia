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
#include "lock.h"
#include "zx_device.h"

class DriverHostContext {
 public:
  using Callback = fit::inline_callback<void(void), 2 * sizeof(void*)>;

  explicit DriverHostContext(const async_loop_config_t* config) : loop_(config) {}

  zx_status_t SetupRootDevcoordinatorConnection(zx::channel ch);

  zx_status_t ScheduleWork(const fbl::RefPtr<zx_device_t>& dev, void (*callback)(void*),
                           void* cookie) REQ_DM_LOCK;

  zx_status_t StartConnection(fbl::RefPtr<DevfsConnection> ios, zx::channel h);

  void ProxyIosDestroy(const fbl::RefPtr<zx_device_t>& dev);

  // Attaches channel |c| to new state representing an open connection to |dev|.
  zx_status_t DeviceConnect(const fbl::RefPtr<zx_device_t>& dev, uint32_t flags, zx::channel c);

  // routines driver_host uses to talk to driver_manager
  // |client_remote| will only be a valid handle if the device was added with
  // DEVICE_ADD_INVISIBLE or DEVICE_ADD_MUST_ISOLATE.
  zx_status_t DriverManagerAdd(const fbl::RefPtr<zx_device_t>& dev,
                               const fbl::RefPtr<zx_device_t>& child, const char* proxy_args,
                               const zx_device_prop_t* props, uint32_t prop_count,
                               zx::channel client_remote) REQ_DM_LOCK;
  // Note that DriverManagerRemove() takes a RefPtr rather than a const RefPtr&.
  // It intends to consume a reference.
  zx_status_t DriverManagerRemove(fbl::RefPtr<zx_device_t> dev) REQ_DM_LOCK;

  // |client_remote| will only be a valid handle if the device was added with
  // DEVICE_ADD_INVISIBLE or DEVICE_ADD_MUST_ISOLATE.
  zx_status_t DeviceAdd(const fbl::RefPtr<zx_device_t>& dev, const fbl::RefPtr<zx_device_t>& parent,
                        const zx_device_prop_t* props, uint32_t prop_count, const char* proxy_args,
                        zx::channel client_remote) REQ_DM_LOCK;

  zx_status_t DeviceInit(const fbl::RefPtr<zx_device_t>& dev) REQ_DM_LOCK;
  void DeviceInitReply(const fbl::RefPtr<zx_device_t>& dev, zx_status_t status,
                       const device_init_reply_args_t* args) REQ_DM_LOCK;
  // TODO(fxb/34574): this should be removed once device_remove() is removed.
  zx_status_t DeviceRemoveDeprecated(const fbl::RefPtr<zx_device_t>& dev) REQ_DM_LOCK;
  zx_status_t DeviceRemove(const fbl::RefPtr<zx_device_t>& dev,
                           bool unbind_self = false) REQ_DM_LOCK;
  zx_status_t DeviceCompleteRemoval(const fbl::RefPtr<zx_device_t>& dev) REQ_DM_LOCK;

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
