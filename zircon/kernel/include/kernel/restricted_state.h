// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_INCLUDE_KERNEL_RESTRICTED_STATE_H_
#define ZIRCON_KERNEL_INCLUDE_KERNEL_RESTRICTED_STATE_H_

#include <lib/zx/result.h>
#include <zircon/syscalls-next.h>

#include <arch/regs.h>
#include <fbl/macros.h>
#include <ktl/unique_ptr.h>

// Per thread state to support restricted mode.
// Intentionally kept simple to keep the amount of kernel/thread.h dependencies to a minimum.

// Architecture specific state base class, final specialized in in arch/<arch>/restricted.h below.
class ArchRestrictedStateImpl {
 public:
  ArchRestrictedStateImpl() = default;
  virtual ~ArchRestrictedStateImpl() = default;
  DISALLOW_COPY_ASSIGN_AND_MOVE(ArchRestrictedStateImpl);

  // Prior to entering restricted mode, ask the arch layer to validate the saved register state is
  // valid. Return true if valid.
  //
  // Possible invalid states: program counter outside of user space, invalid processor flags, etc.
  virtual bool ValidatePreRestrictedEntry() = 0;

  // Just prior to entering restricted mode, give the arch layer a chance to save any state it
  // may need for the return trip back to normal mode.
  // For example, the GS/FS base is saved here for x86.
  virtual void SaveStatePreRestrictedEntry() = 0;

  // Use an architcturally-specific mechanism to directly enter user space in restricted mode.
  // Does not return.
  [[noreturn]] virtual void EnterRestricted() = 0;

  // Having just exited from restricted mode via a syscall, save the necessary restricted mode
  // state from a pointer to the syscall state saved by the exception handler.
  virtual void SaveRestrictedSyscallState(const syscall_regs_t* regs) = 0;

  // Enter normal mode at the address pointed to by vector_table with arguments code and context
  // in an architecturally specific register in an architecturally specific way.
  [[noreturn]] virtual void EnterFull(uintptr_t vector_table, uintptr_t context, uint64_t code) = 0;

  virtual void Dump() = 0;

  void SetState(const zx_restricted_state& state) { state_ = state; }
  const zx_restricted_state& state() const { return state_; }

 protected:
  zx_restricted_state state_{};
};

// Arch-specific restricted.h must provide ArchRestrictedState.
// It should inherit and override everything in ArchRestrictedStateImpl, mark it final,
// name the resulting class ArchRestrictedState, or another name and set an alias with using.
#include <arch/restricted.h>

// Per thread restricted state.
// Mostly just a wrapper around ArchRestrictedState to try to lazy allocate the
// expensive register state.
class RestrictedState {
 public:
  RestrictedState() = default;
  ~RestrictedState() = default;
  DISALLOW_COPY_ASSIGN_AND_MOVE(RestrictedState);

  bool in_restricted() const { return in_restricted_; }
  uintptr_t vector_ptr() const { return vector_ptr_; }
  uintptr_t context() const { return context_; }

  void set_in_restricted(bool r) { in_restricted_ = r; }
  void set_vector_ptr(uintptr_t v) { vector_ptr_ = v; }
  void set_context(uintptr_t c) { context_ = c; }

  // Accessor for the arch restricted state member.
  // Will allocate on first call.
  zx::result<ArchRestrictedState*> GetArchState() {
    // If we've already allocated it, just return the existing pointer.
    if (likely(arch_)) {
      return zx::ok(arch_.get());
    }

    // Allocate a new restricted arch state on demand.
    fbl::AllocChecker ac;
    arch_ = ktl::make_unique<ArchRestrictedState>(&ac);
    if (!ac.check()) {
      return zx::error(ZX_ERR_NO_MEMORY);
    }

    return zx::ok(arch_.get());
  }

 private:
  bool in_restricted_ = false;
  uintptr_t vector_ptr_ = 0;
  uintptr_t context_ = 0;
  ktl::unique_ptr<ArchRestrictedState> arch_;  // allocated on demand to save space
};

#endif  // ZIRCON_KERNEL_INCLUDE_KERNEL_RESTRICTED_STATE_H_
