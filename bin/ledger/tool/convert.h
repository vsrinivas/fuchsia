// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_TOOL_CONVERT_H_
#define PERIDOT_BIN_LEDGER_TOOL_CONVERT_H_

#include <string>

#include "lib/fxl/strings/string_view.h"

namespace tool {

// Inverse of the transformation currently used by DeviceRunner to translate
// human-readable username to user ID.
bool FromHexString(fxl::StringView hex_string, std::string* result);

}  // namespace tool

#endif  // PERIDOT_BIN_LEDGER_TOOL_CONVERT_H_
