// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/unwinder/fuchsia.h"

#include <zircon/syscalls.h>

namespace unwinder {

Error FuchsiaMemory::ReadBytes(uint64_t addr, uint64_t size, void* dst) {
  size_t actual = 0;
  zx_status_t status = zx_process_read_memory(process_, addr, dst, size, &actual);
  if (status != ZX_OK) {
    return Error("zx_process_read_memory: %d", status);
  }
  if (actual != size) {
    return Error("zx_process_read_memory short read: expect %zu, got %zu", size, actual);
  }
  return Success();
}

unwinder::Registers FromFuchsiaRegisters(const zx_thread_state_general_regs& regs) {
#if defined(__x86_64__)
  unwinder::Registers res(unwinder::Registers::Arch::kX64);
  // The first 4 registers are out-of-order.
  res.Set(unwinder::RegisterID::kX64_rax, regs.rax);
  res.Set(unwinder::RegisterID::kX64_rbx, regs.rbx);
  res.Set(unwinder::RegisterID::kX64_rcx, regs.rcx);
  res.Set(unwinder::RegisterID::kX64_rdx, regs.rdx);
  for (int i = 4; i < static_cast<int>(unwinder::RegisterID::kX64_last); i++) {
    res.Set(static_cast<unwinder::RegisterID>(i), reinterpret_cast<const uint64_t*>(&regs)[i]);
  }
#elif defined(__aarch64__)
  unwinder::Registers res(unwinder::Registers::Arch::kArm64);
  for (int i = 0; i < static_cast<int>(unwinder::RegisterID::kArm64_last); i++) {
    res.Set(static_cast<unwinder::RegisterID>(i), reinterpret_cast<const uint64_t*>(&regs)[i]);
  }
#else
#error What platform?
#endif
  return res;
}

}  // namespace unwinder
