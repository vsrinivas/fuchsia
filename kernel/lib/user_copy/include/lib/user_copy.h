// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <arch/user_copy.h>
#include <compiler.h>
#include <err.h>

__BEGIN_CDECLS

#define copy_to_user arch_copy_to_user
#define copy_from_user arch_copy_from_user

/**
 * @brief Copies data from userspace into a newly allocated buffer
 *
 * Upon a successful call, the returned buffer is owned by the caller.
 * When the caller is done with it, it can be released with free().
 *
 * @param dst A pointer to write the allocated buffer into
 * @param src The userspace buffer to copy
 * @param len The length of the userspace buffer
 * @param max_len The maximum allowed length of the userspace buffer
 *
 * @return NO_ERROR on success
 * @return ERR_NO_MEMORY on allocation failure
 * @return ERR_INVALID_ARGS if len > max_len
 * @return See copy_from_user for other possible errors
 */
status_t copy_from_user_dynamic(
        void **dst,
        const void *src,
        size_t len,
        size_t max_len);

// Convenience functions for common data types
#define MAKE_COPY_TO_USER(name, type) \
    static inline status_t name(type *dst, type value) { \
        return copy_to_user(dst, &value, sizeof(type)); \
    }

MAKE_COPY_TO_USER(copy_to_user_u8, uint8_t);
MAKE_COPY_TO_USER(copy_to_user_u16, uint16_t);
MAKE_COPY_TO_USER(copy_to_user_32, int32_t);
MAKE_COPY_TO_USER(copy_to_user_u32, uint32_t);
MAKE_COPY_TO_USER(copy_to_user_u64, uint64_t);
MAKE_COPY_TO_USER(copy_to_user_uptr, uintptr_t);
MAKE_COPY_TO_USER(copy_to_user_iptr, intptr_t);

#undef MAKE_COPY_TO_USER

#define MAKE_COPY_FROM_USER(name, type) \
    static inline status_t name(type *dst, type *src) { \
        return copy_from_user(dst, src, sizeof(type)); \
    }

MAKE_COPY_FROM_USER(copy_from_user_u8, uint8_t);
MAKE_COPY_FROM_USER(copy_from_user_u16, uint16_t);
MAKE_COPY_FROM_USER(copy_from_user_u32, uint32_t);
MAKE_COPY_FROM_USER(copy_from_user_32, int32_t);
MAKE_COPY_FROM_USER(copy_from_user_u64, uint64_t);
MAKE_COPY_FROM_USER(copy_from_user_uptr, uintptr_t);
MAKE_COPY_FROM_USER(copy_from_user_iptr, intptr_t);

#undef MAKE_COPY_FROM_USER

__END_CDECLS
