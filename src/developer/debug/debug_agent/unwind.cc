// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/unwind.h"

#include <inttypes.h>

#include <algorithm>

#include <ngunwind/fuchsia.h>
#include <ngunwind/libunwind.h>

#include "src/developer/debug/debug_agent/arch.h"
#include "src/developer/debug/debug_agent/general_registers.h"
#include "src/developer/debug/debug_agent/module_list.h"
#include "src/developer/debug/debug_agent/process_handle.h"
#include "src/developer/debug/debug_agent/thread_handle.h"
#include "src/developer/debug/third_party/libunwindstack/fuchsia/MemoryFuchsia.h"
#include "src/developer/debug/third_party/libunwindstack/fuchsia/RegsFuchsia.h"
#include "src/developer/debug/third_party/libunwindstack/include/unwindstack/Unwinder.h"
#include "src/lib/containers/cpp/array_view.h"

namespace debug_agent {

namespace {

using debug_ipc::RegisterID;

using ModuleVector = std::vector<debug_ipc::Module>;

// Default unwinder type to use.
UnwinderType unwinder_type = UnwinderType::kNgUnwind;

// These are the registers we attempt to extract from stack frams from NGUnwind This does not
// include the IP/SP which are specially handled separately.
struct NGUnwindRegisterMap {
  int ngunwind;   // NGUnwind's register #define.
  RegisterID id;  // Our RegisterID for the same thing.
};
containers::array_view<NGUnwindRegisterMap> GetNGUnwindGeneralRegisters() {
  // clang-format off
#if defined(__x86_64__)
  static NGUnwindRegisterMap kGeneral[] = {
      {UNW_X86_64_RAX, RegisterID::kX64_rax},
      {UNW_X86_64_RBX, RegisterID::kX64_rbx},
      {UNW_X86_64_RCX, RegisterID::kX64_rcx},
      {UNW_X86_64_RDX, RegisterID::kX64_rdx},
      {UNW_X86_64_RSI, RegisterID::kX64_rsi},
      {UNW_X86_64_RDI, RegisterID::kX64_rdi},
      {UNW_X86_64_RBP, RegisterID::kX64_rbp},
      {UNW_X86_64_R8, RegisterID::kX64_r8},
      {UNW_X86_64_R9, RegisterID::kX64_r9},
      {UNW_X86_64_R10, RegisterID::kX64_r10},
      {UNW_X86_64_R11, RegisterID::kX64_r11},
      {UNW_X86_64_R12, RegisterID::kX64_r12},
      {UNW_X86_64_R13, RegisterID::kX64_r13},
      {UNW_X86_64_R14, RegisterID::kX64_r14},
      {UNW_X86_64_R15, RegisterID::kX64_r15}};
#elif defined(__aarch64__)
  static NGUnwindRegisterMap kGeneral[] = {
      {UNW_AARCH64_X0, RegisterID::kARMv8_x0},
      {UNW_AARCH64_X1, RegisterID::kARMv8_x1},
      {UNW_AARCH64_X2, RegisterID::kARMv8_x2},
      {UNW_AARCH64_X3, RegisterID::kARMv8_x3},
      {UNW_AARCH64_X4, RegisterID::kARMv8_x4},
      {UNW_AARCH64_X5, RegisterID::kARMv8_x5},
      {UNW_AARCH64_X6, RegisterID::kARMv8_x6},
      {UNW_AARCH64_X7, RegisterID::kARMv8_x7},
      {UNW_AARCH64_X8, RegisterID::kARMv8_x8},
      {UNW_AARCH64_X9, RegisterID::kARMv8_x9},
      {UNW_AARCH64_X10, RegisterID::kARMv8_x10},
      {UNW_AARCH64_X11, RegisterID::kARMv8_x11},
      {UNW_AARCH64_X12, RegisterID::kARMv8_x12},
      {UNW_AARCH64_X13, RegisterID::kARMv8_x13},
      {UNW_AARCH64_X14, RegisterID::kARMv8_x14},
      {UNW_AARCH64_X15, RegisterID::kARMv8_x15},
      {UNW_AARCH64_X16, RegisterID::kARMv8_x16},
      {UNW_AARCH64_X17, RegisterID::kARMv8_x17},
      {UNW_AARCH64_X18, RegisterID::kARMv8_x18},
      {UNW_AARCH64_X19, RegisterID::kARMv8_x19},
      {UNW_AARCH64_X20, RegisterID::kARMv8_x20},
      {UNW_AARCH64_X21, RegisterID::kARMv8_x21},
      {UNW_AARCH64_X22, RegisterID::kARMv8_x22},
      {UNW_AARCH64_X23, RegisterID::kARMv8_x23},
      {UNW_AARCH64_X24, RegisterID::kARMv8_x24},
      {UNW_AARCH64_X25, RegisterID::kARMv8_x25},
      {UNW_AARCH64_X26, RegisterID::kARMv8_x26},
      {UNW_AARCH64_X27, RegisterID::kARMv8_x27},
      {UNW_AARCH64_X28, RegisterID::kARMv8_x28},
      {UNW_AARCH64_X29, RegisterID::kARMv8_x29},
      {UNW_AARCH64_X30, RegisterID::kARMv8_lr}};
#else
#error Write for your platform
#endif
  // clang-format on
  return containers::array_view<NGUnwindRegisterMap>(std::begin(kGeneral), std::end(kGeneral));
}

zx_status_t UnwindStackAndroid(const ProcessHandle& process, const ModuleList& modules,
                               const ThreadHandle& thread, const GeneralRegisters& regs,
                               size_t max_depth, std::vector<debug_ipc::StackFrame>* stack) {
  unwindstack::Maps maps;
  for (size_t i = 0; i < modules.modules().size(); i++) {
    // Our module currently doesn't have a size so just report the next address boundary.
    // TODO(brettw) hook up the real size.
    uint64_t end;
    if (i < modules.modules().size() - 1)
      end = modules.modules()[i + 1].base;
    else
      end = std::numeric_limits<uint64_t>::max();

    // The offset of the module is the offset in the file where the memory map starts. For
    // libraries, we can currently always assume 0.
    uint64_t offset = 0;

    uint64_t flags = 0;  // We don't have flags.

    // Don't know what this is, it's not set by the Android impl that reads
    // from /proc.
    uint64_t load_bias = 0;

    maps.Add(modules.modules()[i].base, end, offset, flags, modules.modules()[i].name, load_bias);
  }

  unwindstack::RegsFuchsia unwind_regs;
  unwind_regs.Set(regs.GetNativeRegisters());

  auto memory = std::make_shared<unwindstack::MemoryFuchsia>(process.GetNativeHandle().get());

  // Always ask for one more frame than requested so we can get the canonical frame address for the
  // frames we do return (the CFA is the previous frame's stack pointer at the time of the call).
  unwindstack::Unwinder unwinder(max_depth + 1, &maps, &unwind_regs, std::move(memory), true);
  // We don't need names from the unwinder since those are computed in the client. This will
  // generally fail anyway since the target binaries don't usually have symbols, so turning off
  // makes it a little more efficient.
  unwinder.SetResolveNames(false);

  unwinder.Unwind();

  stack->reserve(unwinder.NumFrames());
  for (size_t i = 0; i < unwinder.NumFrames(); i++) {
    const auto& src = unwinder.frames()[i];

    if (i > 0) {
      // The next frame's canonical frame address is our stack pointer.
      debug_ipc::StackFrame* next_frame = &(*stack)[i - 1];
      next_frame->cfa = src.sp;
    }

    // This termination condition is in the middle here because we don't know for sure if the
    // unwinder was able to return the number of frames we requested, and we always want to fill in
    // the CFA (above) for the returned frames if possible.
    if (i == max_depth)
      break;

    debug_ipc::StackFrame* dest = &stack->emplace_back();
    dest->ip = src.pc;
    dest->sp = src.sp;
    if (src.regs) {
      src.regs->IterateRegisters([&dest](const char* name, uint64_t val) {
        // TODO(sadmac): It'd be nice to be using some sort of ID constant instead of a converted
        // string here.
        auto id = debug_ipc::StringToRegisterID(name);
        if (id != RegisterID::kUnknown) {
          dest->regs.emplace_back(id, val);
        }
      });
    }
  }

  return 0;
}

// Callback for ngunwind.
int LookupDso(void* context, unw_word_t pc, unw_word_t* base, const char** name) {
  // Context is a ModuleVector sorted by load address, need to find the largest one smaller than or
  // equal to the pc.
  //
  // We could use lower_bound for better perf with lots of modules but we expect O(10) modules.
  const ModuleList* modules = static_cast<const ModuleList*>(context);
  for (int i = static_cast<int>(modules->modules().size()) - 1; i >= 0; i--) {
    const debug_ipc::Module& module = modules->modules()[i];
    if (pc >= module.base) {
      *base = module.base;
      *name = module.name.c_str();
      return 1;
    }
  }
  return 0;
}

zx_status_t UnwindStackNgUnwind(const ProcessHandle& process, const ModuleList& modules,
                                const ThreadHandle& thread, const GeneralRegisters& regs,
                                size_t max_depth, std::vector<debug_ipc::StackFrame>* stack) {
  stack->clear();

  // Any of these functions can fail if the program or thread was killed out from under us.
  unw_fuchsia_info_t* fuchsia = unw_create_fuchsia(
      process.GetNativeHandle().get(), thread.GetNativeHandle().get(), (void*)&modules, &LookupDso);
  if (!fuchsia)
    return ZX_ERR_INTERNAL;

  unw_addr_space_t remote_aspace =
      unw_create_addr_space(const_cast<unw_accessors_t*>(&_UFuchsia_accessors), 0);
  if (!remote_aspace)
    return ZX_ERR_INTERNAL;

  unw_cursor_t cursor;
  if (unw_init_remote(&cursor, remote_aspace, fuchsia) < 0)
    return ZX_ERR_INTERNAL;

  // Compute the register IDs for this platform's IP/SP.
  auto arch = arch::GetCurrentArch();
  RegisterID ip_reg_id = GetSpecialRegisterID(arch, debug_ipc::SpecialRegisterType::kIP);
  RegisterID sp_reg_id = GetSpecialRegisterID(arch, debug_ipc::SpecialRegisterType::kSP);

  // Top stack frame.
  debug_ipc::StackFrame frame;
  frame.ip = regs.ip();
  frame.sp = regs.sp();
  frame.cfa = 0;
  regs.CopyTo(frame.regs);
  stack->push_back(std::move(frame));

  while (frame.sp >= 0x1000000 && stack->size() < max_depth + 1) {
    int ret = unw_step(&cursor);
    if (ret <= 0)
      break;

    // Clear registers left over from previous frame.
    frame.regs.clear();

    unw_word_t val;
    unw_get_reg(&cursor, UNW_REG_IP, &val);
    if (val == 0)
      break;  // Null code address means we're done.
    frame.ip = val;
    frame.regs.emplace_back(ip_reg_id, val);

    unw_get_reg(&cursor, UNW_REG_SP, &val);
    frame.sp = val;
    frame.regs.emplace_back(sp_reg_id, val);

    // Previous frame's CFA is our SP.
    if (!stack->empty())
      stack->back().cfa = val;

    // Other registers.
    for (auto& [ng_id, reg_id] : GetNGUnwindGeneralRegisters()) {
      unw_get_reg(&cursor, ng_id, &val);
      frame.regs.emplace_back(reg_id, val);
    }

    // This "if" statement prevents adding more than the max number of stack entries since we
    // requested one more from libunwind to get the CFA.
    if (stack->size() < max_depth)
      stack->push_back(frame);
  }

  // The last stack entry will typically have a 0 IP address. We want to send this anyway because it
  // will hold the initial stack pointer for the thread, which in turn allows computation of the
  // first real frame's fingerprint.

  return ZX_OK;
}

}  // namespace

void SetUnwinderType(UnwinderType type) { unwinder_type = type; }

zx_status_t UnwindStack(const ProcessHandle& process, const ModuleList& modules,
                        const ThreadHandle& thread, const GeneralRegisters& regs, size_t max_depth,
                        std::vector<debug_ipc::StackFrame>* stack) {
  switch (unwinder_type) {
    case UnwinderType::kNgUnwind:
      return UnwindStackNgUnwind(process, modules, thread, regs, max_depth, stack);
    case UnwinderType::kAndroid:
      return UnwindStackAndroid(process, modules, thread, regs, max_depth, stack);
  }
  return ZX_ERR_NOT_SUPPORTED;
}

}  // namespace debug_agent
