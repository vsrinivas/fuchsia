// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>

#include "garnet/bin/zxdb/client/err.h"

namespace zxdb {

struct BreakpointSettings;

// Reads the location string and fills in the BreakpointSettings co
Err ParseBreakpointLocation(const std::string& input,
                            BreakpointSettings* settings);

}  // namespace zxdb
