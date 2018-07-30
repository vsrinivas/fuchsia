// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_PROCESS_H_
#define ZIRCON_PROCESS_H_

#include <zircon/types.h>
#include <stdint.h>

__BEGIN_CDECLS

// Accessors for Zircon-specific state maintained by the language runtime

// Examines the set of handles received at process startup for one matching
// |hnd_info|.  If one is found, atomically returns it and removes it from the
// set available to future calls.
// |hnd_info| is a value returned by PA_HND().
zx_handle_t zx_take_startup_handle(uint32_t hnd_info);

zx_handle_t _zx_thread_self(void);
zx_handle_t zx_thread_self(void);

zx_handle_t _zx_process_self(void);
zx_handle_t zx_process_self(void);

zx_handle_t _zx_vmar_root_self(void);
zx_handle_t zx_vmar_root_self(void);

zx_handle_t _zx_job_default(void);
zx_handle_t zx_job_default(void);

__END_CDECLS

#endif // ZIRCON_PROCESS_H_
