// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>

#include "garnet/bin/zxdb/client/err.h"

namespace zxdb {

struct BreakpointSettings;
class Frame;

// Reads the location string and fills in the BreakpointSettings. The frame
// is used for implied file names based on the current frame, and can be null
// if there is no current frame.
Err ParseBreakpointLocation(const Frame* frame, const std::string& input,
                            BreakpointSettings* settings);

}  // namespace zxdb
