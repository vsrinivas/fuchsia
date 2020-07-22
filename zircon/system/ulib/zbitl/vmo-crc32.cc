// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/cksum.h>
#include <lib/zbitl/vmo.h>

// The Crc32 method goes into a separate translation unit that need not be
// linked in if it's not used.  Callers not using Crc32 checking don't need to
// link in the allocator or checksum code at all.

namespace zbitl {
namespace {

constexpr size_t kBufferSize = 8192;

}  // namespace

fitx::result<zx_status_t, uint32_t> StorageTraits<zx::vmo>::Crc32(const zx::vmo& vmo,
                                                                  uint32_t offset,
                                                                  uint32_t length) {
  // This always copies, when mapping might be better for large sizes.  But
  // address space is cheap, so users concerned with large sizes should just
  // map the whole ZBI in and use View<std::span> instead.
  auto size = [&]() { return std::min(static_cast<uint32_t>(kBufferSize), length); };
  auto buf = std::make_unique<uint8_t[]>(size());

  uint32_t crc = 0;
  while (length > 0) {
    const uint32_t n = size();
    zx_status_t status = vmo.read(buf.get(), offset, n);
    if (status != ZX_OK) {
      return fitx::error{status};
    }
    crc = crc32(crc, buf.get(), n);
    offset += n;
    length -= n;
  }

  return fitx::ok(crc);
}

}  // namespace zbitl
