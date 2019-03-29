// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <map>
#include <string>
#include <vector>

#include "src/developer/debug/ipc/protocol.h"
#include "src/developer/debug/ipc/records.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"

namespace zxdb {

class Err;
class Register;
class RegisterSet;

// Struct meant to configure how the FilterRegister/FormatRegister calls will
// behave.
struct FormatRegisterOptions {
  // What arch this FormatRegisters call belongs to.
  debug_ipc::Arch arch = debug_ipc::Arch::kUnknown;

  // The categories to filter within the FilterRegister step.
  std::vector<debug_ipc::RegisterCategory::Type> categories;

  // Regexp used to filter what registers to show. Empty means no filter.
  std::string filter_regexp;

  // Whether to print extra information about the registers.
  bool extended = false;
};

using FilteredRegisterSet =
    std::map<debug_ipc::RegisterCategory::Type, std::vector<Register>>;

// Filters the available registers to the ones matching the given categories and
// matching the registers.
// Not defining a regexp will let all the registers pass.
Err FilterRegisters(const FormatRegisterOptions&, const RegisterSet&,
                    FilteredRegisterSet* out);

// Format the output of the FilterRegisters call into a console readable format.
Err FormatRegisters(const FormatRegisterOptions&, const FilteredRegisterSet&,
                    OutputBuffer* out);

// Formatting helpers ----------------------------------------------------------

// Formats the register and returns a vector with the following information:
//  - name
//  - hex value
//  - comment (may be empty if inapplicable).
std::vector<OutputBuffer> DescribeRegister(const Register& reg,
                                           TextForegroundColor color);

}  // namespace zxdb
