// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ostream>

namespace escher {

#if defined(NDEBUG)

// Define as no-op.
#define ESCHER_DEBUG_PRINTABLE(X) \
  inline std::ostream& operator<<(std::ostream& str, const X&) { return str; }

#else

// Declare here, and implement elsewhere.
#define ESCHER_DEBUG_PRINTABLE(X) \
  std::ostream& operator<<(std::ostream& str, const X&);

#endif

}  // namespace escher
