// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>

#include "garnet/bin/zxdb/client/register.h"
#include "garnet/bin/zxdb/common/err.h"
#include "garnet/bin/zxdb/console/format_register.h"
#include "garnet/bin/zxdb/console/format_register_arm64.h"
#include "garnet/bin/zxdb/console/format_table.h"
#include "garnet/bin/zxdb/console/output_buffer.h"
#include "garnet/bin/zxdb/console/string_formatters.h"
#include "garnet/lib/debug_ipc/helper/arch_arm64.h"
#include "lib/fxl/strings/string_printf.h"

using debug_ipc::RegisterCategory;
using debug_ipc::RegisterID;

namespace zxdb {

namespace {

#define FLAG_VALUE(value, shift, mask) (uint8_t)((value >> shift) & mask)

TextForegroundColor GetRowColor(size_t table_len) {
  return table_len % 2 == 0 ? TextForegroundColor::kDefault
                            : TextForegroundColor::kLightGray;
}

// Format General Registers
// -----------------------------------------------------

std::vector<OutputBuffer> DescribeCPSR(const Register& cpsr,
                                       TextForegroundColor color) {
  std::vector<OutputBuffer> result;
  result.emplace_back(color, RegisterIDToString(cpsr.id()));

  uint64_t value = cpsr.GetValue();

  // Hex value: rflags is a 32 bit value.
  result.emplace_back(color, fxl::StringPrintf("0x%08" PRIx64, value));

  // Decode individual flags.
  result.emplace_back(color, fxl::StringPrintf("V=%d, C=%d, Z=%d, N=%d",
                                               ARM64_FLAG_VALUE(value, CpsrV),
                                               ARM64_FLAG_VALUE(value, CpsrC),
                                               ARM64_FLAG_VALUE(value, CpsrZ),
                                               ARM64_FLAG_VALUE(value, CpsrN)));
  // TODO(donosoc): Implement system formatting when we enable
  //                ENABLE_ARM64_SYSTEM_FLAGS

  return result;
}

std::vector<OutputBuffer> DescribeCPSRExtended(const Register& cpsr,
                                               TextForegroundColor color) {
  std::vector<OutputBuffer> result;
  result.reserve(3);
  result.emplace_back(OutputBuffer());
  result.emplace_back(OutputBuffer());

  uint64_t value = cpsr.GetValue();

  result.emplace_back(
      color,
      fxl::StringPrintf(
          "EL=%d, F=%d, I=%d, A=%d, D=%d, IL=%d, SS=%d, PAN=%d, UAO=%d",
          ARM64_FLAG_VALUE(value, CpsrEL), ARM64_FLAG_VALUE(value, CpsrF),
          ARM64_FLAG_VALUE(value, CpsrI), ARM64_FLAG_VALUE(value, CpsrA),
          ARM64_FLAG_VALUE(value, CpsrD), ARM64_FLAG_VALUE(value, CpsrIL),
          ARM64_FLAG_VALUE(value, CpsrSS), ARM64_FLAG_VALUE(value, CpsrPAN),
          ARM64_FLAG_VALUE(value, CpsrUAO)));
  return result;
}

void FormatGeneralRegisters(const FormatRegisterOptions& options,
                            const std::vector<Register>& registers,
                            OutputBuffer* out) {
  std::vector<std::vector<OutputBuffer>> rows;

  for (const Register& reg : registers) {
    auto color = GetRowColor(rows.size());
    if (reg.id() == RegisterID::kARMv8_cpsr) {
      rows.push_back(DescribeCPSR(reg, color));
      if (options.extended)
        rows.push_back(DescribeCPSRExtended(reg, color));
    } else {
      rows.push_back(DescribeRegister(reg, color));
    }
  }

  // Output the tables.
  if (!rows.empty()) {
    std::vector<ColSpec> colspecs({ColSpec(Align::kRight),
                                   ColSpec(Align::kRight, 0, std::string(), 1),
                                   ColSpec()});
    FormatTable(colspecs, rows, out);
  }
}

}  // namespace

bool FormatCategoryARM64(const FormatRegisterOptions& options,
                         debug_ipc::RegisterCategory::Type category,
                         const std::vector<Register>& registers,
                         OutputBuffer* out, Err* err) {
  // Only general registers specialized for now.
  if (category != RegisterCategory::Type::kGeneral)
    return false;

  FormatGeneralRegisters(options, registers, out);
  return true;
}

}  // namespace zxdb
