// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/fidl/llcpp/tests/dispatcher/fake_sequence_dispatcher.h"

#include <lib/async/cpp/time.h>
#include <lib/async/irq.h>
#include <lib/async/paged_vmo.h>
#include <lib/async/receiver.h>
#include <lib/async/task.h>
#include <lib/async/trap.h>
#include <lib/async/wait.h>
#include <zircon/assert.h>

namespace fidl_testing {

void FakeSequenceDispatcher::SetSequenceId(async_sequence_id_t current_sequence_id) {
  current_sequence_id_.emplace(current_sequence_id);
}

zx::time FakeSequenceDispatcher::Now() { return async::Now(underlying_dispatcher_); }

zx_status_t FakeSequenceDispatcher::BeginWait(async_wait_t* wait) {
  return async_begin_wait(underlying_dispatcher_, wait);
}

zx_status_t FakeSequenceDispatcher::CancelWait(async_wait_t* wait) {
  return async_cancel_wait(underlying_dispatcher_, wait);
}

zx_status_t FakeSequenceDispatcher::PostTask(async_task_t* task) {
  return async_post_task(underlying_dispatcher_, task);
}

zx_status_t FakeSequenceDispatcher::CancelTask(async_task_t* task) {
  return async_post_task(underlying_dispatcher_, task);
}

zx_status_t FakeSequenceDispatcher::QueuePacket(async_receiver_t* receiver,
                                                const zx_packet_user_t* data) {
  return async_queue_packet(underlying_dispatcher_, receiver, data);
}

zx_status_t FakeSequenceDispatcher::SetGuestBellTrap(async_guest_bell_trap_t* trap,
                                                     const zx::guest& guest, zx_vaddr_t addr,
                                                     size_t length) {
  return async_set_guest_bell_trap(underlying_dispatcher_, trap, guest.get(), addr, length);
}

zx_status_t FakeSequenceDispatcher::BindIrq(async_irq_t* irq) {
  return async_bind_irq(underlying_dispatcher_, irq);
}

zx_status_t FakeSequenceDispatcher::UnbindIrq(async_irq_t* irq) {
  return async_unbind_irq(underlying_dispatcher_, irq);
}

zx_status_t FakeSequenceDispatcher::CreatePagedVmo(async_paged_vmo_t* paged_vmo, zx_handle_t pager,
                                                   uint32_t options, uint64_t vmo_size,
                                                   zx_handle_t* vmo_out) {
  return async_create_paged_vmo(underlying_dispatcher_, paged_vmo, options, pager, vmo_size,
                                vmo_out);
}

zx_status_t FakeSequenceDispatcher::DetachPagedVmo(async_paged_vmo_t* paged_vmo) {
  return async_detach_paged_vmo(underlying_dispatcher_, paged_vmo);
}

zx_status_t FakeSequenceDispatcher::GetSequenceId(async_sequence_id_t* out_sequence_id,
                                                  const char** out_error) {
  ZX_ASSERT(current_sequence_id_.has_value());
  *out_sequence_id = current_sequence_id_.value();
  return ZX_OK;
}

zx_status_t FakeSequenceDispatcher::CheckSequenceId(async_sequence_id_t sequence_id,
                                                    const char** out_error) {
  ZX_ASSERT(current_sequence_id_.has_value());
  if (sequence_id.value != current_sequence_id_.value().value) {
    *out_error = "Wrong sequence ID from fake dispatcher";
    return ZX_ERR_OUT_OF_RANGE;
  }
  return ZX_OK;
}

}  // namespace fidl_testing
