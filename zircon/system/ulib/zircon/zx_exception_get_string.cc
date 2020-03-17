// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/exception.h>

#include "private.h"

__EXPORT const char* _zx_exception_get_string(zx_excp_type_t exception) {
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
