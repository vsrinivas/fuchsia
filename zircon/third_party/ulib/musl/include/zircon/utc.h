// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SYSROOT_ZIRCON_UTC_H_
#define SYSROOT_ZIRCON_UTC_H_

#include <zircon/types.h>

__BEGIN_CDECLS

// Accessors for the Zircon-specific UTC clock reference maintained by the
// language runtime

// zx_utc_reference_get
//
// Returns a handle to the currently assigned UTC clock, or ZX_HANDLE_INVALID if
// no such clock currently exists.  The handle returned has borrow semantics,
// and should never be directly closed by the user.  If a program wishes to take
// ownership of the clock, it should do so using |zx_utc_reference_swap|.
//
// Thread safety is the responsibility of the user.  In particular, if a clock
// is fetched by a user using |zx_utc_reference_get|, but then the clock is
// swapped out using |zx_utc_reference_swap| and the original clock is closed,
// then the initial clock handle returned is now invalid and could result in a
// use-after-close situation.  It is the user's responsibility to avoid these
// situations.
//
zx_handle_t _zx_utc_reference_get(void);
zx_handle_t zx_utc_reference_get(void);

// zx_utc_reference_swap
//
// Atomically swap the clock handle provided with the current UTC reference.
//
// Upon success, the runtime takes ownership of the handle provided by
// |new_utc_reference| and returns the previous clock handle to the caller via
// |prev_utc_reference_out|, or ZX_HANDLE_INVALID if no clock was currently
// assigned.  The clock returned via swap is now owned by the caller.
//
// In the case of failure, the handle passed in by new_utc_reference will be
// consumed, and the clock held by the runtime will remain unchanged.
//
// Clock handles provided to libc via zx_utc_reference_swap must have read rights
// or they will be rejected.
//
// Parameters:
// new_utc_reference      : A handle to the clock to install as the UTC reference.
//                          Ownership of this handle will _always_ be consumed
//                          by zx_utc_reference_swap.
// prev_utc_reference_out : Either the handle to the previous UTC clock (success
//                          case), or ZX_HANDLE_INVALID in the case of failure.
//                          If a valid handle is returned in this out parameter,
//                          it is _always_ owned by the caller after the call.
//                          It is illegal to pass NULL for this parameter.
//
//
// Return Values:
// If a new clock reference is being provided, the return value of
// |zx_utc_reference_swap| will be the result of a |zx_clock_read| call made to
// verify the clock.  If ZX_HANDLE_INVALID was passed in order to uninstall a
// reference clock, the function cannot fail and will always return ZX_OK.
//
zx_status_t _zx_utc_reference_swap(zx_handle_t new_utc_reference,
                                   zx_handle_t* prev_utc_reference_out) __NONNULL((2));
zx_status_t zx_utc_reference_swap(zx_handle_t new_utc_reference,
                                  zx_handle_t* prev_utc_reference_out) __NONNULL((2));

__END_CDECLS

#endif  // SYSROOT_ZIRCON_UTC_H_
