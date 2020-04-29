// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// N.B. The offline symbolizer (scripts/symbolize) reads our output,
// don't break it.

#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>
#include <zircon/types.h>

#include <fbl/alloc_checker.h>
#include <fbl/array.h>
#include <ngunwind/fuchsia.h>
#include <ngunwind/libunwind.h>

#include "dso-list-impl.h"
#include "inspector/inspector.h"
#include "utils-impl.h"

namespace inspector {

constexpr unsigned int kBacktraceFrameLimit = 50;

static void btprint(FILE* f, inspector_dsoinfo_t* dso_list, uint32_t n, uintptr_t pc, uintptr_t sp,
                    bool use_new_format) {
  if (use_new_format) {
    fprintf(f, "{{{bt:%u:%#" PRIxPTR ":sp %#" PRIxPTR "}}}\n", n, pc, sp);
    return;
  }

  inspector_dsoinfo_t* dso = inspector_dso_lookup(dso_list, pc);
  if (dso == nullptr) {
    // The pc is not in any DSO.
    fprintf(f, "bt#%02u: pc %p sp %p\n", n, (void*)pc, (void*)sp);
    return;
  }

  fprintf(f, "bt#%02u: pc %p sp %p (%s,%p)", n, (void*)pc, (void*)sp, dso->name,
          (void*)(pc - dso->base));
  fprintf(f, "\n");
}

static int dso_lookup_for_unw(void* context, unw_word_t pc, unw_word_t* base, const char** name) {
  auto dso_list = reinterpret_cast<inspector_dsoinfo_t*>(context);
  inspector_dsoinfo_t* dso = inspector_dso_lookup(dso_list, pc);
  if (dso == nullptr)
    return 0;
  *base = dso->base;
  *name = dso->name;
  return 1;
}

static void inspector_print_backtrace_impl(FILE* f, zx_handle_t process, zx_handle_t thread,
                                           inspector_dsoinfo_t* dso_list, uintptr_t pc,
                                           uintptr_t sp, uintptr_t fp, bool use_libunwind,
                                           bool use_new_format) {
  // Set up libunwind if requested.

  bool libunwind_ok = use_libunwind;
  if (verbosity_level > 0) {
    // Don't turn on libunwind debugging for -d1.
    // Note: max libunwind debugging level is 16
    unw_set_debug_level(verbosity_level - 1);
  }

  unw_fuchsia_info_t* fuchsia = nullptr;
  unw_addr_space_t remote_as = nullptr;

  if (libunwind_ok) {
    fuchsia = unw_create_fuchsia(process, thread, dso_list, dso_lookup_for_unw);
    if (fuchsia == nullptr) {
      print_error("unw_fuchsia_create failed (OOM)");
      libunwind_ok = false;
    }
  }

  if (libunwind_ok) {
    remote_as = unw_create_addr_space((unw_accessors_t*)&_UFuchsia_accessors, 0);
    if (remote_as == nullptr) {
      print_error("unw_create_addr_space failed (OOM)");
      libunwind_ok = false;
    }
  }

  unw_cursor_t cursor;
  if (libunwind_ok) {
    int ret = unw_init_remote(&cursor, remote_as, fuchsia);
    if (ret < 0) {
      print_error("unw_init_remote failed (%d)", ret);
      libunwind_ok = false;
    }
  }

  if (!libunwind_ok) {
    print_error("Unable to initialize libunwind.");
    print_error("Falling back on heuristics which likely won't work");
    print_error("with optimized code.");
  }

  // TODO: Handle libunwind not finding .eh_frame in which case fallback
  // on using heuristics. Ideally this would be handled on a per-DSO basis.

  // On with the show.

  uint32_t n = 1;
  btprint(f, dso_list, n++, pc, sp, use_new_format);
  while ((sp >= 0x1000000) && (n < kBacktraceFrameLimit)) {
    if (libunwind_ok) {
      int ret = unw_step(&cursor);
      if (ret < 0) {
        print_error("unw_step failed for pc %p, aborting backtrace here", (void*)pc);
        break;
      }
      if (ret == 0)
        break;
      unw_word_t val;
      unw_get_reg(&cursor, UNW_REG_IP, &val);
      pc = val;
      unw_get_reg(&cursor, UNW_REG_SP, &val);
      sp = val;
    } else {
      sp = fp;
      if (read_mem(process, fp + 8, &pc, sizeof(pc))) {
        break;
      }
      if (read_mem(process, fp, &fp, sizeof(fp))) {
        break;
      }
    }
    btprint(f, dso_list, n++, pc, sp, use_new_format);
  }

  if (!use_new_format) {
    fprintf(f, "bt#%02d: end\n", n);
  }

  if (n >= kBacktraceFrameLimit) {
    fprintf(f, "warning: backtrace frame limit exceeded; backtrace may be truncated\n");
  }

  unw_destroy_addr_space(remote_as);
  unw_destroy_fuchsia(fuchsia);
}

extern "C" __EXPORT void inspector_print_backtrace_markup(FILE* f, zx_handle_t process,
                                                          zx_handle_t thread,
                                                          inspector_dsoinfo_t* dso_list,
                                                          uintptr_t pc, uintptr_t sp, uintptr_t fp,
                                                          bool use_libunwind) {
  inspector_print_backtrace_impl(f, process, thread, dso_list, pc, sp, fp, use_libunwind, true);
}

extern "C" __EXPORT void inspector_print_backtrace(FILE* f, zx_handle_t process, zx_handle_t thread,
                                                   inspector_dsoinfo_t* dso_list, uintptr_t pc,
                                                   uintptr_t sp, uintptr_t fp, bool use_libunwind) {
  inspector_print_backtrace_impl(f, process, thread, dso_list, pc, sp, fp, use_libunwind, false);
}

}  // namespace inspector
