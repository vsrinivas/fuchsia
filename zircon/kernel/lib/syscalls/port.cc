// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <inttypes.h>
#include <lib/ktrace.h>
#include <lib/syscalls/forward.h>
#include <trace.h>
#include <zircon/errors.h>
#include <zircon/syscalls/policy.h>
#include <zircon/types.h>

#include <fbl/alloc_checker.h>
#include <fbl/ref_ptr.h>
#include <object/handle.h>
#include <object/port_dispatcher.h>
#include <object/process_dispatcher.h>

#define LOCAL_TRACE 0

// zx_status_t zx_port_create
zx_status_t sys_port_create(uint32_t options, user_out_handle* out) {
  LTRACEF("options %u\n", options);
  auto up = ProcessDispatcher::GetCurrent();
  zx_status_t result = up->EnforceBasicPolicy(ZX_POL_NEW_PORT);
  if (result != ZX_OK) {
    return result;
  }

  KernelHandle<PortDispatcher> handle;
  zx_rights_t rights;

  result = PortDispatcher::Create(options, &handle, &rights);
  if (result != ZX_OK) {
    return result;
  }

  result = out->make(ktl::move(handle), rights);

  return result;
}

// zx_status_t zx_port_queue
zx_status_t sys_port_queue(zx_handle_t handle, user_in_ptr<const zx_port_packet_t> packet_in) {
  LTRACEF("handle %x\n", handle);

  auto up = ProcessDispatcher::GetCurrent();

  fbl::RefPtr<PortDispatcher> port;
  zx_status_t status =
      up->handle_table().GetDispatcherWithRights(*up, handle, ZX_RIGHT_WRITE, &port);
  if (status != ZX_OK)
    return status;

  zx_port_packet_t packet;
  status = packet_in.copy_from_user(&packet);
  if (status != ZX_OK) {
    return status;
  }

  return port->QueueUser(packet);
}

// zx_status_t zx_port_wait
zx_status_t sys_port_wait(zx_handle_t handle, zx_time_t deadline,
                          user_out_ptr<zx_port_packet_t> packet_out) {
  LTRACEF("handle %x\n", handle);

  auto up = ProcessDispatcher::GetCurrent();

  fbl::RefPtr<PortDispatcher> port;
  zx_status_t status =
      up->handle_table().GetDispatcherWithRights(*up, handle, ZX_RIGHT_READ, &port);
  if (status != ZX_OK) {
    return status;
  }

  const Deadline slackDeadline(deadline, up->GetTimerSlackPolicy());

  zx_port_packet_t pp;
  zx_status_t st = port->Dequeue(slackDeadline, &pp);

  if (st != ZX_OK) {
    return st;
  }
  status = packet_out.copy_to_user(pp);
  if (status != ZX_OK) {
    return status;
  }

  return ZX_OK;
}

// zx_status_t zx_port_cancel
zx_status_t sys_port_cancel(zx_handle_t handle, zx_handle_t source, uint64_t key) {
  auto up = ProcessDispatcher::GetCurrent();

  fbl::RefPtr<PortDispatcher> port;
  zx_status_t status =
      up->handle_table().GetDispatcherWithRights(*up, handle, ZX_RIGHT_WRITE, &port);
  if (status != ZX_OK) {
    return status;
  }

  {
    Guard<BrwLockPi, BrwLockPi::Reader> guard{up->handle_table().get_lock()};
    Handle* watched = up->handle_table().GetHandleLocked(*up, source);
    if (!watched) {
      return ZX_ERR_BAD_HANDLE;
    }
    if (!watched->HasRights(ZX_RIGHT_WAIT)) {
      return ZX_ERR_ACCESS_DENIED;
    }

    auto dispatcher = watched->dispatcher();
    if (!dispatcher->is_waitable()) {
      return ZX_ERR_NOT_SUPPORTED;
    }

    bool had_observer = dispatcher->CancelByKey(watched, port.get(), key);
    bool packet_removed = port->CancelQueued(watched, key);
    return (had_observer || packet_removed) ? ZX_OK : ZX_ERR_NOT_FOUND;
  }
}
