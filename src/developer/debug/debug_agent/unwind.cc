// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/unwind.h"

#include <inttypes.h>
#include <ngunwind/fuchsia.h>
#include <ngunwind/libunwind.h>
#include <algorithm>

#include "garnet/third_party/libunwindstack/fuchsia/MemoryFuchsia.h"
#include "garnet/third_party/libunwindstack/fuchsia/RegsFuchsia.h"
#include "garnet/third_party/libunwindstack/include/unwindstack/Unwinder.h"
#include "src/developer/debug/debug_agent/process_info.h"

namespace debug_agent {

namespace {

using ModuleVector = std::vector<debug_ipc::Module>;

// Default unwinder type to use.
UnwinderType unwinder_type = UnwinderType::kNgUnwind;

zx_status_t UnwindStackAndroid(const zx::process& process,
                               uint64_t dl_debug_addr, const zx::thread& thread,
                               uint64_t ip, uint64_t sp, uint64_t bp,
                               size_t max_depth,
                               std::vector<debug_ipc::StackFrame>* stack) {
  // Ignore errors getting modules, the empty case can at least give the
  // current location, and maybe more if there are stack pointers.
  ModuleVector modules;  // Sorted by load address.
  GetModulesForProcess(process, dl_debug_addr, &modules);
  std::sort(modules.begin(), modules.end(),
            [](const debug_ipc::Module& a, const debug_ipc::Module& b) {
              return a.base < b.base;
            });

  unwindstack::Maps maps;
  for (size_t i = 0; i < modules.size(); i++) {
    // Our module currently doesn't have a size so just report the next
    // address boundary.
    // TODO(brettw) hook up the real size.
    uint64_t end;
    if (i < modules.size() - 1)
      end = modules[i + 1].base;
    else
      end = std::numeric_limits<uint64_t>::max();

    // The offset of the module is the offset in the file where the memory map
    // starts. For libraries, we can currently always assume 0.
    uint64_t offset = 0;

    uint64_t flags = 0;  // We don't have flags.

    // Don't know what this is, it's not set by the Android impl that reads
    // from /proc.
    uint64_t load_bias = 0;

    maps.Add(modules[i].base, end, offset, flags, modules[i].name, load_bias);
  }

  unwindstack::RegsFuchsia regs;
  zx_status_t status = regs.Read(thread.get());
  if (status != ZX_OK)
    return status;

  auto memory = std::make_shared<unwindstack::MemoryFuchsia>(process.get());

  unwindstack::Unwinder unwinder(max_depth, &maps, &regs, std::move(memory));

  unwinder.Unwind();

  stack->resize(unwinder.NumFrames());
  for (size_t i = 0; i < unwinder.NumFrames(); i++) {
    const auto& src = unwinder.frames()[i];
    debug_ipc::StackFrame* dest = &(*stack)[i];
    dest->ip = src.pc;
    dest->sp = src.sp;
  }

  // Add the base pointer for the top stack frame.
  // TODO(brettw) libstackunwind should be able to give us base pointers for
  // other frames when we're compiling with frame pointers.
  (*stack)[0].bp = bp;

  return 0;
}

// Libunwind doesn't have a cross-platform typedef for the frame pointer
// register so define one.
#if defined(__x86_64__)
#define LIBUNWIND_FRAME_POINTER_REGISTER UNW_X86_64_RBP
#elif defined(__aarch64__)
#define LIBUNWIND_FRAME_POINTER_REGISTER UNW_AARCH64_X29
#else
#error Need frame pointer.
#endif

using ModuleVector = std::vector<debug_ipc::Module>;

// Callback for ngunwind.
int LookupDso(void* context, unw_word_t pc, unw_word_t* base,
              const char** name) {
  // Context is a ModuleVector sorted by load address, need to find the
  // largest one smaller than or equal to the pc.
  //
  // We could use lower_bound for better perf with lots of modules but we
  // expect O(10) modules.
  const ModuleVector* modules = static_cast<const ModuleVector*>(context);
  for (int i = static_cast<int>(modules->size()) - 1; i >= 0; i--) {
    const debug_ipc::Module& module = (*modules)[i];
    if (pc >= module.base) {
      *base = module.base;
      *name = module.name.c_str();
      return 1;
    }
  }
  return 0;
}

zx_status_t UnwindStackNgUnwind(const zx::process& process,
                                uint64_t dl_debug_addr,
                                const zx::thread& thread, uint64_t ip,
                                uint64_t sp, uint64_t bp, size_t max_depth,
                                std::vector<debug_ipc::StackFrame>* stack) {
  stack->clear();

  // Ignore errors getting modules, the empty case can at least give the
  // current location, and maybe more if there are stack pointers.
  ModuleVector modules;  // Sorted by load address.
  GetModulesForProcess(process, dl_debug_addr, &modules);
  std::sort(modules.begin(), modules.end(),
            [](const debug_ipc::Module& a, const debug_ipc::Module& b) {
              return a.base < b.base;
            });

  unw_fuchsia_info_t* fuchsia =
      unw_create_fuchsia(process.get(), thread.get(), &modules, &LookupDso);
  if (!fuchsia)
    return ZX_ERR_INTERNAL;

  unw_addr_space_t remote_aspace = unw_create_addr_space(
      const_cast<unw_accessors_t*>(&_UFuchsia_accessors), 0);
  if (!remote_aspace)
    return ZX_ERR_INTERNAL;

  unw_cursor_t cursor;
  if (unw_init_remote(&cursor, remote_aspace, fuchsia) < 0)
    return ZX_ERR_INTERNAL;

  debug_ipc::StackFrame frame;
  frame.ip = ip;
  frame.sp = sp;
  frame.bp = bp;
  stack->push_back(frame);
  while (frame.sp >= 0x1000000 && stack->size() < max_depth) {
    int ret = unw_step(&cursor);
    if (ret <= 0)
      break;

    unw_word_t val;
    unw_get_reg(&cursor, UNW_REG_IP, &val);
    if (val == 0)
      break;  // Null code address means we're done.
    frame.ip = val;

    unw_get_reg(&cursor, UNW_REG_SP, &val);
    frame.sp = val;

    unw_get_reg(&cursor, LIBUNWIND_FRAME_POINTER_REGISTER, &val);
    frame.bp = val;

    // Note that libunwind may theoretically be able to give us all
    // callee-saved register values for a given frame. Currently asking for any
    // register always returns success, making it impossible to tell what is
    // valid and what is not.
    //
    // If we switch unwinders (maybe to LLVM's or a custom one), this should be
    // re-evaluated. We may be able to attach a vector of Register structs on
    // each frame for the values we know about.

    stack->push_back(frame);
  }

  // The last stack entry will typically have a 0 IP address. We want to send
  // this anyway because it will hold the initial stack pointer for the thread,
  // which in turn allows computation of the first real frame's fingerprint.

  return ZX_OK;
}

}  // namespace

void SetUnwinderType(UnwinderType type) { unwinder_type = type; }

zx_status_t UnwindStack(const zx::process& process, uint64_t dl_debug_addr,
                        const zx::thread& thread, uint64_t ip, uint64_t sp,
                        uint64_t bp, size_t max_depth,
                        std::vector<debug_ipc::StackFrame>* stack) {
  switch (unwinder_type) {
    case UnwinderType::kNgUnwind:
      return UnwindStackNgUnwind(process, dl_debug_addr, thread, ip, sp, bp,
                                 max_depth, stack);
    case UnwinderType::kAndroid:
      return UnwindStackAndroid(process, dl_debug_addr, thread, ip, sp, bp,
                                max_depth, stack);
  }
  return ZX_ERR_NOT_SUPPORTED;
}

}  // namespace debug_agent
