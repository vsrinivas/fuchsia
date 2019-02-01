// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_FAR_ALIGNMENT_H_
#define GARNET_LIB_FAR_ALIGNMENT_H_

#include <cstdint>

namespace archive {

constexpr inline uint64_t AlignToPage(uint64_t offset) {
  return (offset + 4095u) & ~4095ull;
}

constexpr inline uint64_t AlignTo8ByteBoundary(uint64_t offset) {
  return (offset + 7u) & ~7ull;
}

}  // namespace archive

#endif  // GARNET_LIB_FAR_ALIGNMENT_H_
