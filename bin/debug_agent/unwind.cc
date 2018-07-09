// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/debug_agent/unwind.h"

#include <inttypes.h>
#include <ngunwind/fuchsia.h>
#include <ngunwind/libunwind.h>
#include <algorithm>

#include "garnet/bin/debug_agent/process_info.h"

namespace debug_agent {

namespace {

using ModuleVector = std::vector<debug_ipc::Module>;

// Callback for libunwind.
int LookupDso(void* context, unw_word_t pc, unw_word_t* base,
              const char** name) {
  // Context is a ModuleVector sorted by load address.
  // TODO(brettw) could use lower_bound for better perf with lots of modules.
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

}  // namespace

zx_status_t UnwindStack(const zx::process& process, uint64_t dl_debug_addr,
                        const zx::thread& thread, uint64_t ip, uint64_t sp,
                        size_t max_depth,
                        std::vector<debug_ipc::StackFrame>* stack) {
  // Get the modules sorted by load address.
  ModuleVector modules;
  zx_status_t status = GetModulesForProcess(process, dl_debug_addr, &modules);
  if (status != ZX_OK)
    return status;
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
  stack->push_back(frame);
  while (frame.sp >= 0x1000000 && stack->size() < max_depth) {
    int ret = unw_step(&cursor);
    if (ret <= 0)
      break;

    unw_word_t val;
    unw_get_reg(&cursor, UNW_REG_IP, &val);
    frame.ip = val;
    unw_get_reg(&cursor, UNW_REG_SP, &val);
    frame.sp = val;

    stack->push_back(frame);
  }

  return ZX_OK;
}

}  // namespace debug_agent
