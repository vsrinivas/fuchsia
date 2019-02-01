// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_UTIL_DEBUG_PRINT_H_
#define LIB_ESCHER_UTIL_DEBUG_PRINT_H_

#include <ostream>

namespace escher {

// Declare here, and implement elsewhere.
#define ESCHER_DEBUG_PRINTABLE(X) \
  std::ostream& operator<<(std::ostream& str, const X&);

}  // namespace escher

#endif  // LIB_ESCHER_UTIL_DEBUG_PRINT_H_
