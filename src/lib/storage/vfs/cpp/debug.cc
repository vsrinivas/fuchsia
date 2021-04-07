// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/storage/vfs/cpp/debug.h"

#include <iostream>
#include <string_view>

namespace fs::debug_internal {

void Log(std::string_view buffer) { std::cerr << buffer << std::endl; }

}  // namespace fs::debug_internal
