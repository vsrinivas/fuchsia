// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>

#include "garnet/bin/zxdb/common/err.h"

namespace zxdb {

class Register;

// Format for float, double and long double
// |precision| sets the amount of digits to be written. If 0, the maximum for
// that particular floating type will be used.
Err GetFPString(const Register&, std::string* out, int precision = 0);

}  // namespace zxdb
