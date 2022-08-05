// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/user_copy/internal.h>
#include <sys/types.h>

#include <arch/vm.h>
#include <fbl/conditional_select_nospec.h>
#include <fbl/confine_array_index.h>

namespace internal {

// Ensure that addresses in the range [vaddr, vaddr+len) are accessible to the user. If any address
// in this range is not accessible to the user, `vaddr` and `len` are set to {0,0}.
//
// So you might wonder why we don't simply implement this function using `is_user_accessible_range`?
// That's because we need to avoid introducing any conditional branches. The purpose of
// `validate_user_accessible_range` is to mitigate Spectre V1 attacks (Bounds Check Bypass) which
// rely on speculative execution of conditional branches.
void validate_user_accessible_range(vaddr_t* vaddr, size_t* len) {
  // Check for overflow. `vaddr` and `len` are set to zero if there is overflow.
  vaddr_t user_addr_end = *vaddr + *len;
  vaddr_t old_vaddr = *vaddr;
  *vaddr = fbl::conditional_select_nospec_lt(user_addr_end, old_vaddr, 0, *vaddr);
  *len = fbl::conditional_select_nospec_lt(user_addr_end, old_vaddr, 0, *len);

#if defined(__aarch64__)

  // On arm64, we must check that no address in the range of [vaddr, vaddr+len) has bit 55 set.

  // Check the lower bound is user accessible.
  vaddr_t user_bit = *vaddr & kUserBitMask;
  *vaddr = fbl::conditional_select_nospec_eq(user_bit, 0, *vaddr, 0);
  *len = fbl::conditional_select_nospec_eq(user_bit, 0, *len, 0);

  // Check the upper bound is user accessible.
  //
  // Note that even if we overflowed above, `vaddr` and `len` will still be zero here.
  // Underflow should only happen if `vaddr` and `len` are both zero. This could happen because
  // those were the original function parameters, or because `vaddr+len` overflowed and we set it to
  // zero above. In the case of an underflow, `vaddr` and `len` will still be zero after this block.
  vaddr_t user_bit_end = (*vaddr + *len - 1) & kUserBitMask;
  *vaddr = fbl::conditional_select_nospec_eq(user_bit_end, 0, *vaddr, 0);
  *len = fbl::conditional_select_nospec_eq(user_bit_end, 0, *len, 0);

  // Cover the corner case where the start and end are accessible
  // (bit 55 == 0), but there could be a value within the range that could have
  // bit 55 == 1. This is for cases like `addr = 0, len = 0x17f'ffff'ffff'ffff` where both `addr`
  // and `addr+len` pass `is_user_accessible` but there's a value between them that fails
  // `is_user_accessible`. In this case, the difference between start and end must be at least 2^55.
  *vaddr = fbl::conditional_select_nospec_lt(*len, kUserBitMask, *vaddr, 0);
  *len = fbl::conditional_select_nospec_lt(*len, kUserBitMask, *len, 0);

#elif defined(__x86_64__)

  // On x86_64, we must check that no address in the range of [vaddr, vaddr+len) has a bit set above
  // the lower half of the canonical address ranges.

  // Note that we only really need to check the upper bound. Even if we overflowed above, `vaddr`
  // and `len` will still be zero here.
  vaddr_t user_bit_end = (*vaddr + *len - 1) & kX86CanonicalAddressMask;
  *vaddr = fbl::conditional_select_nospec_eq(user_bit_end, 0, *vaddr, 0);
  *len = fbl::conditional_select_nospec_eq(user_bit_end, 0, *len, 0);

#endif
}

}  // namespace internal
