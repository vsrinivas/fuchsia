// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef _MAGMA_SYSTEM_DISPLAY_ABI_H_
#define _MAGMA_SYSTEM_DISPLAY_ABI_H_

#include "magma_system_common_defs.h"

#if defined(__cplusplus)
extern "C" {
#endif

struct magma_system_display {
    uint32_t magic_;
};

// Opens a device - triggered by a client action. returns null on failure
struct magma_system_display* magma_system_display_open(uint32_t device_handle);

void magma_system_display_close(struct magma_system_display* display);

// TODO(MA-88) make these arguments less confusing
bool magma_system_display_import_buffer(struct magma_system_display* display, uint32_t token,
                                        uint32_t* handle_out);

// Provides a buffer to be scanned out on the next vblank event.
// |callback| will be called with |data| as its second argument when the buffer
// referred to by |handle| is no longer being displayed and is safe to be
// reused. The first argument to |callback| indicates an error with the page
// flip, where 0 indicates success
void magma_system_display_page_flip(struct magma_system_display* display, uint32_t handle,
                                    magma_system_pageflip_callback_t callback, void* data);

#if defined(__cplusplus)
}
#endif

#endif // _MAGMA_SYSTEM_DISPLAY_ABI_H_