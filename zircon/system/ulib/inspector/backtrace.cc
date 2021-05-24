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

#include <vector>

#include <fbl/alloc_checker.h>
#include <fbl/array.h>
#include <fbl/string.h>
#include <fbl/string_printf.h>
#include <ngunwind/fuchsia.h>
#include <ngunwind/libunwind.h>

#include "dso-list-impl.h"
#include "inspector/inspector.h"
#include "utils-impl.h"

namespace inspector {

constexpr int kBacktraceFrameLimit = 50;

struct Frame {
  uint64_t pc;
  fbl::String source;
};

static int dso_lookup_for_unw(void* context, unw_word_t pc, unw_word_t* base, const char** name) {
  auto dso_list = reinterpret_cast<inspector_dsoinfo_t*>(context);
  inspector_dsoinfo_t* dso = inspector_dso_lookup(dso_list, pc);
  if (dso == nullptr)
    return 0;
  *base = dso->base;
  *name = dso->name;
  return 1;
}

static std::vector<Frame> unwind_from_ngunwind(zx_handle_t process, zx_handle_t thread,
                                               inspector_dsoinfo_t* dso_list, uintptr_t pc,
                                               uintptr_t sp, uintptr_t fp) {
  // Set up libunwind
  bool libunwind_ok = true;

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

  std::vector<Frame> frames;
  frames.push_back({pc, fbl::StringPrintf("sp %#" PRIxPTR, sp)});

  while ((sp >= 0x1000000) && (frames.size() < kBacktraceFrameLimit)) {
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
    frames.push_back({pc, fbl::StringPrintf("sp %#" PRIxPTR, sp)});
  }

  unw_destroy_addr_space(remote_as);
  unw_destroy_fuchsia(fuchsia);
  return frames;
}

#if defined(__aarch64__)

// Return vector of frames unwound from the shadow call stack, the current frame being the first.
static std::vector<Frame> unwind_from_shadow_call_stack(zx_handle_t process, zx_handle_t thread) {
  zx_thread_state_general_regs_t regs;
  if (inspector_read_general_regs(thread, &regs) != ZX_OK) {
    print_error("inspector_read_general_regs failed");
    return {};
  }

  // The current frame must be obtained from the context.
  std::vector<Frame> frames = {{regs.pc, "from pc"}};

  // It's hard for us to know whether regs.lr is pushed on the SCS or not because some functions
  // that never call a subroutine may skip the step. Instead we'll check whether the first frame in
  // the SCS is equal to lr, which might drop one frame for recursive functions. However, it's
  // acceptable because we are only checking whether SCS is a subsequence of the regular stack
  // below.
  uint64_t lr = regs.lr;

  // If the SCS isn't setup yet, r18 will be 0.
  if (!regs.r[18]) {
    frames.push_back({lr, "from lr"});
    return frames;
  }

  // ssp points to the last entry in the SCS. r18 points to the next free slot in the SCS.
  uint64_t ssp = regs.r[18] - 8;
  uint64_t scs_page[PAGE_SIZE / 8];

  bool loop = true;
  while (loop) {
    // Read the whole page at once for performance.
    uint64_t page_start = ssp / PAGE_SIZE * PAGE_SIZE;
    uint64_t num_frames = (ssp % PAGE_SIZE / 8) + 1;
    if (read_mem(process, page_start, scs_page, num_frames * 8) != ZX_OK) {
      break;
    }

    if (lr) {
      if (lr != scs_page[num_frames - 1]) {
        frames.push_back({lr, "from lr"});
      }
      lr = 0;
    }

    while (num_frames > 0) {
      // pc = 0 marks the end of the SCS.
      if (frames.size() >= kBacktraceFrameLimit || scs_page[num_frames - 1] == 0) {
        loop = false;
        break;
      }
      frames.push_back({scs_page[num_frames - 1],
                        fbl::StringPrintf("ssp %#" PRIxPTR, page_start + (num_frames - 1) * 8)});
      num_frames--;
    }

    ssp = page_start - 8;
  }

  return frames;
}

#else

static std::vector<Frame> unwind_from_shadow_call_stack(zx_handle_t process, zx_handle_t thread) {
  return {};
}

#endif

static void print_stack(FILE* f, const std::vector<Frame>& stack) {
  int n = 0;
  for (auto& frame : stack) {
    fprintf(f, "{{{bt:%u:%#" PRIxPTR ":%s}}}\n", n++, frame.pc, frame.source.c_str());
  }
  if (n >= kBacktraceFrameLimit) {
    fprintf(f, "warning: backtrace frame limit exceeded; backtrace may be truncated\n");
  }
}

extern "C" __EXPORT void inspector_print_backtrace_markup(FILE* f, zx_handle_t process,
                                                          zx_handle_t thread,
                                                          inspector_dsoinfo_t* dso_list,
                                                          uintptr_t pc, uintptr_t sp,
                                                          uintptr_t fp) {
  // Check the consistency between ngunwind's stack and SCS. Print both if they mismatch.
  std::vector<Frame> stack = unwind_from_ngunwind(process, thread, dso_list, pc, sp, fp);
  std::vector<Frame> scs = unwind_from_shadow_call_stack(process, thread);

  // The SCS should be a subsequence of the real stack, because some functions may have SCS disabled
  // and we might drop one frame for recursive functions (see unwind_from_shadow_call_stack above).
  auto scs_it = scs.begin();
  for (auto& frame : stack) {
    if (scs_it != scs.end() && scs_it->pc == frame.pc) {
      scs_it++;
    }
  }

  if (scs_it != scs.end()) {
    print_stack(f, scs);
    fprintf(
        f,
        "warning: the backtrace above is from the shadow call stack because the backtrace from "
        "metadata-based unwinding is incomplete or corrupted. Here's the original backtrace:\n");
  }

  print_stack(f, stack);
}

}  // namespace inspector
