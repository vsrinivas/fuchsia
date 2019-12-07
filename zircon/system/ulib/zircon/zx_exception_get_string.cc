// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/exception.h>

#include "private.h"

const char* _zx_exception_get_string(zx_excp_type_t exception) {
  switch (exception) {
    case ZX_EXCP_GENERAL:
      return "ZX_EXCP_GENERAL";
    case ZX_EXCP_FATAL_PAGE_FAULT:
      return "ZX_EXCP_FATAL_PAGE_FAULT";
    case ZX_EXCP_UNDEFINED_INSTRUCTION:
      return "ZX_EXCP_UNDEFINED_INSTRUCTION";
    case ZX_EXCP_SW_BREAKPOINT:
      return "ZX_EXCP_SW_BREAKPOINT";
    case ZX_EXCP_HW_BREAKPOINT:
      return "ZX_EXCP_HW_BREAKPOINT";
    case ZX_EXCP_UNALIGNED_ACCESS:
      return "ZX_EXCP_UNALIGNED_ACCESS";
    case ZX_EXCP_THREAD_STARTING:
      return "ZX_EXCP_THREAD_STARTING";
    case ZX_EXCP_THREAD_EXITING:
      return "ZX_EXCP_THREAD_EXITING";
    case ZX_EXCP_POLICY_ERROR:
      return "ZX_EXCP_POLICY_ERROR";
    case ZX_EXCP_PROCESS_STARTING:
      return "ZX_EXCP_PROCESS_STARTING";
    default:
      return "(UNKNOWN)";

    // TODO(mcgrathr): Having this extra case here (a value far away from
    // the other values) forces LLVM to disable its switch->table-lookup
    // optimization.  That optimization produces a table of pointers in
    // rodata, which is not PIC-friendly (requires a dynamic reloc for each
    // element) and so makes the vDSO build bomb out at link time.  Some
    // day we'll teach LLVM either to disable this optimization in PIC mode
    // when it would result in dynamic relocs, or (ideally) to generate a
    // PIC-friendly lookup table like it does for jump tables.
    case 99999:
      return "(UNKNOWN)";
  }
}

VDSO_INTERFACE_FUNCTION(zx_exception_get_string);

// Generated with:
#if 0
grep '#define' zircon/system/public/zircon/syscalls/exception.h |
grep 'ZX_EXCP[_A-Z]* ' |
sed 's/#define //g' |
sed 's/\s.*//g' |
awk '{print "case "$1": return \""$1"\";";}'
#endif
