// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "decompress.h"

#include <lib/fitx/result.h>
#include <lib/zbitl/error_stdio.h>
#include <lib/zbitl/view.h>

#include <fbl/function.h>
#include <ktl/byte.h>
#include <ktl/span.h>
#include <ktl/unique_ptr.h>

#include "memory.h"

fitx::result<ktl::string_view, DecompressResult> CopyAndDecompressItem(
    zbitl::View<zbitl::ByteView>& zbi, typename zbitl::View<zbitl::ByteView>::iterator item) {
  struct ScratchAllocator {
    // Perform an allocation.
    fitx::result<ktl::string_view, UniquePtr<void>> operator()(size_t size) const {
      void* result = AllocateMemory(size);
      if (result == nullptr) {
        return fitx::error("could not allocate scratch memory");
      }
      return fitx::ok(AdoptAllocation(result, size));
    }
  };

  // Get (uncompressed) length of the payload.
  uint32_t size = zbitl::UncompressedLength(*(*item).header);

  // Allocate memory for the payload.
  auto* allocation = static_cast<std::byte*>(AllocateMemory(size));
  if (allocation == nullptr) {
    return fitx::error("could not allocate memory for payload");
  }

  // Decompress the item.
  if (auto result = zbi.CopyStorageItem(ktl::span(allocation, size), item, ScratchAllocator{});
      result.is_error()) {
    zbitl::PrintViewCopyError(result.error_value());
    return fitx::error("could not decompress item");
  }

  return fitx::ok(
      DecompressResult{.ptr = ktl::unique_ptr<std::byte, AllocationDeleter>(
                           static_cast<std::byte*>(allocation), AllocationDeleter{size}),
                       .size = size});
}
