// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/format_register.h"

#include <inttypes.h>
#include <stdlib.h>

#include <map>

#include "src/developer/debug/shared/regex.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/format_register_arm64.h"
#include "src/developer/debug/zxdb/console/format_register_x64.h"
#include "src/developer/debug/zxdb/console/format_table.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/console/string_formatters.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

using debug_ipc::Register;
using debug_ipc::RegisterCategory;
using debug_ipc::RegisterID;

namespace {

void InternalFormatGeneric(const std::vector<Register>& registers, OutputBuffer* out) {
  std::vector<std::vector<OutputBuffer>> rows;
  for (const Register& reg : registers) {
    auto color =
        rows.size() % 2 == 1 ? TextForegroundColor::kDefault : TextForegroundColor::kLightGray;
    rows.push_back(DescribeRegister(reg, color));
  }

  // Pad left by two spaces so the headings make more sense.
  FormatTable({ColSpec(Align::kRight, 0, std::string(), 2), ColSpec(Align::kRight), ColSpec()},
              rows, out);
}

void FormatCategory(const FormatRegisterOptions& options, RegisterCategory category,
                    const std::vector<Register>& registers, OutputBuffer* out) {
  auto title = fxl::StringPrintf("%s Registers\n", debug_ipc::RegisterCategoryToString(category));
  out->Append(OutputBuffer(Syntax::kHeading, std::move(title)));

  if (registers.empty()) {
    out->Append("No registers to show in this category.");
    return;
  }

  // Check for architecture-specific printing.
  if (options.arch == debug_ipc::Arch::kX64) {
    if (FormatCategoryX64(options, category, registers, out))
      return;
  } else if (options.arch == debug_ipc::Arch::kArm64) {
    if (FormatCategoryARM64(options, category, registers, out))
      return;
  }

  // General formatting.
  InternalFormatGeneric(registers, out);
}

}  // namespace

std::vector<Register> FilterRegisters(const FormatRegisterOptions& options,
                                      const std::vector<Register>& registers) {
  std::vector<Register> result;

  for (const Register& reg : registers) {
    RegisterCategory category = RegisterIDToCategory(reg.id);
    if (std::find(options.categories.begin(), options.categories.end(), category) ==
        options.categories.end())
      continue;  // Register filtered out by category.

    if (options.filter_regex.valid()) {
      // Filter by regex.
      if (options.filter_regex.Match(RegisterIDToString(reg.id)))
        result.push_back(reg);
    } else {
      // Unconditional addition.
      result.push_back(reg);
    }
  }

  return result;
}

OutputBuffer FormatRegisters(const FormatRegisterOptions& options,
                             const std::vector<Register>& registers) {
  OutputBuffer out;

  // Group register by category.
  std::map<RegisterCategory, std::vector<Register>> categorized;
  for (const Register& reg : registers)
    categorized[RegisterIDToCategory(reg.id)].push_back(reg);

  for (const auto& [category, cat_regs] : categorized) {
    FormatCategory(options, category, cat_regs, &out);
    out.Append("\n");
  }
  return out;
}

std::vector<OutputBuffer> DescribeRegister(const Register& reg, TextForegroundColor color) {
  std::vector<OutputBuffer> result;
  result.emplace_back(RegisterIDToString(reg.id), color);

  if (reg.data.size() <= 8) {
    // Treat <= 64 bit registers as numbers.
    uint64_t value = static_cast<uint64_t>(reg.GetValue());
    result.emplace_back(fxl::StringPrintf("0x%" PRIx64, value), color);

    // For plausible small integers, show the decimal value also. This size check is intended to
    // avoid cluttering up the results with large numbers corresponding to pointers.
    constexpr uint64_t kMaxSmallMagnitude = 0xffff;
    if (value <= kMaxSmallMagnitude || llabs(static_cast<long long int>(value)) <=
                                           static_cast<long long int>(kMaxSmallMagnitude)) {
      result.emplace_back(fxl::StringPrintf("= %d", static_cast<int>(value)), color);
    } else {
      result.emplace_back();
    }
  } else {
    // Assume anything bigger than 64 bits is a vector and print with grouping.
    result.emplace_back(GetLittleEndianHexOutput(reg.data));
  }

  return result;
}

}  // namespace zxdb
