// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

// This file is used both in the kernel and in the vDSO implementation.
// So it must be compatible with both C and C++, and with both the
// kernel and userland header environments.  It must use only the basic
// types so that struct layouts match exactly in both contexts.

#include <stddef.h>
#include <stdint.h>

// This struct contains constants that are initialized by the kernel
// once at boot time.  From the vDSO code's perspective, they are
// read-only data that can never change.  Hence, no synchronization is
// required to read them.
struct vdso_constants {

    // Maximum number of CPUs that might be online during the lifetime
    // of the booted system.
    uint32_t max_num_cpus;

};
