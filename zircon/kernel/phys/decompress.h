// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Functionality for decompressing and copying ZBI payloads into new memory allocations.

#ifndef ZIRCON_KERNEL_PHYS_DECOMPRESS_H_
#define ZIRCON_KERNEL_PHYS_DECOMPRESS_H_

#include <lib/fitx/result.h>
#include <lib/zbitl/view.h>

#include <fbl/function.h>
#include <ktl/byte.h>
#include <ktl/span.h>
#include <ktl/string_view.h>
#include <ktl/unique_ptr.h>

#include "memory.h"

// Result of CopyAndDecompressItem.
struct DecompressResult {
  UniquePtr<std::byte> ptr;
  size_t size;
};

// Copy the given ZBI item into newly allocated memory, decompressing if required.
fitx::result<ktl::string_view, DecompressResult> CopyAndDecompressItem(
    zbitl::View<zbitl::ByteView>& zbi, typename zbitl::View<zbitl::ByteView>::iterator item);

#endif  // ZIRCON_KERNEL_PHYS_DECOMPRESS_H_
