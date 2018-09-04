// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <map>
#include <string>
#include <vector>

#include "garnet/lib/debug_ipc/records.h"

namespace zxdb {

class Err;
class OutputBuffer;
class Register;
class RegisterSet;

using FilteredRegisterSet = std::map<debug_ipc::RegisterCategory::Type,
                                     std::vector<Register>>;

// Filters the available registers to the ones matching the given categories and
// matching the registers.
// Not defining a regexp will let all the registers pass.
Err FilterRegisters(const RegisterSet&, FilteredRegisterSet* out,
                    std::vector<debug_ipc::RegisterCategory::Type> categories,
                    const std::string& search_regexp = std::string());

// Format the output of the FilterRegisters call into a console readable format.
void FormatRegisters(const FilteredRegisterSet&, OutputBuffer* out);

// Formatting helpers ----------------------------------------------------------

std::string RegisterCategoryTypeToString(debug_ipc::RegisterCategory::Type);

const char* RegisterIDToString(debug_ipc::RegisterID);

Err FormaterRegisterValue(const Register&, std::string* out);

}  // namespace zxdb
