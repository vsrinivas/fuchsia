// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <fbl/conditional_select_nospec.h>
#include <fbl/confine_array_index.h>
#include <sys/types.h>

namespace internal {

// Confines vaddr, len to [0, top]; if [|vaddr|, |vaddr + len|] are above top, return {0,0}
// Does so without any conditional branches, avoiding Spectre V1 attacks.
//
// Confines both vaddr and len by following the sequence:
//   vaddr_lo = vaddr <= top ? vaddr : 0
//   vaddr_hi = vaddr + len <= top : vaddr + len : 0
//   *vaddr = (len == vaddr_hi - vaddr_lo) ? vaddr_lo : 0
//   *len = (len == vaddr_hi - vaddr_lo) ? len : 0
void confine_user_address_range(vaddr_t* vaddr, size_t* len, const uintptr_t top) {
  const size_t vaddr_lo = fbl::confine_array_index(*vaddr, top + 1);
  const size_t vaddr_hi = fbl::confine_array_index(*vaddr + *len, top + 1);
  *vaddr = fbl::conditional_select_nospec_eq(*len, vaddr_hi - vaddr_lo, vaddr_lo, 0);
  *len = fbl::conditional_select_nospec_eq(*len, vaddr_hi - vaddr_lo, *len, 0);
}

}  // namespace internal
