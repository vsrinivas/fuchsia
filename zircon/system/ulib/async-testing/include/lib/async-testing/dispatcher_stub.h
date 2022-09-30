// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ASYNC_TESTING_DISPATCHER_STUB_H_
#define LIB_ASYNC_TESTING_DISPATCHER_STUB_H_

#include <lib/async/dispatcher.h>
#include <lib/async/sequence_id.h>
#include <lib/zx/guest.h>
#include <lib/zx/time.h>

namespace async {

struct DispatcherStub : public async_dispatcher_t {
 public:
  DispatcherStub();
  virtual ~DispatcherStub();

  DispatcherStub(const DispatcherStub&) = delete;
  DispatcherStub& operator=(const DispatcherStub&) = delete;

  DispatcherStub(DispatcherStub&&) = delete;
  DispatcherStub& operator=(DispatcherStub&&) = delete;

  virtual zx::time Now();
  virtual zx_status_t BeginWait(async_wait_t* wait);
  virtual zx_status_t CancelWait(async_wait_t* wait);
  virtual zx_status_t PostTask(async_task_t* task);
  virtual zx_status_t CancelTask(async_task_t* task);
  virtual zx_status_t QueuePacket(async_receiver_t* receiver, const zx_packet_user_t* data);
  virtual zx_status_t SetGuestBellTrap(async_guest_bell_trap_t* trap, const zx::guest& guest,
                                       zx_vaddr_t addr, size_t length);
  virtual zx_status_t BindIrq(async_irq_t* irq);
  virtual zx_status_t UnbindIrq(async_irq_t* irq);
  virtual zx_status_t CreatePagedVmo(async_paged_vmo_t* paged_vmo, zx_handle_t pager,
                                     uint32_t options, uint64_t vmo_size, zx_handle_t* vmo_out);
  virtual zx_status_t DetachPagedVmo(async_paged_vmo_t* paged_vmo);
  virtual zx_status_t GetSequenceId(async_sequence_id_t* out_sequence_id, const char** out_error);
  virtual zx_status_t CheckSequenceId(async_sequence_id_t sequence_id, const char** out_error);
};

}  // namespace async

#endif  // LIB_ASYNC_TESTING_DISPATCHER_STUB_H_
