// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/format_register_arm64.h"
#include "garnet/bin/zxdb/client/register.h"
#include "garnet/bin/zxdb/common/err.h"
#include "garnet/bin/zxdb/console/format_register.h"
#include "garnet/bin/zxdb/console/format_table.h"
#include "garnet/bin/zxdb/console/output_buffer.h"
#include "garnet/bin/zxdb/console/string_formatters.h"
#include "lib/fxl/strings/string_printf.h"

using debug_ipc::RegisterCategory;
using debug_ipc::RegisterID;

namespace zxdb {

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

#define FLAG_VALUE(value, shift, mask) (uint8_t)((value >> shift) & mask)

namespace {

Err FormatCPSR(const Register& cpsr, TextForegroundColor color,
               OutputBuffer* out) {
  uint64_t value = cpsr.GetValue();

  // We format as table, but we only need one row.
  std::vector<std::vector<OutputBuffer>> rows(1);
  auto& row = rows.back();

  // Register name.
  OutputBuffer buf;
  buf = OutputBuffer(RegisterIDToString(cpsr.id()));
  buf.SetForegroundColor(color);
  row.push_back(std::move(buf));

  // Value.
  buf = OutputBuffer();
  buf.SetForegroundColor(color);
  std::string hex_out;
  // Get the hex formatted value.
  Err err = GetLittleEndianHexOutput(cpsr.data(), &hex_out, 4);
  if (!err.ok())
    return err;
  buf.Append(fxl::StringPrintf(" %s ", hex_out.data()));

  // Get the custom formatting.
  buf.Append(fxl::StringPrintf(
      "(V = %d, C = %d, Z = %d, N = %d)", FLAG_VALUE(value, kCpsrVShift, 0x1),
      FLAG_VALUE(value, kCpsrCShift, 0x1), FLAG_VALUE(value, kCpsrZShift, 0x1),
      FLAG_VALUE(value, kCpsrNShift, 0x1)));
  row.push_back(std::move(buf));

  // TODO(donosoc): Implement system formatting when we enable
  //                ENABLE_ARM64_SYSTEM_FLAGS
  auto colspecs = std::vector<ColSpec>(2);
  FormatTable(std::move(colspecs), rows, out);
  return Err();
}

Err FormatGenericRow(const Register& reg, TextForegroundColor color,
                     std::vector<OutputBuffer>* row) {
  auto name = OutputBuffer(RegisterIDToString(reg.id()));
  name.SetForegroundColor(color);
  row->push_back(std::move(name));

  std::string value;
  Err err = GetLittleEndianHexOutput(reg.data(), &value);
  if (!err.ok())
    return err;
  OutputBuffer value_buffer(value);
  value_buffer.SetForegroundColor(color);
  row->push_back(std::move(value_buffer));
  return Err();
}

Err FormatGeneralRegisters(const std::vector<Register>& registers,
                           OutputBuffer* out) {
  // Title.
  out->Append(OutputBuffer(Syntax::kHeading, "General Purpose Registers\n"));

  OutputBuffer cpsr_out;
  std::vector<std::vector<OutputBuffer>> rows;
  for (const Register& reg : registers) {
    // Different row colors for easier reading.
    auto color = rows.size() % 2 == 0 ? TextForegroundColor::kDefault
                                      : TextForegroundColor::kLightGray;
    Err err;
    if (reg.id() == RegisterID::kARMv8_cpsr) {
      err = FormatCPSR(reg, color, &cpsr_out);
    } else {
      // Other registers get default treatment.
      rows.emplace_back();
      err = FormatGenericRow(reg, color, &rows.back());
    }

    if (!err.ok())
      return err;
  }

  auto colspecs = std::vector<ColSpec>(
      {ColSpec(Align::kLeft, 0, "Name"), ColSpec(Align::kRight, 0, "Value")});
  if (!rows.empty()) {
    FormatTable(colspecs, rows, out);
    // Separate rflags if appropriate.
    if (!cpsr_out.empty())
      out->Append("\n");
  }
  if (!cpsr_out.empty())
    out->Append(std::move(cpsr_out));
  return Err();

  return Err();
}

}  // namespace

bool FormatCategoryARM64(debug_ipc::RegisterCategory::Type category,
                         const std::vector<Register>& registers,
                         OutputBuffer* out, Err* err) {
  // Only general registers specialized for now.
  if (category != RegisterCategory::Type::kGeneral)
    return false;

  *err = FormatGeneralRegisters(registers, out);
  return true;
}

}  // namespace zxdb
