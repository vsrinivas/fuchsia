// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

// This header file has been generated from the strings library fuchsia.intl.l10n.
#include "fuchsia/intl/l10n/cpp/fidl.h"

// Each library name segment between dots gets its own nested namespace in
// the generated C++ code.
using fuchsia::intl::l10n::MessageIds;

int main() {
  std::cout << "Constant: " << static_cast<uint64_t>(MessageIds::STRING_NAME) << std::endl;
  return 0;
}
