// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_ZIRCON_BIN_HWSTRESS_HWSTRESS_H_
#define SRC_ZIRCON_BIN_HWSTRESS_HWSTRESS_H_

#include <string_view>

namespace hwstress {

// Run the main binary with the given command line args.
int Run(int argc, const char** argv);

}  // namespace hwstress

#endif  // SRC_ZIRCON_BIN_HWSTRESS_HWSTRESS_H_
