// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

// N.B. This is ideally temporary. It is currently used by Performance Monitor support, and is a
// stopgap until "resources" can be used to read/write x86 MSRs. The intent is to use this interface
// for similar facilities in ARM (assuming we need it - on x86 we need ring 0 to access most of the
// MSRs we need).
// Note on naming: The "m" in "mtrace" means "miscellaneous". "trace" is being used very
// generically, e.g., all the different kinds of h/w based trace and performance data capturing one
// can do.

#ifndef ZIRCON_KERNEL_LIB_MTRACE_INCLUDE_LIB_MTRACE_H_
#define ZIRCON_KERNEL_LIB_MTRACE_INCLUDE_LIB_MTRACE_H_

#include <lib/user_copy/user_ptr.h>
#include <stdint.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

zx_status_t mtrace_control(uint32_t kind, uint32_t action, uint32_t options,
                           user_inout_ptr<void> arg, size_t size);

zx_status_t mtrace_perfmon_control(uint32_t action, uint32_t options, user_inout_ptr<void> arg,
                                   size_t size);

#endif  // ZIRCON_KERNEL_LIB_MTRACE_INCLUDE_LIB_MTRACE_H_
