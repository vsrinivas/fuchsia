// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_FORMAT_REGISTER_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_FORMAT_REGISTER_H_

#include <map>
#include <string>
#include <vector>

#include "src/developer/debug/ipc/protocol.h"
#include "src/developer/debug/ipc/records.h"
#include "src/developer/debug/shared/regex.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"

namespace debug_ipc {
struct Register;
}

namespace zxdb {

// Struct meant to configure how the FilterRegister/FormatRegister calls will
// behave.
struct FormatRegisterOptions {
  // What arch this FormatRegisters call belongs to.
  debug_ipc::Arch arch = debug_ipc::Arch::kUnknown;

  // The categories to filter within the FilterRegister step. Applied by FilterRegisters();
  std::vector<debug_ipc::RegisterCategory> categories;

  // Regex used to filter what registers to show. !valid() means no filter. Applied by
  // FilterRegisters();
  debug_ipc::Regex filter_regex;

  // Whether to print extra information about the registers.
  bool extended = false;
};

// Filters the available registers to the ones matching the given categories and matching the
// registers. Not defining a regexp will let all the registers pass.
std::vector<debug_ipc::Register> FilterRegisters(const FormatRegisterOptions& options,
                                                 const std::vector<debug_ipc::Register>& registers);

// Format the output of the FilterRegisters call into a console readable format.
OutputBuffer FormatRegisters(const FormatRegisterOptions& options,
                             const std::vector<debug_ipc::Register>& registers);

// Formats the register and returns a vector with the following information:
//  - name
//  - hex value
//  - comment (may be empty if inapplicable).
std::vector<OutputBuffer> DescribeRegister(const debug_ipc::Register& reg,
                                           TextForegroundColor color);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_FORMAT_REGISTER_H_
