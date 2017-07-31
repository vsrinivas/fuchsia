// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/types.h>
#include <stdint.h>

__BEGIN_CDECLS

// Accessors for Magenta-specific state maintained by the language runtime

// Examines the set of handles received at process startup for one matching
// |hnd_info|.  If one is found, atomically returns it and removes it from the
// set available to future calls.
// |hnd_info| is a value returned by PA_HND().
mx_handle_t mx_get_startup_handle(uint32_t hnd_info);

mx_handle_t _mx_thread_self(void);
mx_handle_t mx_thread_self(void);

mx_handle_t _mx_process_self(void);
mx_handle_t mx_process_self(void);

mx_handle_t _mx_vmar_root_self(void);
mx_handle_t mx_vmar_root_self(void);

mx_handle_t _mx_job_default(void);
mx_handle_t mx_job_default(void);

__END_CDECLS
