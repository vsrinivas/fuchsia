// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#define MEMMOV_OPS_DST_OFFSET (0)
#define MEMMOV_OPS_SRC_OFFSET (8)
#define MEMMOV_OPS_LEN_OFFSET (16)

#ifndef ASSEMBLY

#include <magenta/compiler.h>
#include <magenta/types.h>
#include <stddef.h>
#include <stdint.h>

__BEGIN_CDECLS

// Warning: The geometry of this struct is depended upon by the mexec assembly
//          function. Do not modify without also updating mexec.S.
typedef struct __PACKED {
    void* dst;
    void* src;
    size_t len;
} memmov_ops_t;

// Implemented in assembly. Copies the new kernel into place and branches to it.
typedef void (*mexec_asm_func)(uint64_t arg0, uint64_t arg1, uint64_t arg2,
                               uint64_t arg3, memmov_ops_t* ops,
                               void* new_kernel_addr);

// Appends a section to the end of a given bootdata image.
mx_status_t bootdata_append_section(uint8_t* bootdata_buf, const size_t buflen,
                                    const uint8_t* section, const uint32_t section_length,
                                    const uint32_t type, const uint32_t extra,
                                    const uint32_t flags);

/* Allow the platform to patch the bootdata structure with any platform specific
 * data that might be necessary for the kernel that mexec is chain-loading.
 */
mx_status_t platform_mexec_patch_bootdata(uint8_t* bootdata, const size_t len);

/* Ask the platform to mexec into the next kernel.
 */
void platform_mexec(mexec_asm_func mexec_assembly, memmov_ops_t* ops,
                    uintptr_t new_bootimage_addr, size_t new_bootimage_len);

static_assert(__offsetof(memmov_ops_t, dst) == MEMMOV_OPS_DST_OFFSET, "");
static_assert(__offsetof(memmov_ops_t, src) == MEMMOV_OPS_SRC_OFFSET, "");
static_assert(__offsetof(memmov_ops_t, len) == MEMMOV_OPS_LEN_OFFSET, "");

__END_CDECLS

#endif // ASSEMBLY
