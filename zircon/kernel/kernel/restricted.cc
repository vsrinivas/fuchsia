// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#include "kernel/restricted.h"

#include <arch.h>
#include <inttypes.h>
#include <lib/zx/result.h>
#include <stdlib.h>
#include <trace.h>
#include <zircon/syscalls-next.h>

#include <arch/regs.h>
#include <fbl/alloc_checker.h>
#include <object/process_dispatcher.h>
#include <object/thread_dispatcher.h>
#include <vm/vm.h>

#define LOCAL_TRACE 0

// Kernel implementation of restricted mode. Most of these routines are more or less directly
// called from a corresponding syscall. The rest are up called from architecturally specific
// hardware traps, such as an exception or syscall when the cpu is in restricted mode.

// Dispatched directly from arch-specific syscall handler. Called after saving state
// on the stack, but before trying to dispatch as a zircon syscall.
extern "C" [[noreturn]] void syscall_from_restricted(const syscall_regs_t* regs);
extern "C" [[noreturn]] void syscall_from_restricted(const syscall_regs_t* regs) {
  LTRACEF("regs %p\n", regs);

  DEBUG_ASSERT(arch_ints_disabled());

  RestrictedState& rs = Thread::Current().restricted_state();

  // get a handle to the arch specific buffer
  zx::result<ArchRestrictedState*> _arch = rs.GetArchState();
  ASSERT_MSG(_arch.is_ok(), "unable to get handle to arch state\n");
  ArchRestrictedState* arch = *_arch;

  // assert that some things make sense
  DEBUG_ASSERT(rs.in_restricted() == true);
  DEBUG_ASSERT(is_user_accessible(rs.vector_ptr()));

  // set on the thread and the arch level that we're exiting restricted mode
  rs.set_in_restricted(false);
  arch_set_restricted_flag(false);

  // save the restricted state
  arch->SaveRestrictedSyscallState(regs);

  LTRACEF("returning to normal mode at vector %#lx, contex %#lx\n", rs.vector_ptr(), rs.context());

  ProcessDispatcher* up = ProcessDispatcher::GetCurrent();
  vmm_set_active_aspace(up->normal_aspace_ptr());

  // bounce into normal mode
  arch->EnterFull(rs.vector_ptr(), rs.context(), 0);

  __UNREACHABLE;
}

// entry points

zx_status_t RestrictedEnter(uint32_t options, uintptr_t vector_table_ptr, uintptr_t context) {
  LTRACEF("options %#x vector %#" PRIx64 " context %#" PRIx64 "\n", options, vector_table_ptr,
          context);

  ArchRestrictedState* arch;

  // open a new scope in case there are any RAII variables we want cleaned
  // up before heading towards the NO_RETURN portion of this function
  {
    // no options defined for the moment
    if (options != 0) {
      return ZX_ERR_INVALID_ARGS;
    }

    // validate the vector table pointer
    if (!is_user_accessible(vector_table_ptr)) {
      return ZX_ERR_INVALID_ARGS;
    }

    // load the restricted state buffer
    RestrictedState& rs = Thread::Current::restricted_state();

    DEBUG_ASSERT(!rs.in_restricted());

    // get a handle to the arch specific buffer
    zx::result<ArchRestrictedState*> _arch = rs.GetArchState();
    if (unlikely(_arch.is_error())) {
      return _arch.error_value();
    }
    arch = *_arch;

#if LOCAL_TRACE
    arch->Dump();
#endif

    // validate the user state is valid (PC is in user space, etc)
    if (unlikely(!arch->ValidatePreRestrictedEntry())) {
      return ZX_ERR_BAD_STATE;
    }

    // from now on out we're committed, disable interrupts so we can do this
    // without being interrupted as we save/restore state
    arch_disable_ints();

    // no more errors or interrupts, so we can switch the active aspace
    // without worrying about ending up in a situation where the thread is
    // set to normal with the restricted aspace active.
    ProcessDispatcher* up = ProcessDispatcher::GetCurrent();
    VmAspace* restricted_aspace = up->restricted_aspace();
    // This check can be removed once the restricted mode tests can and do run with a restricted
    // aspace.
    if (restricted_aspace) {
      vmm_set_active_aspace(restricted_aspace);
    }

    // save the vector table pointer for return from restricted mode
    rs.set_vector_ptr(vector_table_ptr);

    // save the context ptr for return from restricted mode
    rs.set_context(context);

    // set our state to restricted enabled at the thread and arch level
    rs.set_in_restricted(true);
    arch_set_restricted_flag(true);
  }
  // no more RAII from here on out since we're heading towards a NO_RETURN
  // function.

  // get a chance to save some state before we enter normal mode
  arch->SaveStatePreRestrictedEntry();

  // enter restricted mode
  arch->EnterRestricted();

  // does not return
}

zx_status_t RestrictedWriteState(user_in_ptr<const void> data, size_t data_size) {
  LTRACEF("data_size %zu\n", data_size);

  // we only support writing the entire state at once
  if (data_size != sizeof(zx_restricted_state)) {
    return ZX_ERR_INVALID_ARGS;
  }

  // copy the data in
  zx_restricted_state state{};
  zx_status_t status = data.reinterpret<const zx_restricted_state>().copy_from_user(&state);
  if (status != ZX_OK) {
    return status;
  }

  // successful, overwrite our saved state
  RestrictedState& rs = Thread::Current::restricted_state();

  // get a handle to the arch specific buffer
  zx::result<ArchRestrictedState*> _arch = rs.GetArchState();
  if (_arch.is_error()) {
    return _arch.error_value();
  }
  ArchRestrictedState* arch = *_arch;

  // copy the entire state. validation will be done at restricted enter time
  arch->SetState(state);

  return ZX_OK;
}

zx_status_t RestrictedReadState(user_out_ptr<void> data, size_t data_size) {
  LTRACEF("data_size %zu\n", data_size);

  // we only support reading the entire state at once
  if (data_size != sizeof(zx_restricted_state)) {
    return ZX_ERR_INVALID_ARGS;
  }

  // get a handle to the arch specific buffer
  RestrictedState& rs = Thread::Current::restricted_state();
  zx::result<ArchRestrictedState*> _arch = rs.GetArchState();
  if (_arch.is_error()) {
    return _arch.error_value();
  }
  ArchRestrictedState* arch = *_arch;
  const zx_restricted_state& state = arch->state();

  // copy out to user space
  return data.reinterpret<zx_restricted_state>().copy_to_user(state);
}
