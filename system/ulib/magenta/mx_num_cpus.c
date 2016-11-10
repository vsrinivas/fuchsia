// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/syscalls.h>

#include <magenta/compiler.h>
#include "private.h"

unsigned int _mx_num_cpus(void) {
    return DATA_CONSTANTS.max_num_cpus;
}

__typeof(mx_num_cpus) mx_num_cpus __attribute__((weak, alias("_mx_num_cpus")));
