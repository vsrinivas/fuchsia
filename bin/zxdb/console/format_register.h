// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>
#include <vector>

#include "garnet/lib/debug_ipc/records.h"

namespace zxdb {

class Err;
class OutputBuffer;
class RegisterSet;

// Outputs the register information received from the debug agent.
// |search_regexp| is to limit which register to show. It will only output
// information for registers that matches.
//
// You can define the types of categories to print using the vector at the end.
// By default only prints the general registers. Empty means every category.
Err FormatRegisters(
    const RegisterSet&, const std::string& search_regexp, OutputBuffer* out,
    std::vector<debug_ipc::RegisterCategory::Type> categories = {
        debug_ipc::RegisterCategory::Type::kGeneral});

// Formatting helpers ----------------------------------------------------------

std::string RegisterCategoryTypeToString(debug_ipc::RegisterCategory::Type);

const char* RegisterIDToString(debug_ipc::RegisterID);

}  // namespace zxdb
