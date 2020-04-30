// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <threads.h>
#include <unistd.h>
#include <zircon/errors.h>
#include <zircon/listnode.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <array>
#include <atomic>
#include <new>
#include <utility>

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddktl/resume-txn.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>

#include "composite_device.h"
#include "driver_host.h"
#include "log.h"

using llcpp::fuchsia::device::DevicePowerState;

namespace internal {

static thread_local internal::BindContext* g_bind_context;
static thread_local CreationContext* g_creation_context;

// The bind and creation contexts is setup before the bind() or
// create() ops are invoked to provide the ability to sanity check the
// required DeviceAdd() operations these hooks should be making.
void set_bind_context(internal::BindContext* ctx) { g_bind_context = ctx; }

void set_creation_context(CreationContext* ctx) {
  ZX_DEBUG_ASSERT(!ctx || ctx->device_controller_rpc->is_valid() ||
                  ctx->coordinator_rpc->is_valid());
  g_creation_context = ctx;
}

}  // namespace internal

static zx_status_t default_open(void* ctx, zx_device_t** out, uint32_t flags) { return ZX_OK; }

static zx_status_t default_close(void* ctx, uint32_t flags) { return ZX_OK; }

static void default_unbind(void* ctx) {}
static void default_suspend(void* ctx, uint8_t requested_state, bool enable_wake,
                            uint8_t suspend_reason) {}
static void default_resume(void* ctx, uint32_t requested_state) {}
static void default_release(void* ctx) {}

static zx_status_t default_read(void* ctx, void* buf, size_t count, zx_off_t off, size_t* actual) {
  return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t default_write(void* ctx, const void* buf, size_t count, zx_off_t off,
                                 size_t* actual) {
  return ZX_ERR_NOT_SUPPORTED;
}

static zx_off_t default_get_size(void* ctx) { return 0; }

static zx_status_t default_set_performance_state(void* ctx, uint32_t requested_state,
                                                 uint32_t* out_state) {
  return ZX_ERR_NOT_SUPPORTED;
}
static zx_status_t default_rxrpc(void* ctx, zx_handle_t channel) { return ZX_ERR_NOT_SUPPORTED; }

static zx_status_t default_message(void* ctx, fidl_msg_t* msg, fidl_txn_t* txn) {
  fidl_message_header_t* hdr = (fidl_message_header_t*)msg->bytes;
  LOGF(WARNING, "Unsupported FIDL protocol (ordinal %#16lx)", hdr->ordinal);
  zx_handle_close_many(msg->handles, msg->num_handles);
  return ZX_ERR_NOT_SUPPORTED;
}

static void default_child_pre_release(void* ctx, void* child_ctx) {}

const zx_protocol_device_t kDeviceDefaultOps = []() {
  zx_protocol_device_t ops = {};
  ops.open = default_open;
  ops.close = default_close;
  ops.unbind = default_unbind;
  ops.release = default_release;
  ops.read = default_read;
  ops.write = default_write;
  ops.get_size = default_get_size;
  ops.suspend = default_suspend;
  ops.resume = default_resume;
  ops.rxrpc = default_rxrpc;
  ops.message = default_message;
  ops.set_performance_state = default_set_performance_state;
  ops.child_pre_release = default_child_pre_release;
  return ops;
}();

[[noreturn]] static void device_invalid_fatal(void* ctx) {
  LOGF(FATAL, "Device used after destruction");
  __builtin_trap();
}

static zx_protocol_device_t device_invalid_ops = []() {
  zx_protocol_device_t ops = {};
  ops.open = +[](void* ctx, zx_device_t**, uint32_t) -> zx_status_t { device_invalid_fatal(ctx); };
  ops.close = +[](void* ctx, uint32_t) -> zx_status_t { device_invalid_fatal(ctx); };
  ops.unbind = +[](void* ctx) { device_invalid_fatal(ctx); };
  ops.suspend = +[](void* ctx, uint8_t requested_state, bool enable_wake,
                        uint8_t suspend_reason) { device_invalid_fatal(ctx); };
  ops.resume = +[](void* ctx, uint32_t) { device_invalid_fatal(ctx); };
  ops.release = +[](void* ctx) { device_invalid_fatal(ctx); };
  ops.read =
      +[](void* ctx, void*, size_t, size_t, size_t*) -> zx_status_t { device_invalid_fatal(ctx); };
  ops.write = +[](void* ctx, const void*, size_t, size_t, size_t*) -> zx_status_t {
    device_invalid_fatal(ctx);
  };
  ops.get_size = +[](void* ctx) -> zx_off_t { device_invalid_fatal(ctx); };
  ops.rxrpc = +[](void* ctx, zx_handle_t) -> zx_status_t { device_invalid_fatal(ctx); };
  ops.message =
      +[](void* ctx, fidl_msg_t*, fidl_txn_t*) -> zx_status_t { device_invalid_fatal(ctx); };
  ops.set_performance_state =
      +[](void* ctx, uint32_t requested_state, uint32_t* out_state) -> zx_status_t {
    device_invalid_fatal(ctx);
  };
  return ops;
}();

// Maximum number of dead devices to hold on the dead device list
// before we start free'ing the oldest when adding a new one.
#define DEAD_DEVICE_MAX 7

void DriverHostContext::DeviceDestroy(zx_device_t* dev) {
  // Wrap the deferred-deletion list in a struct, so we can give it a proper
  // dtor.  Otherwise, this causes the binary to crash on exit due to an
  // is_empty assert in fbl::DoublyLinkedList.  This was particularly a
  // problem for unit tests.
  struct DeadList {
    ~DeadList() {
      while (!devices.is_empty()) {
        delete devices.pop_front();
      }
    }
    fbl::DoublyLinkedList<zx_device*, zx_device::Node> devices;
  };

  static DeadList dead_list;
  static unsigned dead_count = 0;

  // ensure any ops will be fatal
  dev->ops = &device_invalid_ops;

  dev->magic = 0xdeaddeaddeaddead;

  // ensure all owned handles are invalid
  dev->event.reset();
  dev->local_event.reset();

  // ensure all pointers are invalid
  dev->ctx = nullptr;
  dev->driver = nullptr;
  dev->parent.reset();
  dev->conn.store(nullptr);
  {
    fbl::AutoLock guard(&dev->proxy_ios_lock);
    dev->proxy_ios = nullptr;
  }

  // Defer destruction to help catch use-after-free and also
  // so the compiler can't (easily) optimize away the poisoning
  // we do above.
  dead_list.devices.push_back(dev);

  if (dead_count == DEAD_DEVICE_MAX) {
    zx_device_t* to_delete = dead_list.devices.pop_front();
    delete to_delete;
  } else {
    dead_count++;
  }
}

void DriverHostContext::FinalizeDyingDevices() {
  // Early exit if there's no work
  if (defer_device_list_.is_empty()) {
    return;
  }

  // Otherwise we snapshot the list
  auto list = std::move(defer_device_list_);

  // We detach all the devices from their parents list-of-children
  // while under the DM lock to avoid an enumerator starting to mutate
  // things before we're done detaching them.
  for (auto& dev : list) {
    if (dev.parent) {
      dev.parent->children.erase(dev);
    }
  }

  // Then we can get to the actual final teardown where we have
  // to drop the lock to call the callback
  zx_device* dev;
  while ((dev = list.pop_front()) != nullptr) {
    // invoke release op
    if (dev->flags & DEV_FLAG_ADDED) {
      if (dev->parent) {
        api_lock_.Release();
        dev->parent->ChildPreReleaseOp(dev->ctx);
        api_lock_.Acquire();
      }
      api_lock_.Release();
      dev->ReleaseOp();
      api_lock_.Acquire();
    }

    if (dev->parent) {
      // When all the children are gone, complete the pending unbind request.
      if ((!(dev->parent->flags & DEV_FLAG_DEAD)) && dev->parent->children.is_empty()) {
        if (auto unbind_children = dev->parent->take_unbind_children_conn(); unbind_children) {
          unbind_children(ZX_OK);
        }
      }
      // If the parent wants rebinding when its children are gone,
      // And the parent is not dead, And this was the last child...
      if ((dev->parent->flags & DEV_FLAG_WANTS_REBIND) && (!(dev->parent->flags & DEV_FLAG_DEAD)) &&
          dev->parent->children.is_empty()) {
        // Clear the wants rebind flag and request the rebind
        dev->parent->flags &= (~DEV_FLAG_WANTS_REBIND);
        std::string drv = dev->parent->get_rebind_drv_name().value_or("");
        zx_status_t status = DeviceBind(dev->parent, drv.c_str());
        if (status != ZX_OK) {
          if (auto rebind = dev->parent->take_rebind_conn(); rebind) {
            rebind(status);
          }
        }
      }

      dev->parent.reset();
    }

    // destroy/deallocate the device
    DeviceDestroy(dev);
  }
}

zx_status_t DriverHostContext::DeviceValidate(const fbl::RefPtr<zx_device_t>& dev) {
  if (dev == nullptr) {
    LOGF(ERROR, "Invalid device");
    return ZX_ERR_INVALID_ARGS;
  }
  if (dev->flags & DEV_FLAG_ADDED) {
    LOGD(ERROR, dev, "Already added device %p", dev.get());
    return ZX_ERR_BAD_STATE;
  }
  if (dev->magic != DEV_MAGIC) {
    LOGD(ERROR, dev, "Invalid signature for device %p: %#lx", dev.get(), dev->magic);
    return ZX_ERR_BAD_STATE;
  }
  if (dev->ops == nullptr) {
    LOGD(ERROR, dev, "Invalid ops for device %p", dev.get());
    return ZX_ERR_INVALID_ARGS;
  }
  if ((dev->protocol_id == ZX_PROTOCOL_MISC_PARENT) || (dev->protocol_id == ZX_PROTOCOL_ROOT)) {
    LOGD(ERROR, dev, "Invalid protocol for device %p: %#x", dev.get(), dev->protocol_id);
    // These protocols is only allowed for the special
    // singleton misc or root parent devices.
    return ZX_ERR_INVALID_ARGS;
  }
  // devices which do not declare a primary protocol
  // are implied to be misc devices
  if (dev->protocol_id == 0) {
    dev->protocol_id = ZX_PROTOCOL_MISC;
  }

  return ZX_OK;
}

namespace internal {

namespace {

#define REMOVAL_BAD_FLAGS (DEV_FLAG_DEAD | DEV_FLAG_BUSY | DEV_FLAG_INSTANCE | DEV_FLAG_MULTI_BIND)

const char* removal_problem(uint32_t flags) {
  if (flags & DEV_FLAG_DEAD) {
    return "already dead";
  }
  if (flags & DEV_FLAG_BUSY) {
    return "being created";
  }
  if (flags & DEV_FLAG_INSTANCE) {
    return "ephemeral device";
  }
  if (flags & DEV_FLAG_MULTI_BIND) {
    return "multi-bind-able device";
  }
  return "?";
}

uint8_t device_get_suspend_reason(fuchsia_device_manager_SystemPowerState power_state) {
  switch (power_state) {
    case fuchsia_device_manager_SystemPowerState_SYSTEM_POWER_STATE_REBOOT:
      return DEVICE_SUSPEND_REASON_REBOOT;
    case fuchsia_device_manager_SystemPowerState_SYSTEM_POWER_STATE_REBOOT_RECOVERY:
      return DEVICE_SUSPEND_REASON_REBOOT_RECOVERY;
    case fuchsia_device_manager_SystemPowerState_SYSTEM_POWER_STATE_REBOOT_BOOTLOADER:
      return DEVICE_SUSPEND_REASON_REBOOT_BOOTLOADER;
    case fuchsia_device_manager_SystemPowerState_SYSTEM_POWER_STATE_MEXEC:
      return DEVICE_SUSPEND_REASON_MEXEC;
    case fuchsia_device_manager_SystemPowerState_SYSTEM_POWER_STATE_POWEROFF:
      return DEVICE_SUSPEND_REASON_POWEROFF;
    case fuchsia_device_manager_SystemPowerState_SYSTEM_POWER_STATE_SUSPEND_RAM:
      return DEVICE_SUSPEND_REASON_SUSPEND_RAM;
    default:
      return DEVICE_SUSPEND_REASON_SELECTIVE_SUSPEND;
  }
}

zx_status_t device_get_dev_power_state_from_mapping(
    const fbl::RefPtr<zx_device>& dev, uint32_t flags,
    llcpp::fuchsia::device::SystemPowerStateInfo* info, uint8_t* suspend_reason) {
  // TODO(ravoorir) : When the usage of suspend flags is replaced with
  // system power states, this function will not need the switch case.
  // Some suspend flags might be translated to system power states with
  // additional hints (ex: REBOOT/REBOOT_BOOTLOADER/REBOOT_RECOVERY/MEXEC).
  // For now, each of these flags are treated as an individual state.
  fuchsia_device_manager_SystemPowerState sys_state;
  switch (flags) {
    case DEVICE_SUSPEND_FLAG_REBOOT:
      sys_state = fuchsia_device_manager_SystemPowerState_SYSTEM_POWER_STATE_REBOOT;
      break;
    case DEVICE_SUSPEND_FLAG_REBOOT_RECOVERY:
      sys_state = fuchsia_device_manager_SystemPowerState_SYSTEM_POWER_STATE_REBOOT_RECOVERY;
      break;
    case DEVICE_SUSPEND_FLAG_REBOOT_BOOTLOADER:
      sys_state = fuchsia_device_manager_SystemPowerState_SYSTEM_POWER_STATE_REBOOT_BOOTLOADER;
      break;
    case DEVICE_SUSPEND_FLAG_MEXEC:
      sys_state = fuchsia_device_manager_SystemPowerState_SYSTEM_POWER_STATE_MEXEC;
      break;
    case DEVICE_SUSPEND_FLAG_POWEROFF:
      sys_state = fuchsia_device_manager_SystemPowerState_SYSTEM_POWER_STATE_POWEROFF;
      break;
    default:
      return ZX_ERR_INVALID_ARGS;
  }
  auto& sys_power_states = dev->GetSystemPowerStateMapping();
  *info = sys_power_states[sys_state];
  *suspend_reason = internal::device_get_suspend_reason(sys_state);
  return ZX_OK;
}

}  // namespace

uint32_t get_perf_state(const fbl::RefPtr<zx_device>& dev, uint32_t requested_perf_state) {
  // Give preference to the performance state that is explicitly for this device.
  if (dev->current_performance_state() != ::llcpp::fuchsia::device::DEVICE_PERFORMANCE_STATE_P0) {
    return dev->current_performance_state();
  }
  return requested_perf_state;
}

}  // namespace internal

zx_status_t DriverHostContext::DeviceCreate(zx_driver_t* drv, const char* name, void* ctx,
                                            const zx_protocol_device_t* ops,
                                            fbl::RefPtr<zx_device_t>* out) {
  if (!drv) {
    LOGF(ERROR, "Cannot find driver");
    return ZX_ERR_INVALID_ARGS;
  }

  fbl::RefPtr<zx_device> dev;
  zx_status_t status = zx_device::Create(this, &dev);
  if (status != ZX_OK) {
    return status;
  }

  dev->ops = ops;
  dev->driver = drv;

  if (name == nullptr) {
    LOGF(WARNING, "Invalid name for device %p", dev.get());
    name = "invalid";
    dev->magic = 0;
  }

  size_t len = strlen(name);
  // TODO(teisenbe): I think this is overly aggresive, and could be changed
  // to |len > ZX_DEVICE_NAME_MAX| and |len = ZX_DEVICE_NAME_MAX|.
  if (len >= ZX_DEVICE_NAME_MAX) {
    LOGF(WARNING, "Name too large for device %p: %s", dev.get(), name);
    len = ZX_DEVICE_NAME_MAX - 1;
    dev->magic = 0;
  }

  memcpy(dev->name, name, len);
  dev->name[len] = 0;
  // TODO(teisenbe): Why do we default to dev.get() here?  Why not just
  // nullptr
  dev->ctx = ctx ? ctx : dev.get();
  *out = std::move(dev);
  return ZX_OK;
}

zx_status_t DriverHostContext::DeviceAdd(const fbl::RefPtr<zx_device_t>& dev,
                                         const fbl::RefPtr<zx_device_t>& parent,
                                         const zx_device_prop_t* props, uint32_t prop_count,
                                         const char* proxy_args, zx::channel client_remote) {
  auto mark_dead = fbl::MakeAutoCall([&dev]() {
    if (dev) {
      dev->flags |= DEV_FLAG_DEAD;
    }
  });

  zx_status_t status;
  if ((status = DeviceValidate(dev)) < 0) {
    return status;
  }
  if (parent == nullptr) {
    LOGD(ERROR, dev, "Cannot add device %p to invalid parent", dev.get());
    return ZX_ERR_NOT_SUPPORTED;
  }
  if (parent->flags & DEV_FLAG_DEAD) {
    LOGD(ERROR, dev, "Cannot add device %p to dead parent %p", dev.get(), parent.get());
    return ZX_ERR_BAD_STATE;
  }

  internal::BindContext* bind_ctx = nullptr;
  internal::CreationContext* creation_ctx = nullptr;

  // If the bind or creation ctx (thread locals) are set, we are in
  // a thread that is handling a bind() or create() callback and if
  // that ctx's parent matches the one provided to add we need to do
  // some additional checking...
  if ((internal::g_bind_context != nullptr) && (internal::g_bind_context->parent == parent)) {
    bind_ctx = internal::g_bind_context;
  }
  if ((internal::g_creation_context != nullptr) &&
      (internal::g_creation_context->parent == parent)) {
    creation_ctx = internal::g_creation_context;
    // create() must create only one child
    if (creation_ctx->child != nullptr) {
      LOGD(ERROR, dev, "Driver attempted to create multiple proxy devices");
      return ZX_ERR_BAD_STATE;
    }
  }
  VLOGD(1, dev, "Adding device %p (parent %p)", dev.get(), parent.get());

  // Don't create an event handle if we alredy have one
  if (!dev->event.is_valid() &&
      ((status = zx::eventpair::create(0, &dev->event, &dev->local_event)) < 0)) {
    return status;
  }

  dev->flags |= DEV_FLAG_BUSY;

  // proxy devices are created through this handshake process
  if (creation_ctx) {
    if (dev->flags & DEV_FLAG_INVISIBLE) {
      LOGD(ERROR, dev, "Driver attempted to create invisible device in create()");
      return ZX_ERR_INVALID_ARGS;
    }
    dev->flags |= DEV_FLAG_ADDED;
    dev->flags &= (~DEV_FLAG_BUSY);
    dev->rpc = zx::unowned_channel(creation_ctx->device_controller_rpc);
    dev->coordinator_rpc = zx::unowned_channel(creation_ctx->coordinator_rpc);
    creation_ctx->child = dev;
    mark_dead.cancel();
    return ZX_OK;
  }

  dev->parent = parent;

  // attach to our parent
  parent->children.push_back(dev.get());

  if (!(dev->flags & DEV_FLAG_INSTANCE)) {
    // Add always consumes the handle
    status = DriverManagerAdd(parent, dev, proxy_args, props, prop_count, std::move(client_remote));
    if (status < 0) {
      LOGD(ERROR, dev, "Failed to add device %p to driver_manager: %s", dev.get(),
           zx_status_get_string(status));
      dev->parent->children.erase(*dev);
      dev->parent.reset();

      // since we are under the lock the whole time, we added the node
      // to the tail and then we peeled it back off the tail when we
      // failed, we don't need to interact with the enum lock mechanism
      dev->flags &= (~DEV_FLAG_BUSY);
      return status;
    }
  }
  dev->flags |= DEV_FLAG_ADDED;
  dev->flags &= (~DEV_FLAG_BUSY);

  // record this device in the bind context if there is one
  if (bind_ctx && (bind_ctx->child == nullptr)) {
    bind_ctx->child = dev;
  }
  mark_dead.cancel();
  return ZX_OK;
}

zx_status_t DriverHostContext::DeviceInit(const fbl::RefPtr<zx_device_t>& dev) {
  if (dev->flags & DEV_FLAG_INITIALIZING) {
    return ZX_ERR_BAD_STATE;
  }
  // Call dev's init op.
  if (dev->ops->init) {
    dev->flags |= DEV_FLAG_INITIALIZING;
    api_lock_.Release();
    dev->InitOp();
    api_lock_.Acquire();
  } else {
    dev->init_cb(ZX_OK);
  }
  return ZX_OK;
}

void DriverHostContext::DeviceInitReply(const fbl::RefPtr<zx_device_t>& dev, zx_status_t status,
                                        const device_init_reply_args_t* args) {
  if (!(dev->flags & DEV_FLAG_INITIALIZING)) {
    LOGD(FATAL, dev, "Device %p cannot reply to init (flags %#x)", dev.get(), dev->flags);
  }
  if (status == ZX_OK) {
    if (args && args->power_states && args->power_state_count != 0) {
      dev->SetPowerStates(args->power_states, args->power_state_count);
    }
    if (args && args->performance_states && (args->performance_state_count != 0)) {
      dev->SetPerformanceStates(args->performance_states, args->performance_state_count);
    }
  }

  if (dev->init_cb == nullptr) {
    LOGD(FATAL, dev, "Device %p cannot reply to init, no callback set (flags %#x)", dev.get(),
         dev->flags);
  }

  dev->init_cb(status);
  // Device is no longer invisible.
  dev->flags &= ~(DEV_FLAG_INVISIBLE);
  // If all children completed intializing,
  // complete pending bind and rebind connections.
  bool complete_bind_rebind = true;
  for (auto& child : dev->parent->children) {
    if (child.flags & DEV_FLAG_INVISIBLE) {
      complete_bind_rebind = false;
    }
  }
  if (complete_bind_rebind) {
    if (auto bind_conn = dev->parent->take_bind_conn(); bind_conn) {
      bind_conn(status);
    }
    if (auto rebind_conn = dev->parent->take_rebind_conn(); rebind_conn) {
      rebind_conn(status);
    }
  }
}

zx_status_t DriverHostContext::DeviceRemoveDeprecated(const fbl::RefPtr<zx_device_t>& dev) {
  // This removal is in response to the unbind hook.
  if (dev->flags & DEV_FLAG_UNBOUND) {
    DeviceUnbindReply(dev);
    return ZX_OK;
  }
  return DeviceRemove(dev, false /* unbind_self */);
}

zx_status_t DriverHostContext::DeviceRemove(const fbl::RefPtr<zx_device_t>& dev, bool unbind_self) {
  if (dev->flags & REMOVAL_BAD_FLAGS) {
    LOGD(ERROR, dev, "Cannot remove device %p: %s", dev.get(),
         internal::removal_problem(dev->flags));
    return ZX_ERR_INVALID_ARGS;
  }
  if (dev->flags & DEV_FLAG_INVISIBLE) {
    // We failed during init and the device is being removed. Complete the pending
    // bind/rebind conn of parent if any.
    if (auto bind_conn = dev->parent->take_bind_conn(); bind_conn) {
      bind_conn(ZX_ERR_IO);
    }
    if (auto rebind_conn = dev->parent->take_rebind_conn(); rebind_conn) {
      rebind_conn(ZX_ERR_IO);
    }
  }
  VLOGD(1, dev, "Device %p is being scheduled for removal", dev.get());
  // Ask the devcoordinator to schedule the removal of this device and its children.
  ScheduleRemove(dev, unbind_self);
  return ZX_OK;
}

zx_status_t DriverHostContext::DeviceCompleteRemoval(const fbl::RefPtr<zx_device_t>& dev) {
  VLOGD(1, dev, "Device %p is being removed (removal requested)", dev.get());

  // This recovers the leaked reference that happened in device_add_from_driver().
  auto dev_add_ref = fbl::ImportFromRawPtr(dev.get());
  DriverManagerRemove(std::move(dev_add_ref));

  dev->flags |= DEV_FLAG_DEAD;
  return ZX_OK;
}

zx_status_t DriverHostContext::DeviceUnbind(const fbl::RefPtr<zx_device_t>& dev) {
  enum_lock_acquire();

  if (!(dev->flags & DEV_FLAG_UNBOUND)) {
    dev->flags |= DEV_FLAG_UNBOUND;
    // Call dev's unbind op.
    if (dev->ops->unbind) {
      VLOGD(1, dev, "Device %p is being unbound", dev.get());
      api_lock_.Release();
      dev->UnbindOp();
      api_lock_.Acquire();
    } else {
      // We should reply to the unbind hook so we don't get stuck.
      dev->unbind_cb(ZX_OK);
    }
  }
  enum_lock_release();

  return ZX_OK;
}

void DriverHostContext::DeviceUnbindReply(const fbl::RefPtr<zx_device_t>& dev) {
  if (dev->flags & REMOVAL_BAD_FLAGS) {
    LOGD(FATAL, dev, "Device %p cannot reply to unbind, bad flags: %s", dev.get(),
         internal::removal_problem(dev->flags));
  }
  if (!(dev->flags & DEV_FLAG_UNBOUND)) {
    LOGD(FATAL, dev, "Device %p cannot reply to unbind, not in unbinding state (flags %#x)",
         dev.get(), dev->flags);
  }
  if (dev->vnode->inflight_transactions() > 0) {
    LOGD(FATAL, dev, "Device %p cannot reply to unbind, has %zu outstanding transactions",
         dev.get(), dev->vnode->inflight_transactions());
  }

  VLOGD(1, dev, "Device %p unbind completed", dev.get());
  if (dev->unbind_cb) {
    dev->CloseAllConnections();
    dev->unbind_cb(ZX_OK);
  } else {
    LOGD(FATAL, dev, "Device %p cannot reply to unbind, no callback set (flags %#x)", dev.get(),
         dev->flags);
  }
}

void DriverHostContext::DeviceSuspendReply(const fbl::RefPtr<zx_device_t>& dev, zx_status_t status,
                                           uint8_t out_state) {
  // There are 3 references when this function gets called in repsonse to
  // selective suspend on a device. 1. When we create a connection in ReadMessage
  // 2. When we wrap the txn in Transaction.
  // 3. When we make the suspend txn asynchronous using ToAsync()
  if (dev->vnode->inflight_transactions() > 3) {
    LOGD(FATAL, dev, "Device %p cannot reply to suspend, has %zu outstanding transactions",
         dev.get(), dev->vnode->inflight_transactions());
  }

  if (dev->suspend_cb) {
    dev->suspend_cb(status, out_state);
  } else {
    LOGD(FATAL, dev, "Device %p cannot reply to suspend, no callback set", dev.get());
  }
}

void DriverHostContext::DeviceResumeReply(const fbl::RefPtr<zx_device_t>& dev, zx_status_t status,
                                          uint8_t out_power_state, uint32_t out_perf_state) {
  if (dev->resume_cb) {
    if (out_power_state == static_cast<uint8_t>(DevicePowerState::DEVICE_POWER_STATE_D0)) {
      // Update the current performance state.
      dev->set_current_performance_state(out_perf_state);
    }
    dev->resume_cb(status, out_power_state, out_perf_state);
  } else {
    LOGD(FATAL, dev, "Device %p cannot reply to resume, no callback set", dev.get());
  }
}

zx_status_t DriverHostContext::DeviceRebind(const fbl::RefPtr<zx_device_t>& dev) {
  if (!dev->children.is_empty() || dev->has_composite()) {
    // note that we want to be rebound when our children are all gone
    dev->flags |= DEV_FLAG_WANTS_REBIND;
    // request that any existing children go away
    ScheduleUnbindChildren(dev);
  } else {
    std::string drv = dev->get_rebind_drv_name().value_or("");
    return DeviceBind(dev, drv.c_str());
  }
  return ZX_OK;
}

zx_status_t DriverHostContext::DeviceOpen(const fbl::RefPtr<zx_device_t>& dev,
                                          fbl::RefPtr<zx_device_t>* out, uint32_t flags) {
  if (dev->flags & DEV_FLAG_DEAD) {
    LOGD(ERROR, dev, "Cannot open device %p, device is dead", dev.get());
    return ZX_ERR_BAD_STATE;
  }
  fbl::RefPtr<zx_device_t> new_ref(dev);
  zx_status_t r;
  zx_device_t* opened_dev = nullptr;
  {
    api_lock_.Release();
    r = dev->OpenOp(&opened_dev, flags);
    api_lock_.Acquire();
  }
  if (r < 0) {
    new_ref.reset();
  } else if (opened_dev != nullptr) {
    // open created a per-instance device for us
    new_ref.reset();
    // Claim the reference from open
    new_ref = fbl::ImportFromRawPtr(opened_dev);

    if (!(opened_dev->flags & DEV_FLAG_INSTANCE)) {
      LOGD(FATAL, new_ref, "Cannot open device %p, bad state %#x", opened_dev, flags);
    }
  }
  *out = std::move(new_ref);
  return r;
}

zx_status_t DriverHostContext::DeviceClose(fbl::RefPtr<zx_device_t> dev, uint32_t flags) {
  api_lock_.Release();
  zx_status_t status = dev->CloseOp(flags);
  api_lock_.Acquire();
  return status;
}

void DriverHostContext::DeviceSystemSuspend(const fbl::RefPtr<zx_device>& dev, uint32_t flags) {
  if (dev->auto_suspend_configured()) {
    dev->ops->configure_auto_suspend(dev->ctx, false,
                                     fuchsia_device_DevicePowerState_DEVICE_POWER_STATE_D0);
    LOGF(INFO, "System suspend overriding auto suspend for device %p '%s'", dev.get(), dev->name);
  }
  zx_status_t status = ZX_ERR_NOT_SUPPORTED;
  // If new suspend hook is implemented, prefer that.
  if (dev->ops->suspend) {
    llcpp::fuchsia::device::SystemPowerStateInfo new_state_info;
    uint8_t suspend_reason = DEVICE_SUSPEND_REASON_SELECTIVE_SUSPEND;

    status = internal::device_get_dev_power_state_from_mapping(dev, flags, &new_state_info,
                                                               &suspend_reason);
    if (status == ZX_OK) {
      enum_lock_acquire();
      {
        api_lock_.Release();
        dev->ops->suspend(dev->ctx, static_cast<uint8_t>(new_state_info.dev_state),
                              new_state_info.wakeup_enable, suspend_reason);
        api_lock_.Acquire();
      }
      enum_lock_release();
      return;
    }
  }

  // If suspend hook is not implemented, do not throw error during system suspend.
  if (status == ZX_ERR_NOT_SUPPORTED) {
    status = ZX_OK;
  }

  dev->suspend_cb(status, 0);
}

void DriverHostContext::DeviceSystemResume(const fbl::RefPtr<zx_device>& dev,
                                           uint32_t target_system_state) {
  if (dev->auto_suspend_configured()) {
    dev->ops->configure_auto_suspend(dev->ctx, false,
                                     fuchsia_device_DevicePowerState_DEVICE_POWER_STATE_D0);
    LOGF(INFO, "System resume overriding auto suspend for device %p '%s'", dev.get(), dev->name);
  }

  zx_status_t status = ZX_ERR_NOT_SUPPORTED;
  // If new resume hook is implemented, prefer that.
  if (dev->ops->resume) {
    enum_lock_acquire();
    {
      api_lock_.Release();
      auto& sys_power_states = dev->GetSystemPowerStateMapping();
      uint32_t requested_perf_state =
          internal::get_perf_state(dev, sys_power_states[target_system_state].performance_state);
      dev->ops->resume(dev->ctx, requested_perf_state);
      api_lock_.Acquire();
    }
    enum_lock_release();
    return;
  }

  // default_resume() returns ZX_ERR_NOT_SUPPORTED
  if (status == ZX_ERR_NOT_SUPPORTED) {
    status = ZX_OK;
  }
  dev->resume_cb(status, static_cast<uint8_t>(DevicePowerState::DEVICE_POWER_STATE_D0),
                 llcpp::fuchsia::device::DEVICE_PERFORMANCE_STATE_P0);
}

void DriverHostContext::DeviceSuspendNew(const fbl::RefPtr<zx_device>& dev,
                                         DevicePowerState requested_state) {
  if (dev->auto_suspend_configured()) {
    LOGF(INFO, "Failed to suspend device %p '%s', auto suspend is enabled", dev.get(), dev->name);
    dev->suspend_cb(ZX_ERR_NOT_SUPPORTED,
                    static_cast<uint8_t>(DevicePowerState::DEVICE_POWER_STATE_D0));
    return;
  }
  if (!(dev->IsPowerStateSupported(requested_state))) {
    dev->suspend_cb(ZX_ERR_INVALID_ARGS,
                    static_cast<uint8_t>(DevicePowerState::DEVICE_POWER_STATE_D0));
    return;
  }

  if (dev->ops->suspend) {
    dev->ops->suspend(dev->ctx, static_cast<uint8_t>(requested_state),
                          DEVICE_SUSPEND_REASON_SELECTIVE_SUSPEND, false /* wake_configured */);
    return;
  }
  dev->suspend_cb(ZX_ERR_NOT_SUPPORTED,
                  static_cast<uint8_t>(DevicePowerState::DEVICE_POWER_STATE_D0));
}

zx_status_t DriverHostContext::DeviceSetPerformanceState(const fbl::RefPtr<zx_device>& dev,
                                                         uint32_t requested_state,
                                                         uint32_t* out_state) {
  if (!(dev->IsPerformanceStateSupported(requested_state))) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (dev->ops->set_performance_state) {
    zx_status_t status = dev->ops->set_performance_state(dev->ctx, requested_state, out_state);
    if (!(dev->IsPerformanceStateSupported(*out_state))) {
      LOGD(FATAL, dev,
           "Device %p 'set_performance_state' hook returned an unsupported performance state",
           dev.get());
    }
    dev->set_current_performance_state(*out_state);
    return status;
  }
  return ZX_ERR_NOT_SUPPORTED;
}

void DriverHostContext::DeviceResumeNew(const fbl::RefPtr<zx_device>& dev) {
  if (dev->auto_suspend_configured()) {
    LOGF(INFO, "Failed to resume device %p '%s', auto suspend is enabled", dev.get(), dev->name);
    dev->resume_cb(ZX_ERR_NOT_SUPPORTED,
                   static_cast<uint8_t>(DevicePowerState::DEVICE_POWER_STATE_D0),
                   llcpp::fuchsia::device::DEVICE_PERFORMANCE_STATE_P0);
    return;
  }
  // If new resume hook is implemented, prefer that.
  if (dev->ops->resume) {
    uint32_t requested_perf_state =
        internal::get_perf_state(dev, llcpp::fuchsia::device::DEVICE_PERFORMANCE_STATE_P0);
    dev->ops->resume(dev->ctx, requested_perf_state);
    return;
  }
  dev->resume_cb(ZX_ERR_NOT_SUPPORTED,
                 static_cast<uint8_t>(DevicePowerState::DEVICE_POWER_STATE_D0),
                 llcpp::fuchsia::device::DEVICE_PERFORMANCE_STATE_P0);
}

zx_status_t DriverHostContext::DeviceConfigureAutoSuspend(const fbl::RefPtr<zx_device>& dev,
                                                          bool enable,
                                                          DevicePowerState requested_state) {
  if (enable && !(dev->IsPowerStateSupported(requested_state))) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (dev->ops->configure_auto_suspend) {
    zx_status_t status =
        dev->ops->configure_auto_suspend(dev->ctx, enable, static_cast<uint8_t>(requested_state));
    if (status != ZX_OK) {
      return status;
    }
    dev->set_auto_suspend_configured(enable);
    return ZX_OK;
  }
  return ZX_ERR_NOT_SUPPORTED;
}

void DriverHostContext::QueueDeviceForFinalization(zx_device_t* device) {
  // Put on the defered work list for finalization
  defer_device_list_.push_back(device);

  // Immediately finalize if there's not an active enumerator
  if (enumerators_ == 0) {
    FinalizeDyingDevices();
  }
}
