// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/public/lib/fostr/indent.h"

namespace fostr {
namespace internal {

constexpr long kDefaultIndentBy = 4;

// static
long& IndentLevel::Value(std::ostream& os) {
  static int index = std::ios_base::xalloc();
  return os.iword(index);
}

// static
long& IndentBy::Value(std::ostream& os) {
  static int index = std::ios_base::xalloc();

  long& stored = os.iword(index);
  if (stored == 0) {
    stored = kDefaultIndentBy;
  }

  return stored;
}

}  // namespace internal
}  // namespace fostr
