// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/processargs.h>
#include <system/compiler.h>

__BEGIN_CDECLS

#pragma GCC visibility push(hidden)

// Parse the argument of _start() and setup the global
// proc info structure.  Return a pointer to the same.
mx_proc_info_t* mxr_process_parse_args(void* arg);

// Obtain the global proc info structure.
mx_proc_info_t* mxr_process_get_info(void);

// Obtain a handle from proc args, if such a handle exists.
mx_handle_t mxr_process_get_handle(uint32_t id);

#pragma GCC visibility pop

__END_CDECLS
