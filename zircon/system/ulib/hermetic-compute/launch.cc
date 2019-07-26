// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/hermetic-compute/hermetic-compute.h>

#include <climits>
#include <cstdarg>
#include <lib/zx/thread.h>
#include <zircon/assert.h>
#include <zircon/syscalls/debug.h>

namespace {

void SetShadowCallStack(zx_thread_state_general_regs_t* regs, uintptr_t tos);

#ifdef __aarch64__

constexpr auto kPcRegister = &zx_thread_state_general_regs_t::pc;
constexpr auto kSpRegister = &zx_thread_state_general_regs_t::sp;
constexpr intptr_t kSpBias = 0;
constexpr auto kThreadRegister = &zx_thread_state_general_regs_t::tpidr;

constexpr bool kShadowCallStack = true;
void SetShadowCallStack(zx_thread_state_general_regs_t* regs, uintptr_t tos) { regs->r[18] = tos; }

template <size_t... I>
auto RegisterAccessors(std::index_sequence<I...>) {
  struct Accessor {
    size_t i;
    auto& operator()(zx_thread_state_general_regs_t& regs) const { return regs.r[i]; }
  };
  return std::array<Accessor, sizeof...(I)>{Accessor{I}...};
}

auto ArgumentRegisters() { return RegisterAccessors(std::make_index_sequence<8>()); }

#elif defined(__x86_64__)

constexpr auto kPcRegister = &zx_thread_state_general_regs_t::rip;
constexpr auto kSpRegister = &zx_thread_state_general_regs_t::rsp;
constexpr intptr_t kSpBias = -8;
constexpr auto kThreadRegister = &zx_thread_state_general_regs_t::fs_base;

constexpr bool kShadowCallStack = false;

std::array<decltype(std::mem_fn(kSpRegister)), 6> ArgumentRegisters() {
  return {
      std::mem_fn(&zx_thread_state_general_regs_t::rdi),
      std::mem_fn(&zx_thread_state_general_regs_t::rsi),
      std::mem_fn(&zx_thread_state_general_regs_t::rdx),
      std::mem_fn(&zx_thread_state_general_regs_t::rcx),
      std::mem_fn(&zx_thread_state_general_regs_t::r8),
      std::mem_fn(&zx_thread_state_general_regs_t::r9),
  };
}

#else
#error "what machine?"
#endif

}  // namespace

HermeticComputeProcess::Launcher::~Launcher() {
  // Make sure a stillborn thread never starts running in user mode.
  // The token is invalid if it's been handed off to an agent.
  if (status_ != ZX_OK && token_) {
    ZX_DEBUG_ASSERT(thread_);
    thread_.kill();
  }
}

zx_handle_t HermeticComputeProcess::Launcher::SendHandle(zx::handle handle) {
  if (status_ == ZX_OK && thread_) {
    // It's already been started, so it's too late.
    status_ = ZX_ERR_BAD_STATE;
  }

  if (status_ == ZX_OK) {
    status_ = engine_.Start(std::move(handle), &thread_, &token_);
  }

  if (status_ == ZX_OK) {
    // Now fetch the registers to discover the remote handle value.
    zx_thread_state_general_regs_t regs;
    status_ = thread_.read_state(ZX_THREAD_STATE_GENERAL_REGS, &regs, sizeof(regs));
    if (status_ == ZX_OK) {
      return static_cast<zx_handle_t>(ArgumentRegisters()[0](regs));
    }
  }

  return ZX_HANDLE_INVALID;
}

void HermeticComputeProcess::Launcher::Launch(size_t nargs, ...) {
  // Bail out early if parameter packing reported errors.
  if (status_ != ZX_OK) {
    return;
  }

  // Called before LoadElf?
  if (entry_pc_ == 0) {
    status_ = ZX_ERR_BAD_STATE;
    return;
  }

  zx_thread_state_general_regs_t regs{};
  regs.*kPcRegister = entry_pc_;

  // Allocate the stacks and TCB.
  auto allocate = [&](size_t size, uintptr_t* base, uintptr_t* tos, size_t* tos_pos) {
    zx::vmo vmo;
    if (status_ == ZX_OK && size > 0) {
      uintptr_t addr = 0;
      status_ = engine_.LoadStack(&size, &vmo, &addr);
      if (base) {
        *base = addr;
      }
      if (tos) {
        *tos = addr + size;
      }
      if (tos_pos) {
        *tos_pos = size;
      }
    }
    return vmo;
  };

  // The TCB points to the unsafe stack, which needs no other setup.
  {
    uintptr_t unsafe_sp = 0;
    allocate(stack_size_, nullptr, &unsafe_sp, nullptr);
    uintptr_t stack_guard = 0;
    zx_cprng_draw(&stack_guard, sizeof(stack_guard));
    uintptr_t tcb_ptr = 0;
    zx::vmo tcb_vmo = allocate(sizeof(hermetic::Tcb), &tcb_ptr, nullptr, nullptr);
    hermetic::Tcb tcb(tcb_ptr, stack_guard, unsafe_sp);
    regs.*kThreadRegister = tcb_ptr + tcb.ThreadPointerOffset();
    status_ = tcb_vmo.write(&tcb, 0, sizeof(tcb));
  }

  // The shadow call stack pointer goes directly into a register.
  if constexpr (kShadowCallStack) {
    uintptr_t sc_sp = 0;
    // TODO(mcgrathr): configurability for ssc size?
    if (stack_size_ > 0) {
      allocate(PAGE_SIZE, nullptr, &sc_sp, nullptr);
    }
    SetShadowCallStack(&regs, sc_sp);
  }

  // The first several arguments go directly into registers.
  va_list args;
  va_start(args, nargs);

  for (const auto& regarg : ArgumentRegisters()) {
    if (nargs == 0) {
      break;
    }
    --nargs;
    regarg(regs) = va_arg(args, uintptr_t);
  }

  // The machine stack is used for passing any remaining arguments.
  // It's always kept double-word aligned.
  const size_t arg_space = sizeof(uintptr_t) * ((nargs + 1) & -size_t{2});

  uintptr_t stack_top = 0;
  size_t stack_top_offset = 0;
  zx::vmo stack_vmo =
      allocate(std::max(stack_size_, arg_space - kSpBias), nullptr, &stack_top, &stack_top_offset);

  for (size_t next = stack_top_offset - arg_space; status_ == ZX_OK && nargs > 0;
       --nargs, next += sizeof(uintptr_t)) {
    uintptr_t arg = va_arg(args, uintptr_t);
    status_ = stack_vmo.write(&arg, next, sizeof(arg));
  }

  va_end(args);

  regs.*kSpRegister = stack_top - arg_space + kSpBias;

  // Now everything is in place in memory and the registers are known.

  // If the thread hasn't been created yet, do it now.
  if (status_ == ZX_OK && !thread_) {
    status_ = engine_.Start({}, &thread_, &token_);
  }

  // Write the register state into the thread and then it's ready to run.
  if (status_ == ZX_OK) {
    status_ = thread_.write_state(ZX_THREAD_STATE_GENERAL_REGS, &regs, sizeof(regs));
  }

  // If there was a Suspended argument, it takes ownership of the
  // thread and token handles and decides when to let it run.
  if (status_ == ZX_OK && suspended_) {
    *suspended_->thread = std::move(thread_);
    *suspended_->token = std::move(token_);
  }
}
