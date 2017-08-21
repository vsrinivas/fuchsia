// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

// N.B. This is ideally temporary. It is used by Intel PT support, and is a
// stopgap until "resources" can be used to read/write x86 MSRs.
// "mtrace" == "magenta trace": the idea being to be a generalization of
// ktrace. It's all temporary, but there may be other uses before the stopgap
// is no longer necessary.

#include "lib/mtrace.h"

#include <magenta/mtrace.h>

#ifdef __x86_64__
#include "arch/x86/proc_trace.h"
#endif

mx_status_t mtrace_control(uint32_t kind, uint32_t action, uint32_t options,
                           user_ptr<void> arg, uint32_t size) {
    switch (kind) {
#ifdef __x86_64__
    case MTRACE_KIND_IPT:
        return mtrace_ipt_control(action, options, arg, size);
#endif
    default:
        return MX_ERR_INVALID_ARGS;
    }
}
