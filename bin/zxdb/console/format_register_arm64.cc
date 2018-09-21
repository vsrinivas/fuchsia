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

#ifdef ENABLE_ARM64_SYSTEM_FLAGS

constexpr int kCpsrExceptionLevelShift = 0;
constexpr int kCpsrFShift = 6;     // FIQ mask bit.
constexpr int kCpsrIShift = 7;     // IRQ mask bit.
constexpr int kCpsrAShift = 8;     // SError mask bit.
constexpr int kCpsrDShift = 9;     // Debug exception mask bit.
constexpr int kCpsrILShift = 20;   // Illegal Execution bit.
constexpr int kCpsrSSShift = 21;   // Single Step.
constexpr int kCpsrPANShift = 22;  // Privilege Access Never. (System).
constexpr int kCpsrUAOShift = 23;  // Load/Store privilege access. (System).

#endif

constexpr int kCpsrVShift = 28;  // Overflow bit.
constexpr int kCpsrCShift = 29;  // Carry bit.
constexpr int kCpsrZShift = 30;  // Zero bit.
constexpr int kCpsrNShift = 31;  // Negative bit.

std::vector<OutputBuffer> DescribeCPSR(const Register& cpsr,
                                       TextForegroundColor color) {
  std::vector<OutputBuffer> result;
  result.emplace_back(color, RegisterIDToString(cpsr.id()));

  uint64_t value = cpsr.GetValue();

  // Hex value: rflags is a 32 bit value.
  result.emplace_back(color, fxl::StringPrintf("0x%08" PRIx64, value));

  // Decode individual flags.
  result.emplace_back(color,
                      fxl::StringPrintf("V=%d, C=%d, Z=%d, N=%d",
                                        FLAG_VALUE(value, kCpsrVShift, 0x1),
                                        FLAG_VALUE(value, kCpsrCShift, 0x1),
                                        FLAG_VALUE(value, kCpsrZShift, 0x1),
                                        FLAG_VALUE(value, kCpsrNShift, 0x1)));

  // TODO(donosoc): Implement system formatting when we enable
  //                ENABLE_ARM64_SYSTEM_FLAGS

  return result;
}

void FormatGeneralRegisters(const std::vector<Register>& registers,
                            OutputBuffer* out) {
  std::vector<std::vector<OutputBuffer>> rows;

  for (const Register& reg : registers) {
    auto color = GetRowColor(rows.size());
    if (reg.id() == RegisterID::kARMv8_cpsr)
      rows.push_back(DescribeCPSR(reg, color));
    else
      rows.push_back(DescribeRegister(reg, color));
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

bool FormatCategoryARM64(debug_ipc::RegisterCategory::Type category,
                         const std::vector<Register>& registers,
                         OutputBuffer* out, Err* err) {
  // Only general registers specialized for now.
  if (category != RegisterCategory::Type::kGeneral)
    return false;

  FormatGeneralRegisters(registers, out);
  return true;
}

}  // namespace zxdb
