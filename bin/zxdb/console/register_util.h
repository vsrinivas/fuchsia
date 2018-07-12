// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>
#include <vector>

#include "garnet/lib/debug_ipc/protocol.h"

namespace zxdb {

class OutputBuffer;
class Err;

// Outputs the register information received from the debug agent.
// |searched_register| is the name of a register we want to look at
// individually. If found, it will only output information for that register
// (and its category), or err otherwise.
Err FormatRegisters(const std::vector<debug_ipc::RegisterCategory>& reg_cats,
                    const std::string& searched_register,
                    OutputBuffer *out);

std::string RegisterCategoryTypeToString(debug_ipc::RegisterCategory::Type type);

}   // namespace zxdb
