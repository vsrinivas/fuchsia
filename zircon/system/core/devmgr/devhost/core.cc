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
#include <zircon/assert.h>
#include <zircon/listnode.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <array>
#include <atomic>
#include <new>
#include <utility>

#include <ddk/device.h>
#include <ddk/driver.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>

#include "composite-device.h"
#include "devhost.h"

namespace devmgr {

#define TRACE 0

#if TRACE
#define xprintf(fmt...) printf(fmt)
#else
#define xprintf(fmt...) \
  do {                  \
  } while (0)
#endif

#define TRACE_ADD_REMOVE 0

namespace internal {
__LOCAL mtx_t devhost_api_lock = MTX_INIT;
__LOCAL std::atomic<thrd_t> devhost_api_lock_owner(0);
}  // namespace internal

static thread_local BindContext* g_bind_context;
static thread_local CreationContext* g_creation_context;

// The bind and creation contexts is setup before the bind() or
// create() ops are invoked to provide the ability to sanity check the
// required device_add() operations these hooks should be making.
void devhost_set_bind_context(BindContext* ctx) { g_bind_context = ctx; }

void devhost_set_creation_context(CreationContext* ctx) {
  ZX_DEBUG_ASSERT(!ctx || ctx->rpc->is_valid());
  g_creation_context = ctx;
}

static zx_status_t default_open(void* ctx, zx_device_t** out, uint32_t flags) { return ZX_OK; }

static zx_status_t default_close(void* ctx, uint32_t flags) { return ZX_OK; }

static void default_unbind(void* ctx) {}

static void default_release(void* ctx) {}

static zx_status_t default_read(void* ctx, void* buf, size_t count, zx_off_t off, size_t* actual) {
  return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t default_write(void* ctx, const void* buf, size_t count, zx_off_t off,
                                 size_t* actual) {
  return ZX_ERR_NOT_SUPPORTED;
}

static zx_off_t default_get_size(void* ctx) { return 0; }

static zx_status_t default_suspend(void* ctx, uint32_t flags) { return ZX_ERR_NOT_SUPPORTED; }

static zx_status_t default_resume(void* ctx, uint32_t target_system_state) {
  return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t default_rxrpc(void* ctx, zx_handle_t channel) { return ZX_ERR_NOT_SUPPORTED; }

static zx_status_t default_message(void* ctx, fidl_msg_t* msg, fidl_txn_t* txn) {
  fidl_message_header_t* hdr = (fidl_message_header_t*)msg->bytes;
  printf("devhost: Unsupported FIDL operation: 0x%lx\n", hdr->ordinal);
  zx_handle_close_many(msg->handles, msg->num_handles);
  return ZX_ERR_NOT_SUPPORTED;
}

zx_protocol_device_t device_default_ops = []() {
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
  return ops;
}();

[[noreturn]] static void device_invalid_fatal(void* ctx) {
  printf("devhost: FATAL: zx_device_t used after destruction.\n");
  __builtin_trap();
}

static zx_protocol_device_t device_invalid_ops = []() {
  zx_protocol_device_t ops = {};
  ops.open = +[](void* ctx, zx_device_t**, uint32_t) -> zx_status_t { device_invalid_fatal(ctx); };
  ops.close = +[](void* ctx, uint32_t) -> zx_status_t { device_invalid_fatal(ctx); };
  ops.unbind = +[](void* ctx) { device_invalid_fatal(ctx); };
  ops.release = +[](void* ctx) { device_invalid_fatal(ctx); };
  ops.read =
      +[](void* ctx, void*, size_t, size_t, size_t*) -> zx_status_t { device_invalid_fatal(ctx); };
  ops.write = +[](void* ctx, const void*, size_t, size_t, size_t*) -> zx_status_t {
    device_invalid_fatal(ctx);
  };
  ops.get_size = +[](void* ctx) -> zx_off_t { device_invalid_fatal(ctx); };
  ops.suspend = +[](void* ctx, uint32_t) -> zx_status_t { device_invalid_fatal(ctx); };
  ops.resume = +[](void* ctx, uint32_t) -> zx_status_t { device_invalid_fatal(ctx); };
  ops.rxrpc = +[](void* ctx, zx_handle_t) -> zx_status_t { device_invalid_fatal(ctx); };
  ops.message =
      +[](void* ctx, fidl_msg_t*, fidl_txn_t*) -> zx_status_t { device_invalid_fatal(ctx); };
  return ops;
}();

// Maximum number of dead devices to hold on the dead device list
// before we start free'ing the oldest when adding a new one.
#define DEAD_DEVICE_MAX 7

void devhost_device_destroy(zx_device_t* dev) REQ_DM_LOCK {
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

// defered work list
fbl::DoublyLinkedList<zx_device*, zx_device::DeferNode> defer_device_list;
int devhost_enumerators = 0;

void devhost_finalize() {
  // Early exit if there's no work
  if (defer_device_list.is_empty()) {
    return;
  }

  // Otherwise we snapshot the list
  auto list = std::move(defer_device_list);

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
      ApiAutoRelock relock;
      dev->ReleaseOp();
    }

    if (dev->parent) {
      // If the parent wants rebinding when its children are gone,
      // And the parent is not dead, And this was the last child...
      if ((dev->parent->flags & DEV_FLAG_WANTS_REBIND) && (!(dev->parent->flags & DEV_FLAG_DEAD)) &&
          dev->parent->children.is_empty()) {
        // Clear the wants rebind flag and request the rebind
        dev->parent->flags &= (~DEV_FLAG_WANTS_REBIND);
        std::string drv = dev->parent->get_rebind_drv_name().value_or("");
        zx_status_t status = devhost_device_bind(dev->parent, drv.c_str());
        if (status != ZX_OK) {
          fs::FidlConnection conn(fidl_txn_t{}, ZX_HANDLE_INVALID, 0);
          if (dev->take_rebind_conn_and_clear(&conn)) {
            fuchsia_device_ControllerRebind_reply(conn.Txn(), status);
          }
        }
      }

      dev->parent.reset();
    }

    // destroy/deallocate the device
    devhost_device_destroy(dev);
  }
}

// enum_lock_{acquire,release}() are used whenever we're iterating
// on the device tree.  When "enum locked" it is legal to add a new
// child to the end of a device's list-of-children, but it is not
// legal to remove a child.  This avoids badness when we have to
// drop the DM lock to call into device ops while enumerating.

static void enum_lock_acquire() REQ_DM_LOCK { devhost_enumerators++; }

static void enum_lock_release() REQ_DM_LOCK {
  if (--devhost_enumerators == 0) {
    devhost_finalize();
  }
}

zx_status_t devhost_device_create(zx_driver_t* drv, const char* name, void* ctx,
                                  const zx_protocol_device_t* ops,
                                  fbl::RefPtr<zx_device_t>* out) REQ_DM_LOCK {
  if (!drv) {
    printf("devhost: device_add could not find driver!\n");
    return ZX_ERR_INVALID_ARGS;
  }

  fbl::RefPtr<zx_device> dev;
  zx_status_t status = zx_device::Create(&dev);
  if (status != ZX_OK) {
    return status;
  }

  dev->ops = ops;
  dev->driver = drv;

  if (name == nullptr) {
    printf("devhost: dev=%p has null name.\n", dev.get());
    name = "invalid";
    dev->magic = 0;
  }

  size_t len = strlen(name);
  // TODO(teisenbe): I think this is overly aggresive, and could be changed
  // to |len > ZX_DEVICE_NAME_MAX| and |len = ZX_DEVICE_NAME_MAX|.
  if (len >= ZX_DEVICE_NAME_MAX) {
    printf("devhost: dev=%p name too large '%s'\n", dev.get(), name);
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

static zx_status_t device_validate(const fbl::RefPtr<zx_device_t>& dev) REQ_DM_LOCK {
  if (dev == nullptr) {
    printf("INVAL: nullptr!\n");
    return ZX_ERR_INVALID_ARGS;
  }
  if (dev->flags & DEV_FLAG_ADDED) {
    printf("device already added: %p(%s)\n", dev.get(), dev->name);
    return ZX_ERR_BAD_STATE;
  }
  if (dev->magic != DEV_MAGIC) {
    return ZX_ERR_BAD_STATE;
  }
  if (dev->ops == nullptr) {
    printf("device add: %p(%s): nullptr ops\n", dev.get(), dev->name);
    return ZX_ERR_INVALID_ARGS;
  }
  if ((dev->protocol_id == ZX_PROTOCOL_MISC_PARENT) || (dev->protocol_id == ZX_PROTOCOL_ROOT)) {
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

zx_status_t devhost_device_add(const fbl::RefPtr<zx_device_t>& dev,
                               const fbl::RefPtr<zx_device_t>& parent,
                               const zx_device_prop_t* props, uint32_t prop_count,
                               const char* proxy_args, zx::channel client_remote) REQ_DM_LOCK {
  auto mark_dead = fbl::MakeAutoCall([&dev]() {
    if (dev) {
      dev->flags |= DEV_FLAG_DEAD;
    }
  });

  zx_status_t status;
  if ((status = device_validate(dev)) < 0) {
    return status;
  }
  if (parent == nullptr) {
    printf("device_add: cannot add %p(%s) to nullptr parent\n", dev.get(), dev->name);
    return ZX_ERR_NOT_SUPPORTED;
  }
  if (parent->flags & DEV_FLAG_DEAD) {
    printf("device add: %p: is dead, cannot add child %p\n", parent.get(), dev.get());
    return ZX_ERR_BAD_STATE;
  }

  BindContext* bind_ctx = nullptr;
  CreationContext* creation_ctx = nullptr;

  // If the bind or creation ctx (thread locals) are set, we are in
  // a thread that is handling a bind() or create() callback and if
  // that ctx's parent matches the one provided to add we need to do
  // some additional checking...
  if ((g_bind_context != nullptr) && (g_bind_context->parent == parent)) {
    bind_ctx = g_bind_context;
  }
  if ((g_creation_context != nullptr) && (g_creation_context->parent == parent)) {
    creation_ctx = g_creation_context;
    // create() must create only one child
    if (creation_ctx->child != nullptr) {
      printf("devhost: driver attempted to create multiple proxy devices!\n");
      return ZX_ERR_BAD_STATE;
    }
  }

#if TRACE_ADD_REMOVE
  printf("devhost: device add: %p(%s) parent=%p(%s)\n", dev.get(), dev->name, parent.get(),
         parent->name);
#endif

  // Don't create an event handle if we alredy have one
  if (!dev->event.is_valid() &&
      ((status = zx::eventpair::create(0, &dev->event, &dev->local_event)) < 0)) {
    printf("device add: %p(%s): cannot create event: %d\n", dev.get(), dev->name, status);
    return status;
  }

  dev->flags |= DEV_FLAG_BUSY;

  // proxy devices are created through this handshake process
  if (creation_ctx) {
    if (dev->flags & DEV_FLAG_INVISIBLE) {
      printf("devhost: driver attempted to create invisible device in create()\n");
      return ZX_ERR_INVALID_ARGS;
    }
    dev->flags |= DEV_FLAG_ADDED;
    dev->flags &= (~DEV_FLAG_BUSY);
    dev->rpc = zx::unowned_channel(creation_ctx->rpc);
    creation_ctx->child = dev;
    mark_dead.cancel();
    return ZX_OK;
  }

  dev->parent = parent;

  // attach to our parent
  parent->children.push_back(dev.get());

  if (!(dev->flags & DEV_FLAG_INSTANCE)) {
    // devhost_add always consumes the handle
    status = devhost_add(parent, dev, proxy_args, props, prop_count, std::move(client_remote));
    if (status < 0) {
      printf("devhost: %p(%s): remote add failed %d\n", dev.get(), dev->name, status);
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

#define REMOVAL_BAD_FLAGS (DEV_FLAG_DEAD | DEV_FLAG_BUSY | DEV_FLAG_INSTANCE | DEV_FLAG_MULTI_BIND)

static const char* removal_problem(uint32_t flags) {
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

zx_status_t devhost_device_remove(const fbl::RefPtr<zx_device_t>& dev,
                                  bool unbind_self) REQ_DM_LOCK {
  if (dev->flags & REMOVAL_BAD_FLAGS) {
    printf("device: %p(%s): cannot be removed (%s)\n", dev.get(), dev->name,
           removal_problem(dev->flags));
    return ZX_ERR_INVALID_ARGS;
  }
#if TRACE_ADD_REMOVE
  printf("device: %p(%s): is being scheduled for removal\n", dev.get(), dev->name);
#endif
  // Ask the devcoordinator to schedule the removal of this device and its children.
  devhost_schedule_remove(dev, unbind_self);
  return ZX_OK;
}

void devhost_device_unbind_reply(const fbl::RefPtr<zx_device_t>& dev) REQ_DM_LOCK {
  if (dev->flags & REMOVAL_BAD_FLAGS) {
    printf("device: %p(%s): cannot reply to unbind, bad flags: (%s)\n", dev.get(), dev->name,
           removal_problem(dev->flags));
    panic();
  }
  if (!(dev->flags & DEV_FLAG_UNBOUND)) {
    printf("device: %p(%s): cannot reply to unbind, not in unbinding state, flags are 0x%x\n",
           dev.get(), dev->name, dev->flags);
    panic();
  }
#if TRACE_ADD_REMOVE
  printf("device: %p(%s): sending unbind completed\n", dev.get(), dev->name);
#endif
  devhost_send_unbind_done(dev);
}

zx_status_t devhost_device_remove_deprecated(const fbl::RefPtr<zx_device_t>& dev) REQ_DM_LOCK {
  // This removal is in response to the unbind hook.
  if (dev->flags & DEV_FLAG_UNBOUND) {
    devhost_device_unbind_reply(dev);
    return ZX_OK;
  }
  return devhost_device_remove(dev, false /* unbind_self */);
}

zx_status_t devhost_device_rebind(const fbl::RefPtr<zx_device_t>& dev) REQ_DM_LOCK {
  if (!dev->children.is_empty() || dev->has_composite()) {
    // note that we want to be rebound when our children are all gone
    dev->flags |= DEV_FLAG_WANTS_REBIND;
    // request that any existing children go away
    devhost_schedule_unbind_children(dev);
  } else {
    std::string drv = dev->get_rebind_drv_name().value_or("");
    return devhost_device_bind(dev, drv.c_str());
  }

  return ZX_OK;
}

zx_status_t devhost_device_unbind(const fbl::RefPtr<zx_device_t>& dev) REQ_DM_LOCK {
  enum_lock_acquire();

  if (!(dev->flags & DEV_FLAG_UNBOUND)) {
    dev->flags |= DEV_FLAG_UNBOUND;
    // Call dev's unbind op.
    if (dev->ops->unbind) {
#if TRACE_ADD_REMOVE
      printf("call unbind dev: %p(%s)\n", dev.get(), dev->name);
#endif
      ApiAutoRelock relock;
      dev->UnbindOp();
    } else {
      // We should reply to the unbind hook so we don't get stuck.
      devhost_send_unbind_done(dev);
    }
  }

  enum_lock_release();

  return ZX_OK;
}

zx_status_t devhost_device_complete_removal(const fbl::RefPtr<zx_device_t>& dev) REQ_DM_LOCK {
#if TRACE_ADD_REMOVE
  printf("device: %p(%s): is being removed (removal requested)\n", dev.get(), dev->name);
#endif

  // This recovers the leaked reference that happened in device_add_from_driver().
  auto dev_add_ref = fbl::ImportFromRawPtr(dev.get());
  devhost_remove(std::move(dev_add_ref));

  dev->flags |= DEV_FLAG_DEAD;
  return ZX_OK;
}

zx_status_t devhost_device_open(const fbl::RefPtr<zx_device_t>& dev, fbl::RefPtr<zx_device_t>* out,
                                uint32_t flags) REQ_DM_LOCK {
  if (dev->flags & DEV_FLAG_DEAD) {
    printf("device open: %p(%s) is dead!\n", dev.get(), dev->name);
    return ZX_ERR_BAD_STATE;
  }
  fbl::RefPtr<zx_device_t> new_ref(dev);
  zx_status_t r;
  zx_device_t* opened_dev = nullptr;
  {
    ApiAutoRelock relock;
    r = dev->OpenOp(&opened_dev, flags);
  }
  if (r < 0) {
    new_ref.reset();
  } else if (opened_dev != nullptr) {
    // open created a per-instance device for us
    new_ref.reset();
    // Claim the reference from open
    new_ref = fbl::ImportFromRawPtr(opened_dev);

    if (!(opened_dev->flags & DEV_FLAG_INSTANCE)) {
      printf("device open: %p(%s) in bad state %x\n", opened_dev, opened_dev->name, flags);
      panic();
    }
  }
  *out = std::move(new_ref);
  return r;
}

zx_status_t devhost_device_close(fbl::RefPtr<zx_device_t> dev, uint32_t flags) REQ_DM_LOCK {
  ApiAutoRelock relock;
  return dev->CloseOp(flags);
}

zx_status_t devhost_device_get_dev_power_state_from_mapping(
    const fbl::RefPtr<zx_device>& dev, uint32_t flags, fuchsia_device_SystemPowerStateInfo* info) {
  // TODO(ravoorir) : When the usage of suspend flags is replaced with
  // system power states, this function will not need the switch case.
  // Some suspend flags might be translated to system power states with
  // additional hints (ex: REBOOT/REBOOT_BOOTLOADER/REBOOT_RECOVERY/MEXEC).
  // For now, each of these flags are treated as an individual state.
  fuchsia_device_manager_SystemPowerState sys_state;
  switch (flags & DEVICE_SUSPEND_REASON_MASK) {
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
  const std::array<fuchsia_device_SystemPowerStateInfo,
                   fuchsia_device_manager_MAX_SYSTEM_POWER_STATES>& sys_power_states =
      dev->GetSystemPowerStateMapping();
  *info = sys_power_states[sys_state];
  return ZX_OK;
}

zx_status_t devhost_device_suspend(const fbl::RefPtr<zx_device>& dev, uint32_t flags) REQ_DM_LOCK {
  // TODO this should eventually be two-pass using SUSPENDING/SUSPENDED flags
  enum_lock_acquire();

  zx_status_t status = ZX_ERR_NOT_SUPPORTED;
  // If new suspend hook is implemented, prefer that.
  if (dev->ops->suspend_new) {
    fuchsia_device_SystemPowerStateInfo new_state_info;
    fuchsia_device_DevicePowerState out_state;
    ApiAutoRelock relock;
    status = devhost_device_get_dev_power_state_from_mapping(dev, flags, &new_state_info);
    if (status == ZX_OK) {
      status = dev->ops->suspend_new(dev->ctx, new_state_info.dev_state,
                                     new_state_info.wakeup_enable, &out_state);
    }
  } else if (dev->ops->suspend) {
    // Invoke suspend hook otherwise.
    ApiAutoRelock relock;
    status = dev->ops->suspend(dev->ctx, flags);
  }

  enum_lock_release();

  // default_suspend() returns ZX_ERR_NOT_SUPPORTED
  if ((status != ZX_OK) && (status != ZX_ERR_NOT_SUPPORTED)) {
    return status;
  }
  return ZX_OK;
}

zx_status_t devhost_device_resume(const fbl::RefPtr<zx_device>& dev,
                                  uint32_t target_system_state) REQ_DM_LOCK {
  enum_lock_acquire();

  zx_status_t status = ZX_ERR_NOT_SUPPORTED;
  // If new suspend hook is implemented, prefer that.
  if (dev->ops->resume_new) {
    fuchsia_device_DevicePowerState out_state;
    ApiAutoRelock relock;
    const std::array<fuchsia_device_SystemPowerStateInfo,
                     fuchsia_device_manager_MAX_SYSTEM_POWER_STATES>& sys_power_states =
        dev->GetSystemPowerStateMapping();
    status =
        dev->ops->resume_new(dev->ctx, sys_power_states[target_system_state].dev_state, &out_state);
  } else if (dev->ops->resume) {
    // Invoke resume hook otherwise.
    ApiAutoRelock relock;
    status = dev->ops->resume(dev->ctx, target_system_state);
  }

  enum_lock_release();

  // default_resume() returns ZX_ERR_NOT_SUPPORTED
  if ((status != ZX_OK) && (status != ZX_ERR_NOT_SUPPORTED)) {
    return status;
  }
  return ZX_OK;
}

zx_status_t devhost_device_suspend_new(const fbl::RefPtr<zx_device>& dev,
                                       fuchsia_device_DevicePowerState requested_state,
                                       fuchsia_device_DevicePowerState* out_state) {
  zx_status_t status = ZX_OK;
  if (dev->ops->suspend_new) {
    status =
        dev->ops->suspend_new(dev->ctx, requested_state, false /* wake_configured */, out_state);
  }
  return status;
}

zx_status_t devhost_device_resume_new(const fbl::RefPtr<zx_device>& dev,
                                      fuchsia_device_DevicePowerState requested_state,
                                      fuchsia_device_DevicePowerState* out_state) {
  zx_status_t status = ZX_OK;
  if (dev->ops->resume_new) {
    status = dev->ops->resume_new(dev->ctx, requested_state, out_state);
  }
  return status;
}

}  // namespace devmgr
