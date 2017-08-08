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

#pragma once

#include <err.h>
#include <lib/user_copy/user_ptr.h>
#include <magenta/compiler.h>
#include <stdint.h>

status_t mtrace_control(uint32_t kind, uint32_t action, uint32_t options,
                        user_ptr<void> arg, uint32_t size);

#ifdef __x86_64__
status_t mtrace_ipt_control(uint32_t action, uint32_t options,
                            user_ptr<void> arg, uint32_t size);
#endif
