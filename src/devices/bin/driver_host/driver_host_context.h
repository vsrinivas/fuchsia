// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_HOST_DRIVER_HOST_CONTEXT_H_
#define SRC_DEVICES_BIN_DRIVER_HOST_DRIVER_HOST_CONTEXT_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/fit/function.h>
#include <lib/zx/event.h>
#include <lib/zx/resource.h>

#include <fbl/intrusive_double_list.h>
#include <fbl/mutex.h>
#include <fbl/ref_ptr.h>
#include <fs/managed_vfs.h>

#include "async_loop_owned_event_handler.h"
#include "lock.h"
#include "zx_device.h"
#include "zx_driver.h"

class DriverHostContext {
 public:
  using Callback = fit::inline_callback<void(void), 2 * sizeof(void*)>;

  explicit DriverHostContext(const async_loop_config_t* config, zx::resource root_resource = {})
      : loop_(config), vfs_(loop_.dispatcher()), root_resource_(std::move(root_resource)) {}

  zx_status_t SetupRootDevcoordinatorConnection(zx::channel ch);

  zx_status_t ScheduleWork(const fbl::RefPtr<zx_device_t>& dev, void (*callback)(void*),
                           void* cookie) TA_REQ(api_lock_);

  void ProxyIosDestroy(const fbl::RefPtr<zx_device_t>& dev);

  // Attaches channel |c| to new state representing an open connection to |dev|.
  zx_status_t DeviceConnect(const fbl::RefPtr<zx_device_t>& dev, uint32_t flags, zx::channel c);

  // routines driver_host uses to talk to driver_manager
  // |client_remote| will only be a valid handle if the device was added with
  // DEVICE_ADD_INVISIBLE or DEVICE_ADD_MUST_ISOLATE.
  zx_status_t DriverManagerAdd(const fbl::RefPtr<zx_device_t>& dev,
                               const fbl::RefPtr<zx_device_t>& child, const char* proxy_args,
                               const zx_device_prop_t* props, uint32_t prop_count,
                               zx::channel client_remote) TA_REQ(api_lock_);
  // Note that DriverManagerRemove() takes a RefPtr rather than a const RefPtr&.
  // It intends to consume a reference.
  zx_status_t DriverManagerRemove(fbl::RefPtr<zx_device_t> dev) TA_REQ(api_lock_);

  // |client_remote| will only be a valid handle if the device was added with
  // DEVICE_ADD_INVISIBLE or DEVICE_ADD_MUST_ISOLATE.
  zx_status_t DeviceAdd(const fbl::RefPtr<zx_device_t>& dev, const fbl::RefPtr<zx_device_t>& parent,
                        const zx_device_prop_t* props, uint32_t prop_count, const char* proxy_args,
                        zx::channel client_remote) TA_REQ(api_lock_);

  zx_status_t DeviceInit(const fbl::RefPtr<zx_device_t>& dev) TA_REQ(api_lock_);
  void DeviceInitReply(const fbl::RefPtr<zx_device_t>& dev, zx_status_t status,
                       const device_init_reply_args_t* args) TA_REQ(api_lock_);
  // TODO(fxb/34574): this should be removed once device_remove() is removed.
  zx_status_t DeviceRemoveDeprecated(const fbl::RefPtr<zx_device_t>& dev) TA_REQ(api_lock_);
  zx_status_t DeviceRemove(const fbl::RefPtr<zx_device_t>& dev, bool unbind_self = false)
      TA_REQ(api_lock_);
  zx_status_t DeviceCompleteRemoval(const fbl::RefPtr<zx_device_t>& dev) TA_REQ(api_lock_);
  zx_status_t DeviceUnbind(const fbl::RefPtr<zx_device_t>& dev) TA_REQ(api_lock_);
  void DeviceUnbindReply(const fbl::RefPtr<zx_device_t>& dev) TA_REQ(api_lock_);
  zx_status_t DeviceBind(const fbl::RefPtr<zx_device_t>& dev, const char* drv_libname)
      TA_REQ(api_lock_);
  zx_status_t DeviceRebind(const fbl::RefPtr<zx_device_t>& dev) TA_REQ(api_lock_);
  void DeviceSuspendNew(const fbl::RefPtr<zx_device_t>& dev,
                        ::llcpp::fuchsia::device::DevicePowerState requested_state);
  void DeviceSuspendReply(const fbl::RefPtr<zx_device_t>& dev, zx_status_t status,
                          uint8_t out_state) TA_REQ(api_lock_);
  void DeviceResumeNew(const fbl::RefPtr<zx_device_t>& dev);
  void DeviceResumeReply(const fbl::RefPtr<zx_device_t>& dev, zx_status_t status,
                         uint8_t out_power_state, uint32_t out_perf_state) TA_REQ(api_lock_);
  zx_status_t DeviceRunCompatibilityTests(const fbl::RefPtr<zx_device_t>& dev,
                                          int64_t hook_wait_time) TA_REQ(api_lock_);
  zx_status_t DeviceCreate(zx_driver_t* drv, const char* name, void* ctx,
                           const zx_protocol_device_t* ops, fbl::RefPtr<zx_device_t>* out)
      TA_REQ(api_lock_);
  zx_status_t DeviceOpen(const fbl::RefPtr<zx_device_t>& dev, fbl::RefPtr<zx_device_t>* out,
                         uint32_t flags) TA_REQ(api_lock_);
  zx_status_t DeviceClose(fbl::RefPtr<zx_device_t> dev, uint32_t flags) TA_REQ(api_lock_);
  void DeviceSystemSuspend(const fbl::RefPtr<zx_device_t>& dev, uint32_t flags) TA_REQ(api_lock_);
  zx_status_t DeviceSetPerformanceState(const fbl::RefPtr<zx_device_t>& dev,
                                        uint32_t requested_state, uint32_t* out_state);
  zx_status_t DeviceConfigureAutoSuspend(
      const fbl::RefPtr<zx_device_t>& dev, bool enable,
      ::llcpp::fuchsia::device::DevicePowerState requested_state);
  void DeviceSystemResume(const fbl::RefPtr<zx_device_t>& dev, uint32_t target_system_state)
      TA_REQ(api_lock_);
  void DeviceDestroy(zx_device_t* dev) TA_REQ(api_lock_);

  // routines driver_host uses to talk to dev coordinator
  zx_status_t ScheduleRemove(const fbl::RefPtr<zx_device_t>& dev, bool unbind_self)
      TA_REQ(api_lock_);
  zx_status_t ScheduleUnbindChildren(const fbl::RefPtr<zx_device_t>& dev) TA_REQ(api_lock_);
  void MakeVisible(const fbl::RefPtr<zx_device_t>& dev, const device_make_visible_args_t* args);

  zx_status_t LoadFirmware(const fbl::RefPtr<zx_device_t>& dev, const char* path,
                           zx_handle_t* vmo_handle, size_t* size);
  zx_status_t GetTopoPath(const fbl::RefPtr<zx_device_t>& dev, char* path, size_t max,
                          size_t* actual);
  zx_status_t GetMetadata(const fbl::RefPtr<zx_device_t>& dev, uint32_t type, void* buf,
                          size_t buflen, size_t* actual) TA_REQ(api_lock_);

  zx_status_t GetMetadataSize(const fbl::RefPtr<zx_device_t>& dev, uint32_t type, size_t* size)
      TA_REQ(api_lock_);

  zx_status_t AddMetadata(const fbl::RefPtr<zx_device_t>& dev, uint32_t type, const void* data,
                          size_t length) TA_REQ(api_lock_);

  zx_status_t PublishMetadata(const fbl::RefPtr<zx_device_t>& dev, const char* path, uint32_t type,
                              const void* data, size_t length) TA_REQ(api_lock_);

  zx_status_t DeviceAddComposite(const fbl::RefPtr<zx_device_t>& dev, const char* name,
                                 const composite_device_desc_t* comp_desc) TA_REQ(api_lock_);

  // Sets up event on async loop which gets triggered once
  zx_status_t SetupEventWaiter();

  // Queues up work item, and signals event to run it.
  void PushWorkItem(const fbl::RefPtr<zx_device_t>& dev, Callback callback);

  // Runs |how_many_to_run| work items. 0 Indicates that caller wishes to run all work items in
  // queue.
  void RunWorkItems(size_t how_many_to_run);

  zx_status_t FindDriver(std::string_view libname, zx::vmo vmo, fbl::RefPtr<zx_driver_t>* out);

  // Called when a zx_device_t has run out of references and needs its destruction finalized.
  void QueueDeviceForFinalization(zx_device_t* device) TA_REQ(api_lock_);

  async::Loop& loop() { return loop_; }

  fs::Vfs* vfs() { return &vfs_; }

  const zx::resource& root_resource() { return root_resource_; }

  ApiLock& api_lock() TA_RET_CAP(api_lock_) { return api_lock_; }

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

  void FinalizeDyingDevices() TA_REQ(api_lock_);

  // enum_lock_{acquire,release}() are used whenever we're iterating
  // on the device tree.  When "enum locked" it is legal to add a new
  // child to the end of a device's list-of-children, but it is not
  // legal to remove a child.  This avoids badness when we have to
  // drop the DM lock to call into device ops while enumerating.
  void enum_lock_acquire() TA_REQ(api_lock_) { enumerators_++; }

  void enum_lock_release() TA_REQ(api_lock_) {
    if (--enumerators_ == 0) {
      FinalizeDyingDevices();
    }
  }

  zx_status_t DeviceValidate(const fbl::RefPtr<zx_device_t>& dev) TA_REQ(api_lock_);

  async::Loop loop_;
  fs::ManagedVfs vfs_;

  // Used to serialize API operations
  ApiLock api_lock_;

  fbl::Mutex lock_;
  // Owned by `loop_`;
  EventWaiter* event_waiter_ TA_GUARDED(lock_) = nullptr;
  fbl::DoublyLinkedList<std::unique_ptr<WorkItem>> work_items_ TA_GUARDED(lock_);

  fbl::DoublyLinkedList<fbl::RefPtr<zx_driver>> drivers_;

  fbl::TaggedDoublyLinkedList<zx_device*, zx_device::DeferListTag> defer_device_list_
      TA_GUARDED(api_lock_);
  int enumerators_ TA_GUARDED(api_lock_) = 0;

  zx::resource root_resource_;
};

#endif  // SRC_DEVICES_BIN_DRIVER_HOST_DRIVER_HOST_CONTEXT_H_
