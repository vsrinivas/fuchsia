// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

// N.B. This is ideally temporary. It is currently used by Intel PT and PM
// support, and is a stopgap until "resources" can be used to read/write
// x86 MSRs. The intent is to use this interface for similar facilities in
// ARM (assuming we need it - on x86 we need ring 0 to access most of the
// MSRs we need).
// Note on naming: The "m" in "mtrace" means "miscellaneous". "trace" is being
// used very generically, e.g., all the different kinds of h/w based trace
// and performance data capturing one can do.

#pragma once

#include <lib/user_copy/user_ptr.h>
#include <stdint.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

zx_status_t mtrace_control(uint32_t kind, uint32_t action, uint32_t options,
                           user_inout_ptr<void> arg, uint32_t size);

#ifdef __x86_64__
zx_status_t mtrace_ipt_control(uint32_t action, uint32_t options,
                               user_inout_ptr<void> arg, uint32_t size);
zx_status_t mtrace_ipm_control(uint32_t action, uint32_t options,
                               user_inout_ptr<void> arg, uint32_t size);
#endif
