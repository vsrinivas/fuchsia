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

#include <ktl/optional.h>

// A small helper struct used for the return type of the various
// *_capture_faults forms of the copy_(to|from)_user routines.
//
// A user copy which captures faults has three different possible results.
//
// 1) The operation succeeds.  The status is OK.
// 2) The operation attempts to copy, but page faults in the process.  The
//    status is != ZX_OK, and the fault_info optional has a valid value which
//    contains the virtual address describing the location of the fault and some
//    flags which describe the nature of the fault.
// 3) The operation fails without ever trying.  The status is != ZX_OK, but
//    fault_info has no valid value.  There was no fault taken, so there is no
//    fault to handle.
//
struct UserCopyCaptureFaultsResult {
  struct FaultInfo {
    FaultInfo(vaddr_t pf_va, uint pf_flags) : pf_va(pf_va), pf_flags(pf_flags) {}
    vaddr_t pf_va;
    uint pf_flags;
  };

  explicit UserCopyCaptureFaultsResult(zx_status_t status) : status(status) {}
  UserCopyCaptureFaultsResult(zx_status_t status, FaultInfo fault_info)
      : status(status), fault_info(fault_info) {}

  zx_status_t status;
  ktl::optional<FaultInfo> fault_info;
};

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
[[nodiscard]] UserCopyCaptureFaultsResult arch_copy_from_user_capture_faults(void *dst,
                                                                             const void *src,
                                                                             size_t len);
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
[[nodiscard]] UserCopyCaptureFaultsResult arch_copy_to_user_capture_faults(void *dst,
                                                                           const void *src,
                                                                           size_t len);

#endif  // ZIRCON_KERNEL_INCLUDE_ARCH_USER_COPY_H_
