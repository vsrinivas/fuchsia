// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_FORMAT_REGISTER_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_FORMAT_REGISTER_H_

#include <map>
#include <string>
#include <vector>

#include "src/developer/debug/shared/arch.h"
#include "src/developer/debug/shared/register_value.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/expr/vector_register_format.h"

namespace zxdb {

// Struct meant to configure how the FilterRegister/FormatRegister calls will
// behave.
struct FormatRegisterOptions {
  // What arch this FormatRegisters call belongs to.
  debug::Arch arch = debug::Arch::kUnknown;

  // Whether to print extra information about the registers.
  bool extended = false;

  // VectorRegisterFormat vector_format = VectorRegisterFormat::kUnsigned8;
  VectorRegisterFormat vector_format = VectorRegisterFormat::kFloat;
};

// Format the given registers into a console readable format.
OutputBuffer FormatRegisters(const FormatRegisterOptions& options,
                             const std::vector<debug::RegisterValue>& registers);

// Formats the given registers as platform-independent values/vectors.
void FormatGeneralRegisters(const std::vector<debug::RegisterValue>& registers, OutputBuffer* out);
void FormatGeneralVectorRegisters(const FormatRegisterOptions& options,
                                  const std::vector<debug::RegisterValue>& registers,
                                  OutputBuffer* out);

// Formats the register and returns a vector with the following information:
//  - name
//  - hex value
//  - comment (may be empty if inapplicable).
std::vector<OutputBuffer> DescribeRegister(const debug::RegisterValue& reg,
                                           TextForegroundColor color);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_FORMAT_REGISTER_H_
