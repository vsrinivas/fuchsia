// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <map>
#include <string>
#include <vector>

#include "garnet/bin/zxdb/console/output_buffer.h"
#include "garnet/lib/debug_ipc/protocol.h"
#include "garnet/lib/debug_ipc/records.h"

namespace zxdb {

class Err;
class Register;
class RegisterSet;

using FilteredRegisterSet =
    std::map<debug_ipc::RegisterCategory::Type, std::vector<Register>>;

// Filters the available registers to the ones matching the given categories and
// matching the registers.
// Not defining a regexp will let all the registers pass.
Err FilterRegisters(const RegisterSet&, FilteredRegisterSet* out,
                    std::vector<debug_ipc::RegisterCategory::Type> categories,
                    const std::string& search_regexp = std::string());

// Format the output of the FilterRegisters call into a console readable format.
Err FormatRegisters(debug_ipc::Arch, const FilteredRegisterSet&,
                    OutputBuffer* out);

// Formatting helpers ----------------------------------------------------------

// Formats the register and returns a vector with the following information:
//  - name
//  - hex value
//  - comment (may be empty if unapplicable).
std::vector<OutputBuffer> DescribeRegister(const Register& reg,
                                           TextForegroundColor color);

const char* RegisterCategoryTypeToString(debug_ipc::RegisterCategory::Type);

const char* RegisterIDToString(debug_ipc::RegisterID);

}  // namespace zxdb
