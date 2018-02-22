// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>

namespace zxdb {

class Err;

[[nodiscard]] Err StringToUint64(const std::string& s, uint64_t* out);

}  // namespace zxdb
