// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/syscalls.h>

#include <magenta/compiler.h>
#include "private.h"

uint32_t _mx_system_get_num_cpus(void) {
    return DATA_CONSTANTS.max_num_cpus;
}

VDSO_INTERFACE_FUNCTION(mx_system_get_num_cpus);

// Deprecated compatibility aliases.
decltype(mx_system_get_num_cpus) _mx_num_cpus
    __WEAK_ALIAS("_mx_system_get_num_cpus");
decltype(mx_system_get_num_cpus) mx_num_cpus
    __WEAK_ALIAS("_mx_system_get_num_cpus");
