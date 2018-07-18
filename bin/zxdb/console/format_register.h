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

std::string RegisterCategoryTypeToString(debug_ipc::RegisterCategory::Type);

// Outputs the register information received from the debug agent.
// |searched_register| is the name of a register we want to look at
// individually. If found, it will only output information for that register
// (and its category), or err otherwise.
//
// You can define the types of categories to print using the vector at the end.
// By default only prints the general registers. Empty means every category.
Err FormatRegisters(
    const RegisterSet&, const std::string& searched_register, OutputBuffer* out,
    std::vector<debug_ipc::RegisterCategory::Type> categories = {
        debug_ipc::RegisterCategory::Type::kGeneral});

std::string RegisterIDToString(debug_ipc::RegisterID);

}   // namespace zxdb
