// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_THIRD_PARTY_LIBUNWINDSTACK_FUCHSIA_REGS_FUCHSIA_H_
#define GARNET_THIRD_PARTY_LIBUNWINDSTACK_FUCHSIA_REGS_FUCHSIA_H_

#include <zircon/types.h>

#include "unwindstack/Regs.h"

struct zx_thread_state_general_regs;

namespace unwindstack {

class RegsFuchsia final : public RegsImpl<uint64_t> {
 public:
  RegsFuchsia();
  virtual ~RegsFuchsia();

  // Sets the registers from the given OS structure.
  void Set(const zx_thread_state_general_regs& input);

  // Populates this class with the registers from the given thread.
  zx_status_t Read(zx_handle_t thread);

  // RegsImpl overrides.
  ArchEnum Arch() final;
  uint64_t GetPcAdjustment(uint64_t rel_pc, Elf* elf) final;
  bool SetPcFromReturnAddress(Memory* process_memory) final;
  bool StepIfSignalHandler(uint64_t rel_pc, Elf* elf,
                           Memory* process_memory) final;
  void IterateRegisters(std::function<void(const char*, uint64_t)>) final;
  uint64_t pc() final;
  uint64_t sp() final;
  void set_pc(uint64_t pc) final;
  void set_sp(uint64_t sp) final;
  Regs* Clone() final;
};

}  // namespace unwindstack

#endif  // GARNET_THIRD_PARTY_LIBUNWINDSTACK_FUCHSIA_REGS_FUCHSIA_H_
