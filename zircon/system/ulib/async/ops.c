// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/receiver.h>
#include <lib/async/task.h>
#include <lib/async/time.h>
#include <lib/async/trap.h>
#include <lib/async/wait.h>

zx_time_t async_now(async_dispatcher_t* dispatcher) { return dispatcher->ops->v1.now(dispatcher); }

zx_status_t async_begin_wait(async_dispatcher_t* dispatcher, async_wait_t* wait) {
  return dispatcher->ops->v1.begin_wait(dispatcher, wait);
}

zx_status_t async_cancel_wait(async_dispatcher_t* dispatcher, async_wait_t* wait) {
  return dispatcher->ops->v1.cancel_wait(dispatcher, wait);
}

zx_status_t async_post_task(async_dispatcher_t* dispatcher, async_task_t* task) {
  return dispatcher->ops->v1.post_task(dispatcher, task);
}

zx_status_t async_cancel_task(async_dispatcher_t* dispatcher, async_task_t* task) {
  return dispatcher->ops->v1.cancel_task(dispatcher, task);
}

zx_status_t async_queue_packet(async_dispatcher_t* dispatcher, async_receiver_t* receiver,
                               const zx_packet_user_t* data) {
  return dispatcher->ops->v1.queue_packet(dispatcher, receiver, data);
}

zx_status_t async_set_guest_bell_trap(async_dispatcher_t* dispatcher, async_guest_bell_trap_t* trap,
                                      zx_handle_t guest, zx_vaddr_t addr, size_t length) {
  return dispatcher->ops->v1.set_guest_bell_trap(dispatcher, trap, guest, addr, length);
}

zx_status_t async_bind_irq(async_dispatcher_t* dispatcher, async_irq_t* irq) {
  if (dispatcher->ops->version < ASYNC_OPS_V2) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  return dispatcher->ops->v2.bind_irq(dispatcher, irq);
}

zx_status_t async_unbind_irq(async_dispatcher_t* dispatcher, async_irq_t* irq) {
  if (dispatcher->ops->version < ASYNC_OPS_V2) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  return dispatcher->ops->v2.unbind_irq(dispatcher, irq);
}

zx_status_t async_create_paged_vmo(async_dispatcher_t* dispatcher, async_paged_vmo_t* paged_vmo,
                                   uint32_t options, zx_handle_t pager, uint64_t vmo_size,
                                   zx_handle_t* vmo_out) {
  if (dispatcher->ops->version < ASYNC_OPS_V2) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  return dispatcher->ops->v2.create_paged_vmo(dispatcher, paged_vmo, options, pager, vmo_size,
                                              vmo_out);
}

zx_status_t async_detach_paged_vmo(async_dispatcher_t* dispatcher, async_paged_vmo_t* paged_vmo) {
  if (dispatcher->ops->version < ASYNC_OPS_V2) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  return dispatcher->ops->v2.detach_paged_vmo(dispatcher, paged_vmo);
}
