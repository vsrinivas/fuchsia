// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_PLATFORM_EFI_PRIVATE_H_
#define ZIRCON_KERNEL_PLATFORM_EFI_PRIVATE_H_

#include <lib/fit/function.h>
#include <zircon/types.h>

#include <efi/boot-services.h>
#include <ktl/byte.h>
#include <ktl/span.h>

// This function is only visible here so that it can be tested in efi_test.cc
zx_status_t ForEachMemoryAttributeEntrySafe(
    ktl::span<const ktl::byte> table,
    fit::inline_function<zx_status_t(const efi_memory_descriptor*)> callback);

#endif  // ZIRCON_KERNEL_PLATFORM_EFI_PRIVATE_H_
