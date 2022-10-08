// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/ld/abi.h>
#include <lib/stdcompat/span.h>

#include <cassert>
#include <cstddef>

namespace ld {

constexpr void* StaticTlsGetAddr(const TlsGetAddrGot& got, cpp20::span<const uintptr_t> offsets,
                                 void* tp) {
  assert(got.tls_mod_id > 0);
  const uintptr_t tp_offset = offsets[got.tls_mod_id - 1] + got.offset;
  return static_cast<std::byte*>(tp) + tp_offset;
}

}  // namespace ld
