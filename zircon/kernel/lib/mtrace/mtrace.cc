// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

// N.B. This is ideally temporary. It is used by Intel PT support, and is a
// stopgap until "resources" can be used to read/write x86 MSRs.
// "mtrace" == "zircon trace": the idea being to be a generalization of
// ktrace. It's all temporary, but there may be other uses before the stopgap
// is no longer necessary.

#include "lib/mtrace.h"

#include <lib/zircon-internal/mtrace.h>

zx_status_t mtrace_control(uint32_t kind, uint32_t action, uint32_t options,
                           user_inout_ptr<void> arg, size_t size) {
  switch (kind) {
#if defined(__x86_64__) || defined(__aarch64__)
    case MTRACE_KIND_PERFMON:
      return mtrace_perfmon_control(action, options, arg, size);
#endif
    default:
      return ZX_ERR_INVALID_ARGS;
  }
}
