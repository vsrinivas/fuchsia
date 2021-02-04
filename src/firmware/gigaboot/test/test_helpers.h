// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_FIRMWARE_GIGABOOT_TEST_TEST_HELPERS_H_
#define SRC_FIRMWARE_GIGABOOT_TEST_TEST_HELPERS_H_

#include <efi/types.h>

// efi_status is defined as size_t, whereas EFI_SUCCESS is literal 0, which
// causes googletest ASSERT/EXPECT macros to complain about sign mismatch.
// Re-define here to use the proper type.
constexpr efi_status kEfiSuccess = EFI_SUCCESS;

#endif  // SRC_FIRMWARE_GIGABOOT_TEST_TEST_HELPERS_H_
