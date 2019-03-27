// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>

namespace zxdb {

// Returns the path (dir + file name) of the current executable.
std::string GetSelfPath();

}  // namespace zxdb
