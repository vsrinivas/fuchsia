// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/user_copy/internal.h>

#include <arch/user_copy.h>
#include <kernel/thread.h>
#include <vm/vm.h>

zx_status_t arch_copy_from_user(void* dst, const void* src, size_t len) {
  return ZX_OK;
}

zx_status_t arch_copy_to_user(void* dst, const void* src, size_t len) {
  return ZX_OK;
}

UserCopyCaptureFaultsResult arch_copy_from_user_capture_faults(void* dst, const void* src,
                                                               size_t len) {
  return UserCopyCaptureFaultsResult{ZX_OK};
}

UserCopyCaptureFaultsResult arch_copy_to_user_capture_faults(void* dst, const void* src,
                                                             size_t len) {
  return UserCopyCaptureFaultsResult{ZX_OK};
}
