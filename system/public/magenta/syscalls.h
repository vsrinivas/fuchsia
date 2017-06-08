// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/types.h>
#include <magenta/syscalls/types.h>

#include <magenta/syscalls/pci.h>
#include <magenta/syscalls/object.h>

__BEGIN_CDECLS

#include <magenta/syscalls/definitions.h>

// Compatibility Wrappers for Deprecated Syscalls

extern uint32_t _mx_num_cpus(void)
    __attribute__((deprecated("use _mx_system_get_num_cpus instead")));
extern uint32_t mx_num_cpus(void)
    __attribute__((deprecated("use mx_system_get_num_cpus instead")));
extern mx_status_t _mx_version_get(char version[], uint32_t len)
    __attribute__((deprecated("use _mx_system_get_version instead")));
extern mx_status_t mx_version_get(char version[], uint32_t len)
    __attribute__((deprecated("use mx_system_get_version instead")));

__END_CDECLS
