// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_INCLUDE_ARCH_USER_COPY_H_
#define ZIRCON_KERNEL_INCLUDE_ARCH_USER_COPY_H_

#include <err.h>
#include <sys/types.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

/*
 * @brief Copy data from userspace into kernelspace
 *
 * This function validates that usermode has access to src before copying the
 * data.
 *
 * @param dst The destination buffer.
 * @param src The source buffer.
 * @param len The number of bytes to copy.
 *
 * @return ZX_OK on success
 */
zx_status_t arch_copy_from_user(void *dst, const void *src, size_t len);

/*
 * @brief Copy data from userspace into kernelspace
 *
 * This function validates that usermode has access to src before copying the
 * data. Unlike arch_copy_from_user it will not fault in memory, and if any
 * fault occurs it will be provided in the output parameter.
 *
 * @param dst The destination buffer.
 * @param src The source buffer.
 * @param len The number of bytes to copy.
 * @param pf_va Virtual address of any fault that occurs, undefined on success.
 * @param pf_flags Flag information of any fault that occurs, undefined on success.
 *
 * @return ZX_OK on success
 */
zx_status_t arch_copy_from_user_capture_faults(void *dst, const void *src, size_t len,
                                               vaddr_t *pf_va, uint *pf_flags);
/*
 * @brief Copy data from kernelspace into userspace
 *
 * This function validates that usermode has access to dst before copying the
 * data.
 *
 * @param dst The destination buffer.
 * @param src The source buffer.
 * @param len The number of bytes to copy.
 *
 * @return ZX_OK on success
 */
zx_status_t arch_copy_to_user(void *dst, const void *src, size_t len);

/*
 * @brief Copy data from kernelspace into userspace
 *
 * This function validates that usermode has access to dst before copying the
 * data. Unlike arch_copy_from_user it will not fault in memory, and if any
 * fault occurs it will be provided in the output parameter.
 *
 * @param dst The destination buffer.
 * @param src The source buffer.
 * @param len The number of bytes to copy.
 * @param pf_va Virtual address of any fault that occurs, undefined on success.
 * @param pf_flags Flag information of any fault that occurs, undefined on success.
 *
 * @return ZX_OK on success
 */
zx_status_t arch_copy_to_user_capture_faults(void *dst, const void *src, size_t len, vaddr_t *pf_va,
                                             uint *pf_flags);

__END_CDECLS

#endif  // ZIRCON_KERNEL_INCLUDE_ARCH_USER_COPY_H_
