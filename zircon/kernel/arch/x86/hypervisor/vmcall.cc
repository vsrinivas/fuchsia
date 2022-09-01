// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/syscalls/forward.h>
#include <lib/syscalls/safe-syscall-argument.h>
#include <lib/syscalls/zx-syscall-numbers.h>
#include <lib/user_copy/user_ptr.h>
#include <zircon/assert.h>
#include <zircon/syscalls/port.h>
#include <zircon/syscalls/types.h>

#include <arch/x86/hypervisor/invalidate.h>
#include <arch/x86/hypervisor/vmx_state.h>
#include <ktl/bit.h>

#include "vmcall_priv.h"

#define LOCAL_TRACE 0

#define LPRINTF(str, x...)       \
  do {                           \
    if constexpr (LOCAL_TRACE) { \
      dprintf(SPEW, str, x);     \
    }                            \
  } while (false)

namespace {

// Stores a newly created handle from a syscall.
//
// This class is used to simplify the process of managing handles as part of
// syscall dispatch.
class GuestHandle {
 public:
  user_out_handle* out() { return &out_; }
  void set_value(zx_handle_t* value) { value_ = value; }
  zx_status_t begin_copyout(ProcessDispatcher* process) {
    return out_.begin_copyout(process, user_out_ptr<zx_handle_t>(value_));
  }
  void finish_copyout(ProcessDispatcher* process) { out_.finish_copyout(process); }

 private:
  user_out_handle out_;
  zx_handle_t* value_;
};

// Casts a `uint64` to type `T`.
template <typename T>
struct Abi {
  static T Cast(uint64_t value, GuestHandle*) { return SafeSyscallArgument<T>::Sanitize(value); }
};

// Casts a `uint64` to a `user_ptr`.
template <typename T, internal::InOutPolicy Policy>
struct Abi<internal::user_ptr<T, Policy>> {
  static internal::user_ptr<T, Policy> Cast(uint64_t value, GuestHandle*) {
    return internal::user_ptr<T, Policy>(SafeSyscallArgument<T*>::Sanitize(value));
  }
};

// Casts a `uint64` to a `user_out_handle*`.
template <>
struct Abi<user_out_handle*> {
  static user_out_handle* Cast(uint64_t value, GuestHandle*& handle) {
    handle->set_value(SafeSyscallArgument<zx_handle_t*>::Sanitize(value));
    user_out_handle* out = handle->out();
    ++handle;
    return out;
  }
};

// Convert argument `i` from a `uint64_t` to type `T`.
//
// NOTE: When making changes to this code, or other code that is part of
// `Vmcall`, please validate the generated assembly. It should be minimal, and
// closely resemble hand-written assembly. Most of this should get optimised
// away by the compiler.
template <typename T>
T AbiArg(GuestState& guest_state, GuestHandle*& handle, size_t i) {
  switch (i) {
    case 0:
      return Abi<T>::Cast(guest_state.rdi, handle);
    case 1:
      return Abi<T>::Cast(guest_state.rsi, handle);
    case 2:
      return Abi<T>::Cast(guest_state.rdx, handle);
    case 3:
      return Abi<T>::Cast(guest_state.r10, handle);
    case 4:
      return Abi<T>::Cast(guest_state.r8, handle);
    case 5:
      return Abi<T>::Cast(guest_state.r9, handle);
    case 6:
      return Abi<T>::Cast(guest_state.r12, handle);
    case 7:
      return Abi<T>::Cast(guest_state.r13, handle);
    default:
      ZX_PANIC("syscall defined with %lu args", i);
  }
}

template <size_t NumArgs>
class Vmcall {
 public:
  // Dispatch a `syscall` using `guest_state`.
  template <typename R, typename... Args>
  static void Dispatch(GuestState& guest_state, R (*syscall)(Args...)) {
    constexpr size_t kNumHandles = (ktl::is_same_v<user_out_handle*, Args> + ... + 0);
    if constexpr (kNumHandles == 0) {
      DispatchWithResult(guest_state, nullptr, syscall);
    } else {
      DispatchWithHandles<kNumHandles>(guest_state, syscall);
    }
  }

 private:
  // Dispatch a `syscall` that creates handles, then make the handles available
  // to the calling process.
  template <size_t NumHandles, typename R, typename... Args>
  static void DispatchWithHandles(GuestState& guest_state, R (*syscall)(Args...)) {
    GuestHandle handles[NumHandles];
    DispatchWithResult(guest_state, handles, syscall);

    ProcessDispatcher* current_process = ProcessDispatcher::GetCurrent();
    for (auto& handle : handles) {
      zx_status_t status = handle.begin_copyout(current_process);
      if (status != ZX_OK) {
        guest_state.rax = ZX_ERR_INVALID_ARGS;
        return;
      }
    }
    for (auto& handle : handles) {
      handle.finish_copyout(current_process);
    }
  }

  // Dispatch a `syscall`, and if the syscall has a return value, store it
  // within `guest_state.rax`.
  template <typename R, typename... Args>
  static void DispatchWithResult(GuestState& guest_state, GuestHandle* handles,
                                 R (*syscall)(Args...)) {
    if constexpr (ktl::is_same_v<void, R>) {
      // If we are handling a syscall without a return value.
      DispatchSyscall(guest_state, handles, syscall, std::make_index_sequence<NumArgs>());
    } else {
      R result =
          DispatchSyscall(guest_state, handles, syscall, std::make_index_sequence<NumArgs>());
      guest_state.rax = result;
      if constexpr (ktl::is_same_v<zx_status_t, R>) {
        // If we are handling a syscall with a `zx_status_t` return value.
        if (result != ZX_OK) {
          return;
        }
      }
    }
  }

  // Dispatch a `syscall`, using `AbiArg` to cast each register to the
  // appropriate argument type.
  template <typename R, typename... Args, size_t... Indices>
  static R DispatchSyscall(GuestState& guest_state, [[maybe_unused]] GuestHandle* handles,
                           R (*syscall)(Args...), std::index_sequence<Indices...>) {
    return syscall(AbiArg<Args>(guest_state, handles, Indices)...);
  }
};

// These don't have kernel entry points.
#define VDSO_SYSCALL(...)

// These are the direct kernel entry points.
#define KERNEL_SYSCALL(name, type, attrs, nargs, arglist, prototype) \
  void vmcall_##name(GuestState& guest_state) {                      \
    LPRINTF("vmcall: %s\n", #name);                                  \
    Vmcall<nargs>::Dispatch(guest_state, sys_##name);                \
  }
#define INTERNAL_SYSCALL(...) KERNEL_SYSCALL(__VA_ARGS__)
#define BLOCKING_SYSCALL(...) KERNEL_SYSCALL(__VA_ARGS__)

#include <lib/syscalls/kernel.inc>

#undef VDSO_SYSCALL
#undef KERNEL_SYSCALL
#undef INTERNAL_SYSCALL
#undef BLOCKING_SYSCALL

// These don't have kernel entry points.
#define VDSO_SYSCALL(...)

// These are the direct kernel entry points.
#define KERNEL_SYSCALL(name, type, attrs, nargs, arglist, prototype) \
  [ZX_SYS_##name] = vmcall_##name,
#define INTERNAL_SYSCALL(...) KERNEL_SYSCALL(__VA_ARGS__)
#define BLOCKING_SYSCALL(...) KERNEL_SYSCALL(__VA_ARGS__)

using VmcallHandler = void (*)(GuestState&);

#if defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wc99-designator"
#endif
constexpr VmcallHandler kVmcallHandlers[ZX_SYS_COUNT] = {
#include <lib/syscalls/kernel.inc>
};
#if defined(__clang__)
#pragma GCC diagnostic pop
#endif

#undef VDSO_SYSCALL
#undef KERNEL_SYSCALL
#undef INTERNAL_SYSCALL
#undef BLOCKING_SYSCALL

// Provide special handling when setting the FS register. This is used for TLS
// by ELF binaries, and we must correctly set the VCPU state accordingly.
zx_status_t vmcall_register_fs(GuestState& guest_state, uintptr_t& fs_base) {
  if (SafeSyscallArgument<size_t>::Sanitize(guest_state.r10) < sizeof(uintptr_t)) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  auto up = ProcessDispatcher::GetCurrent();
  auto handle = SafeSyscallArgument<zx_handle_t>::Sanitize(guest_state.rdi);
  fbl::RefPtr<ThreadDispatcher> thread;
  zx_status_t status =
      up->handle_table().GetDispatcherWithRights(*up, handle, ZX_RIGHT_SET_PROPERTY, &thread);
  if (status != ZX_OK) {
    return status;
  }

  auto value = make_user_in_ptr(SafeSyscallArgument<const void*>::Sanitize(guest_state.rdx));
  status = value.reinterpret<const uintptr_t>().copy_from_user(&fs_base);
  if (status != ZX_OK) {
    return status;
  }
  if (!x86_is_vaddr_canonical(fs_base)) {
    return ZX_ERR_INVALID_ARGS;
  }

  return ZX_OK;
}

}  // namespace

zx_status_t vmcall_dispatch(GuestState& guest_state, uintptr_t& fs_base, zx_port_packet_t& packet) {
  if (guest_state.rax >= ZX_SYS_COUNT) {
    guest_state.rax = ZX_ERR_BAD_SYSCALL;
    return ZX_OK;
  }
  switch (guest_state.rax) {
    case ZX_SYS_object_set_property:
      if (guest_state.rsi == ZX_PROP_REGISTER_FS) {
        LPRINTF("vmcall: %s\n", "object_set_property");
        guest_state.rax = vmcall_register_fs(guest_state, fs_base);
        return ZX_OK;
      }
      break;
    case ZX_SYS_process_exit:
      LPRINTF("vmcall: %s\n", "process_exit");
      packet.type = ZX_PKT_TYPE_GUEST_VCPU;
      packet.guest_vcpu.type = ZX_PKT_GUEST_VCPU_EXIT;
      packet.guest_vcpu.exit.retcode = SafeSyscallArgument<int64_t>::Sanitize(guest_state.rdi);
      return ZX_ERR_NEXT;
    case ZX_SYS_thread_exit:
      LPRINTF("vmcall: %s\n", "thread_exit");
      packet.type = ZX_PKT_TYPE_GUEST_VCPU;
      packet.guest_vcpu.type = ZX_PKT_GUEST_VCPU_EXIT;
      return ZX_ERR_NEXT;
    case ZX_SYS_thread_start:
      LPRINTF("vmcall: %s\n", "thread_start");
      packet.type = ZX_PKT_TYPE_GUEST_VCPU;
      packet.guest_vcpu.type = ZX_PKT_GUEST_VCPU_STARTUP;
      guest_state.rax = ZX_OK;
      return ZX_ERR_NEXT;
  }
  kVmcallHandlers[guest_state.rax](guest_state);
  return ZX_OK;
}
