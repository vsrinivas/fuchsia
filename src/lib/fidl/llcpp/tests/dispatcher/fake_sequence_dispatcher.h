// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_LLCPP_TESTS_DISPATCHER_FAKE_SEQUENCE_DISPATCHER_H_
#define SRC_LIB_FIDL_LLCPP_TESTS_DISPATCHER_FAKE_SEQUENCE_DISPATCHER_H_

#include <lib/async-testing/dispatcher_stub.h>
#include <lib/async/sequence_id.h>

#include <optional>

namespace fidl_testing {

// |FakeSequenceDispatcher| implements an |async_dispatcher_t| that forwards
// all operations to an underlying dispatcher, with the exception of
// |GetSequenceId|, which returns an artificial sequence ID of the user's
// choosing. This is useful for testing the behavior of FIDL clients and
// servers under different sequence IDs.
class FakeSequenceDispatcher : public async::DispatcherStub {
 public:
  explicit FakeSequenceDispatcher(async_dispatcher_t* underlying_dispatcher)
      : underlying_dispatcher_(underlying_dispatcher) {}

  // Set the sequence ID that will be returned by |GetSequenceId|.
  void SetSequenceId(async_sequence_id_t current_sequence_id);

  zx::time Now() override;
  zx_status_t BeginWait(async_wait_t* wait) override;
  zx_status_t CancelWait(async_wait_t* wait) override;
  zx_status_t PostTask(async_task_t* task) override;
  zx_status_t CancelTask(async_task_t* task) override;
  zx_status_t QueuePacket(async_receiver_t* receiver, const zx_packet_user_t* data) override;
  zx_status_t SetGuestBellTrap(async_guest_bell_trap_t* trap, const zx::guest& guest,
                               zx_vaddr_t addr, size_t length) override;
  zx_status_t BindIrq(async_irq_t* irq) override;
  zx_status_t UnbindIrq(async_irq_t* irq) override;
  zx_status_t CreatePagedVmo(async_paged_vmo_t* paged_vmo, zx_handle_t pager, uint32_t options,
                             uint64_t vmo_size, zx_handle_t* vmo_out) override;
  zx_status_t DetachPagedVmo(async_paged_vmo_t* paged_vmo) override;
  zx_status_t GetSequenceId(async_sequence_id_t* out_sequence_id, const char** out_error) override;
  zx_status_t CheckSequenceId(async_sequence_id_t sequence_id, const char** out_error) override;

 private:
  async_dispatcher_t* underlying_dispatcher_;
  std::optional<async_sequence_id_t> current_sequence_id_ = std::nullopt;
};

}  // namespace fidl_testing

#endif  // SRC_LIB_FIDL_LLCPP_TESTS_DISPATCHER_FAKE_SEQUENCE_DISPATCHER_H_
