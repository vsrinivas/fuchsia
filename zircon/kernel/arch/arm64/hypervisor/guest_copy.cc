// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/user_copy.h>

zx_status_t arch_copy_from_guest(GuestPageTable& gpt, void* dst, const void* src, size_t len) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t arch_copy_to_guest(GuestPageTable& gpt, void* dst, const void* src, size_t len) {
  return ZX_ERR_NOT_SUPPORTED;
}
