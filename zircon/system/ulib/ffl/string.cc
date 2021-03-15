// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ffl/string.h>

#ifndef _KERNEL
#include <ios>
namespace ffl {

namespace internal {
const int kIosModeIndex = std::ios_base::xalloc();
}

std::ostream& operator<<(std::ostream& out, String::Mode mode) {
  // ios parameters default to 0 and Dec should be the default.
  static_assert(String::Dec == 0);
  out.iword(internal::kIosModeIndex) = static_cast<long>(mode);
  return out;
}

}  // namespace ffl
#endif
