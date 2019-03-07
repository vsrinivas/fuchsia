// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>

#include "garnet/bin/zxdb/common/err.h"
#include "garnet/bin/zxdb/symbols/type.h"

namespace zxdb {

// Computes the Symbol type name for the input string. Returns an error on
// failure.
Err StringToType(const std::string& input, fxl::RefPtr<Type>* type);

}  // namespace zxdb
