// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_HWSTRESS_HWSTRESS_H_
#define GARNET_BIN_HWSTRESS_HWSTRESS_H_

#include <string_view>

namespace hwstress {

// Run the main binary with the given command line args.
int Run(int argc, const char** argv);

}  // namespace hwstress

#endif  // GARNET_BIN_HWSTRESS_HWSTRESS_H_
