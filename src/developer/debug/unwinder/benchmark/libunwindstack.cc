// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/unwinder/benchmark/libunwindstack.h"

#include "lib/syslog/cpp/macros.h"
#include "snapshot/thread_snapshot.h"
#include "src/developer/debug/third_party/libunwindstack/include/unwindstack/UcontextArm64.h"
#include "src/developer/debug/third_party/libunwindstack/include/unwindstack/UcontextX86_64.h"
#include "src/developer/debug/third_party/libunwindstack/include/unwindstack/Unwinder.h"

namespace benchmark {

namespace {

std::unique_ptr<unwindstack::Regs> GetUnwindRegsARM64(const crashpad::CPUContextARM64& ctx,
                                                      size_t stack_size) {
  unwindstack::arm64_ucontext_t ucontext;

  ucontext.uc_stack.ss_sp = ctx.sp;
  ucontext.uc_stack.ss_size = stack_size;
  ucontext.uc_mcontext.pstate = ctx.spsr;

  for (size_t i = 0; i <= 31; i++) {
    ucontext.uc_mcontext.regs[i] = ctx.regs[i];
  }

  ucontext.uc_mcontext.regs[unwindstack::Arm64Reg::ARM64_REG_PC] = ctx.pc;

  return std::unique_ptr<unwindstack::Regs>(
      unwindstack::Regs::CreateFromUcontext(unwindstack::ArchEnum::ARCH_ARM64, &ucontext));
}

std::unique_ptr<unwindstack::Regs> GetUnwindRegsX86_64(const crashpad::CPUContextX86_64& ctx,
                                                       size_t stack_size) {
  unwindstack::x86_64_ucontext_t ucontext;

  ucontext.uc_stack.ss_sp = ctx.rsp;
  ucontext.uc_stack.ss_size = stack_size;
  ucontext.uc_mcontext.rax = ctx.rax;
  ucontext.uc_mcontext.rbx = ctx.rbx;
  ucontext.uc_mcontext.rcx = ctx.rcx;
  ucontext.uc_mcontext.rdx = ctx.rdx;
  ucontext.uc_mcontext.rsi = ctx.rsi;
  ucontext.uc_mcontext.rdi = ctx.rdi;
  ucontext.uc_mcontext.rbp = ctx.rbp;
  ucontext.uc_mcontext.rsp = ctx.rsp;
  ucontext.uc_mcontext.r8 = ctx.r8;
  ucontext.uc_mcontext.r9 = ctx.r9;
  ucontext.uc_mcontext.r10 = ctx.r10;
  ucontext.uc_mcontext.r11 = ctx.r11;
  ucontext.uc_mcontext.r12 = ctx.r12;
  ucontext.uc_mcontext.r13 = ctx.r13;
  ucontext.uc_mcontext.r14 = ctx.r14;
  ucontext.uc_mcontext.r15 = ctx.r15;
  ucontext.uc_mcontext.rip = ctx.rip;

  return std::unique_ptr<unwindstack::Regs>(
      unwindstack::Regs::CreateFromUcontext(unwindstack::ArchEnum::ARCH_X86_64, &ucontext));
}

unwinder::RegisterID RegisterNameToId(const char* name) {
  if (!strcmp(name, "rax"))
    return unwinder::RegisterID::kX64_rax;
  if (!strcmp(name, "rbx"))
    return unwinder::RegisterID::kX64_rbx;
  if (!strcmp(name, "rcx"))
    return unwinder::RegisterID::kX64_rcx;
  if (!strcmp(name, "rdx"))
    return unwinder::RegisterID::kX64_rdx;
  if (!strcmp(name, "r8"))
    return unwinder::RegisterID::kX64_r8;
  if (!strcmp(name, "r9"))
    return unwinder::RegisterID::kX64_r9;
  if (!strcmp(name, "r10"))
    return unwinder::RegisterID::kX64_r10;
  if (!strcmp(name, "r11"))
    return unwinder::RegisterID::kX64_r11;
  if (!strcmp(name, "r12"))
    return unwinder::RegisterID::kX64_r12;
  if (!strcmp(name, "r13"))
    return unwinder::RegisterID::kX64_r13;
  if (!strcmp(name, "r14"))
    return unwinder::RegisterID::kX64_r14;
  if (!strcmp(name, "r15"))
    return unwinder::RegisterID::kX64_r15;
  if (!strcmp(name, "rdi"))
    return unwinder::RegisterID::kX64_rdi;
  if (!strcmp(name, "rsi"))
    return unwinder::RegisterID::kX64_rsi;
  if (!strcmp(name, "rbp"))
    return unwinder::RegisterID::kX64_rbp;
  if (!strcmp(name, "rsp"))
    return unwinder::RegisterID::kX64_rsp;
  if (!strcmp(name, "rip"))
    return unwinder::RegisterID::kX64_rip;
  if (!strcmp(name, "x0"))
    return unwinder::RegisterID::kArm64_x0;
  if (!strcmp(name, "x1"))
    return unwinder::RegisterID::kArm64_x1;
  if (!strcmp(name, "x2"))
    return unwinder::RegisterID::kArm64_x2;
  if (!strcmp(name, "x3"))
    return unwinder::RegisterID::kArm64_x3;
  if (!strcmp(name, "x4"))
    return unwinder::RegisterID::kArm64_x4;
  if (!strcmp(name, "x5"))
    return unwinder::RegisterID::kArm64_x5;
  if (!strcmp(name, "x6"))
    return unwinder::RegisterID::kArm64_x6;
  if (!strcmp(name, "x7"))
    return unwinder::RegisterID::kArm64_x7;
  if (!strcmp(name, "x8"))
    return unwinder::RegisterID::kArm64_x8;
  if (!strcmp(name, "x9"))
    return unwinder::RegisterID::kArm64_x9;
  if (!strcmp(name, "x10"))
    return unwinder::RegisterID::kArm64_x10;
  if (!strcmp(name, "x11"))
    return unwinder::RegisterID::kArm64_x11;
  if (!strcmp(name, "x12"))
    return unwinder::RegisterID::kArm64_x12;
  if (!strcmp(name, "x13"))
    return unwinder::RegisterID::kArm64_x13;
  if (!strcmp(name, "x14"))
    return unwinder::RegisterID::kArm64_x14;
  if (!strcmp(name, "x15"))
    return unwinder::RegisterID::kArm64_x15;
  if (!strcmp(name, "x16"))
    return unwinder::RegisterID::kArm64_x16;
  if (!strcmp(name, "x17"))
    return unwinder::RegisterID::kArm64_x17;
  if (!strcmp(name, "x18"))
    return unwinder::RegisterID::kArm64_x18;
  if (!strcmp(name, "x19"))
    return unwinder::RegisterID::kArm64_x19;
  if (!strcmp(name, "x20"))
    return unwinder::RegisterID::kArm64_x20;
  if (!strcmp(name, "x21"))
    return unwinder::RegisterID::kArm64_x21;
  if (!strcmp(name, "x22"))
    return unwinder::RegisterID::kArm64_x22;
  if (!strcmp(name, "x23"))
    return unwinder::RegisterID::kArm64_x23;
  if (!strcmp(name, "x24"))
    return unwinder::RegisterID::kArm64_x24;
  if (!strcmp(name, "x25"))
    return unwinder::RegisterID::kArm64_x25;
  if (!strcmp(name, "x26"))
    return unwinder::RegisterID::kArm64_x26;
  if (!strcmp(name, "x27"))
    return unwinder::RegisterID::kArm64_x27;
  if (!strcmp(name, "x28"))
    return unwinder::RegisterID::kArm64_x28;
  if (!strcmp(name, "x29"))
    return unwinder::RegisterID::kArm64_x29;
  if (!strcmp(name, "sp"))
    return unwinder::RegisterID::kArm64_sp;
  if (!strcmp(name, "lr"))
    return unwinder::RegisterID::kArm64_lr;
  if (!strcmp(name, "pc"))
    return unwinder::RegisterID::kArm64_pc;
  return unwinder::RegisterID::kInvalid;
}

}  // namespace

std::vector<unwinder::Frame> UnwindFromLibunwindstack(
    const std::shared_ptr<MinidumpMemory>& memory,
    std::vector<const crashpad::ModuleSnapshot*> modules, const crashpad::ThreadSnapshot* thread) {
  size_t stack_size = 0;
  if (auto stack = thread->Stack()) {
    stack_size = stack->Size();
  }

  const auto& context = *thread->Context();
  std::unique_ptr<unwindstack::Regs> regs;
  unwinder::Registers::Arch arch;

  switch (context.architecture) {
    case crashpad::CPUArchitecture::kCPUArchitectureARM64:
      arch = unwinder::Registers::Arch::kArm64;
      regs = GetUnwindRegsARM64(*context.arm64, stack_size);
      break;
    case crashpad::CPUArchitecture::kCPUArchitectureX86_64:
      arch = unwinder::Registers::Arch::kX64;
      regs = GetUnwindRegsX86_64(*context.x86_64, stack_size);
      break;
    default:
      FX_NOTREACHED();
  }

  std::sort(modules.begin(), modules.end(),
            [](auto a, auto b) { return a->Address() < b->Address(); });

  unwindstack::Maps maps;
  for (const auto& mod : modules) {
    maps.Add(mod->Address(), mod->Address() + mod->Size(), 0, 0, mod->Name(), 0);
  }

  unwindstack::Unwinder unwinder(40, &maps, regs.get(), memory, true);
  unwinder.SetResolveNames(false);
  unwinder.Unwind();

  std::vector<unwinder::Frame> res;
  for (size_t i = 0; i < unwinder.NumFrames(); i++) {
    const auto& src = unwinder.frames()[i];
    unwinder::Registers dest(arch);
    if (src.regs) {
      src.regs->IterateRegisters([&dest](const char* name, uint64_t val) {
        if (auto id = RegisterNameToId(name); id != unwinder::RegisterID::kInvalid) {
          dest.Set(id, val);
        }
      });
    }
    // unwindstack will adjust the pc for all frames except the bottom-most one. The logic lives in
    // RegsFuchsia::GetPcAdjustment and is required in order to get the correct cfa_offset. However,
    // it's not ideal for us because we want return addresses rather than call sites for previous
    // frames. So we restore the pc here.
    if (i == 0) {
      dest.SetPC(src.pc);
    } else {
      dest.SetPC(src.pc + regs->GetPcAdjustment(src.pc, nullptr));
    }
    dest.SetSP(src.sp);
    res.emplace_back(std::move(dest), unwinder::Frame::Trust::kCFI, unwinder::Success());
  }
  return res;
}

}  // namespace benchmark
