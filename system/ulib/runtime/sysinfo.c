// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/syscalls.h>
#include <runtime/sysinfo.h>

int mxr_get_nprocs_conf(void) {
    return _mx_num_cpus();
}

int mxr_get_nprocs(void) {
    return _mx_num_cpus();
}
