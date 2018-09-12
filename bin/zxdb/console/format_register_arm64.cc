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

namespace {

#define FLAG_VALUE(value, shift, mask) (uint8_t)((value >> shift) & mask)

inline void PushName(const Register& reg, TextForegroundColor color,
                    std::vector<OutputBuffer>* row) {
  OutputBuffer buf(RegisterIDToString(reg.id()));
  buf.SetForegroundColor(color);
  row->emplace_back(std::move(buf));
}

inline Err PushHex(const Register& reg, TextForegroundColor color,
    std::vector<OutputBuffer>* row, int length) {
  std::string hex_out;
  Err err = GetLittleEndianHexOutput(reg.data(), &hex_out, length);
  if (!err.ok())
    return err;
  OutputBuffer buf(std::move(hex_out));
  buf.SetForegroundColor(color);
  row->emplace_back(std::move(buf));
  return Err();
}

// Format General Registers -----------------------------------------------------

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

Err FormatCPSR(const Register& cpsr, TextForegroundColor color,
               OutputBuffer* out) {
  uint64_t value = cpsr.GetValue();

  // We format as table, but we only need one row.
  std::vector<std::vector<OutputBuffer>> rows(1);
  auto& row = rows.back();

  PushName(cpsr, color, &row);

  Err err = PushHex(cpsr, color, &row, 4);
  if (!err.ok())
    return err;

  // Get the custom formatting.
  OutputBuffer cpsr_out(fxl::StringPrintf(
      "V=%d, C=%d, Z=%d, N=%d", FLAG_VALUE(value, kCpsrVShift, 0x1),
      FLAG_VALUE(value, kCpsrCShift, 0x1), FLAG_VALUE(value, kCpsrZShift, 0x1),
      FLAG_VALUE(value, kCpsrNShift, 0x1)));
  // TODO(donosoc): Implement system formatting when we enable
  //                ENABLE_ARM64_SYSTEM_FLAGS
  cpsr_out.SetForegroundColor(color);
  row.push_back(std::move(cpsr_out));

  auto colspecs = std::vector<ColSpec>({ColSpec(Align::kLeft, 0, "Name"),
                                        ColSpec(Align::kLeft, 0, "Raw", 1),
                                        ColSpec(Align::kLeft, 0, "Value", 1)});
  FormatTable(std::move(colspecs), rows, out);
  return Err();
}

Err FormatGenericRow(const Register& reg, TextForegroundColor color,
                     std::vector<OutputBuffer>* row) {
  auto name = OutputBuffer(RegisterIDToString(reg.id()));
  name.SetForegroundColor(color);
  row->push_back(std::move(name));
  PushName(reg, color, row);
  Err err = PushHex(reg, color, row, 8);
  return err;
}

Err FormatGeneralRegisters(const std::vector<Register>& registers,
                           OutputBuffer* out) {
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
  if (!rows.empty())
    FormatTable(colspecs, rows, out);
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
