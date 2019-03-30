// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>

#include <set>

#include "garnet/bin/zxdb/console/format_register.h"
#include "garnet/bin/zxdb/console/format_register_x64.h"
#include "garnet/bin/zxdb/console/format_table.h"
#include "garnet/bin/zxdb/console/output_buffer.h"
#include "garnet/bin/zxdb/console/string_formatters.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/developer/debug/ipc/records.h"
#include "src/developer/debug/shared/arch_x86.h"
#include "src/developer/debug/zxdb/client/register.h"
#include "src/developer/debug/zxdb/common/err.h"

using debug_ipc::RegisterCategory;
using debug_ipc::RegisterID;

namespace zxdb {

namespace {

inline void PushName(const Register& reg, TextForegroundColor color,
                     std::vector<OutputBuffer>* row) {
  row->emplace_back(RegisterIDToString(reg.id()), color);
}

inline Err PushHex(const Register& reg, TextForegroundColor color,
                   std::vector<OutputBuffer>* row, int length) {
  std::string hex_out;
  Err err = GetLittleEndianHexOutput(reg.data(), &hex_out, length);
  if (!err.ok())
    return err;
  row->emplace_back(std::move(hex_out), color);
  return Err();
}

inline Err PushFP(const Register& reg, TextForegroundColor color,
                  std::vector<OutputBuffer>* row) {
  std::string fp_val;
  Err err = GetFPString(reg.data(), &fp_val);
  if (!err.ok())
    return err;
  row->emplace_back(std::move(fp_val), color);
  return Err();
}

// Function used for interleaving color, for easier reading of a table.
TextForegroundColor GetRowColor(size_t table_len) {
  return table_len % 2 == 0 ? TextForegroundColor::kDefault
                            : TextForegroundColor::kLightGray;
}

// Format General Registers ----------------------------------------------------

std::vector<OutputBuffer> DescribeRflags(const Register& rflags,
                                         TextForegroundColor color) {
  std::vector<OutputBuffer> result;
  result.emplace_back(RegisterIDToString(rflags.id()), color);

  uint64_t value = rflags.GetValue();

  // Hex value: rflags is a 32 bit value.
  result.emplace_back(fxl::StringPrintf("0x%08" PRIx64, value), color);

  // Decode individual flags.
  result.emplace_back(
      fxl::StringPrintf(
          "CF=%d, PF=%d, AF=%d, ZF=%d, SF=%d, TF=%d, IF=%d, DF=%d, OF=%d",
          X86_FLAG_VALUE(value, RflagsCF), X86_FLAG_VALUE(value, RflagsPF),
          X86_FLAG_VALUE(value, RflagsAF), X86_FLAG_VALUE(value, RflagsZF),
          X86_FLAG_VALUE(value, RflagsSF), X86_FLAG_VALUE(value, RflagsTF),
          X86_FLAG_VALUE(value, RflagsIF), X86_FLAG_VALUE(value, RflagsDF),
          X86_FLAG_VALUE(value, RflagsOF)),
      color);

  return result;
}

std::vector<OutputBuffer> DescribeRflagsExtended(const Register& rflags,
                                                 TextForegroundColor color) {
  std::vector<OutputBuffer> result;
  result.reserve(3);
  result.emplace_back(OutputBuffer());
  result.emplace_back(OutputBuffer());

  uint64_t value = rflags.GetValue();

  // Decode individual flags.
  result.emplace_back(
      fxl::StringPrintf(
          "IOPL=%d, NT=%d, RF=%d, VM=%d, AC=%d, VIF=%d, VIP=%d, ID=%d",
          X86_FLAG_VALUE(value, RflagsIOPL), X86_FLAG_VALUE(value, RflagsNT),
          X86_FLAG_VALUE(value, RflagsRF), X86_FLAG_VALUE(value, RflagsVM),
          X86_FLAG_VALUE(value, RflagsAC), X86_FLAG_VALUE(value, RflagsVIF),
          X86_FLAG_VALUE(value, RflagsVIP), X86_FLAG_VALUE(value, RflagsID)),
      color);

  return result;
}

void FormatGeneralRegisters(const FormatRegisterOptions& options,
                            const std::vector<Register>& registers,
                            OutputBuffer* out) {
  std::vector<std::vector<OutputBuffer>> rows;

  for (const Register& reg : registers) {
    auto color = GetRowColor(rows.size());
    if (reg.id() == RegisterID::kX64_rflags) {
      rows.push_back(DescribeRflags(reg, color));
      if (options.extended)
        rows.push_back(DescribeRflagsExtended(reg, color));
    } else
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

// Format Floating Point (x87) -------------------------------------------------

inline const std::set<RegisterID>& GetFPControlRegistersSet() {
  static std::set<RegisterID> regs = {
      RegisterID::kX64_fcw, RegisterID::kX64_fsw, RegisterID::kX64_ftw,
      RegisterID::kX64_fop, RegisterID::kX64_fip, RegisterID::kX64_fdp};
  return regs;
}
inline const std::set<RegisterID>& GetFPValueRegistersSet() {
  static std::set<RegisterID> regs = {
      RegisterID::kX64_st0, RegisterID::kX64_st1, RegisterID::kX64_st2,
      RegisterID::kX64_st3, RegisterID::kX64_st4, RegisterID::kX64_st5,
      RegisterID::kX64_st6, RegisterID::kX64_st7};
  return regs;
}

Err FormatFPRegisters(const std::vector<Register>& registers,
                      OutputBuffer* out) {
  // We want to look up the registers in two sets: control & values,
  // and display them differently.
  // There is no memory movement the inpue, so taking pointers is fine.
  const auto& control_set = GetFPControlRegistersSet();
  std::vector<const Register*> control_registers;
  const auto& value_set = GetFPValueRegistersSet();
  std::vector<const Register*> value_registers;
  for (const Register& reg : registers) {
    if (control_set.find(reg.id()) != control_set.end()) {
      control_registers.push_back(&reg);
    } else if (value_set.find(reg.id()) != value_set.end()) {
      value_registers.push_back(&reg);
    } else {
      FXL_NOTREACHED() << "UNCATEGORIZED FP REGISTER: "
                       << RegisterIDToString(reg.id());
    }
  }

  // We format the control register first.
  if (!control_registers.empty()) {
    std::vector<std::vector<OutputBuffer>> rows;
    rows.reserve(control_registers.size());
    for (size_t i = 0; i < control_registers.size(); i++) {
      const auto& reg = *control_registers[i];
      rows.emplace_back();
      auto& row = rows[i];
      auto color = GetRowColor(rows.size());

      Err err;
      switch (reg.id()) {
        default: {
          // TODO: Placeholder. Remove when all control registers have their
          //       custom output implemented.
          PushName(reg, color, &row);
          err = PushHex(reg, color, &row, 4);
          if (!err.ok())
            return err;
          row.emplace_back();
        }
      }

      if (!err.ok())
        return err;
    }

    // Output the control table.
    OutputBuffer control_out;
    auto colspecs = std::vector<ColSpec>({ColSpec(Align::kLeft, 0, "Name"),
                                          ColSpec(Align::kLeft, 0, "Raw", 1),
                                          ColSpec(Align::kLeft, 0, "", 1)});
    FormatTable(colspecs, rows, &control_out);
    out->Append(std::move(control_out));
  }

  // We format the value registers.
  if (!value_registers.empty()) {
    std::vector<std::vector<OutputBuffer>> rows;
    rows.reserve(value_registers.size());
    for (size_t i = 0; i < value_registers.size(); i++) {
      const auto& reg = *value_registers[i];
      rows.emplace_back();
      auto color = GetRowColor(rows.size());
      auto& row = rows[i];
      row.reserve(3);
      PushName(reg, color, &row);
      Err err = PushFP(reg, color, &row);
      if (!err.ok())
        return err;
      err = PushHex(reg, color, &row, 16);
      if (!err.ok())
        return err;
    }

    // The "value" for the floating point registers is left-aligned here rather
    // than right-aligned like the normal numeric registers because the
    // right-hand digits don't correspond to each other, and usually this will
    // end up aligning the decimal point which is nice.
    OutputBuffer value_out;
    auto colspecs = std::vector<ColSpec>(
        {ColSpec(Align::kRight), ColSpec(Align::kLeft, 0, std::string(), 1),
         ColSpec(Align::kLeft, 0, std::string(), 1)});
    FormatTable(std::move(colspecs), rows, &value_out);
    out->Append(std::move(value_out));
  }
  return Err();
}

// Format Debug Registers ------------------------------------------------------

std::vector<OutputBuffer> FormatDr6(const Register& dr6,
                                    TextForegroundColor color) {
  std::vector<OutputBuffer> result;
  result.emplace_back(RegisterIDToString(dr6.id()), color);

  // Write as padded 32-bit value.
  uint64_t value = dr6.GetValue();
  result.emplace_back(fxl::StringPrintf("0x%08" PRIx64, value), color);

  result.emplace_back(
      fxl::StringPrintf(
          "B0=%d, B1=%d, B2=%d, B3=%d, BD=%d, BS=%d, BT=%d",
          X86_FLAG_VALUE(value, Dr6B0), X86_FLAG_VALUE(value, Dr6B1),
          X86_FLAG_VALUE(value, Dr6B2), X86_FLAG_VALUE(value, Dr6B3),
          X86_FLAG_VALUE(value, Dr6BD), X86_FLAG_VALUE(value, Dr6BS),
          X86_FLAG_VALUE(value, Dr6BT)),
      color);

  return result;
}

// NOTE: This function receives the table because it will append another row.
void FormatDr7(const Register& dr7, TextForegroundColor color,
               std::vector<std::vector<OutputBuffer>>* rows) {
  rows->emplace_back();
  auto& first_row = rows->back();

  // First row gets the name and raw value (padded 32 bits).
  first_row.emplace_back(RegisterIDToString(dr7.id()), color);
  uint64_t value = dr7.GetValue();
  first_row.emplace_back(fxl::StringPrintf("0x%08" PRIx64, value), color);

  // First row decoded values.
  first_row.emplace_back(
      fxl::StringPrintf(
          "L0=%d, G0=%d, L1=%d, G1=%d, L2=%d, G2=%d, L3=%d, G4=%d, LE=%d, "
          "GE=%d, "
          "GD=%d",
          X86_FLAG_VALUE(value, Dr7L0), X86_FLAG_VALUE(value, Dr7G0),
          X86_FLAG_VALUE(value, Dr7L1), X86_FLAG_VALUE(value, Dr7G1),
          X86_FLAG_VALUE(value, Dr7L2), X86_FLAG_VALUE(value, Dr7G2),
          X86_FLAG_VALUE(value, Dr7L3), X86_FLAG_VALUE(value, Dr7G3),
          X86_FLAG_VALUE(value, Dr7LE), X86_FLAG_VALUE(value, Dr7GE),
          X86_FLAG_VALUE(value, Dr7GD)),
      color);

  // Second row only gets decoded values in the 3rd column.
  rows->emplace_back();
  auto& second_row = rows->back();
  second_row.resize(2);  // Default-construct two empty cols.

  second_row.emplace_back(
      fxl::StringPrintf(
          "R/W0=%d, LEN0=%d, R/W1=%d, LEN1=%d, R/W2=%d, "
          "LEN2=%d, R/W3=%d, LEN3=%d",
          X86_FLAG_VALUE(value, Dr7RW0), X86_FLAG_VALUE(value, Dr7LEN0),
          X86_FLAG_VALUE(value, Dr7RW1), X86_FLAG_VALUE(value, Dr7LEN1),
          X86_FLAG_VALUE(value, Dr7RW2), X86_FLAG_VALUE(value, Dr7LEN2),
          X86_FLAG_VALUE(value, Dr7RW3), X86_FLAG_VALUE(value, Dr7LEN3)),
      color);
}

void FormatDebugRegisters(const std::vector<Register>& registers,
                          OutputBuffer* out) {
  // dr[0-3] and dr[6-7] have different formats, so get separate tables.
  std::vector<std::vector<OutputBuffer>> rows;
  std::vector<std::vector<OutputBuffer>> dr67_rows;

  for (const Register& reg : registers) {
    auto color = GetRowColor(rows.size() + 1);

    // We do special formatting for dr6/dr7
    if (reg.id() == RegisterID::kX64_dr6) {
      rows.push_back(FormatDr6(reg, color));
    } else if (reg.id() == RegisterID::kX64_dr7) {
      FormatDr7(reg, color, &rows);
    } else {
      // Generic formatting for now.
      rows.push_back(DescribeRegister(reg, color));
    }
  }

  // Output each table if needed.
  auto colspecs = std::vector<ColSpec>(
      {ColSpec(Align::kLeft), ColSpec(Align::kRight, 0, std::string(), 1),
       ColSpec(Align::kLeft)});
  FormatTable(colspecs, rows, out);
}

}  // namespace

bool FormatCategoryX64(const FormatRegisterOptions& options,
                       debug_ipc::RegisterCategory::Type category,
                       const std::vector<Register>& registers,
                       OutputBuffer* out, Err* err) {
  switch (category) {
    case RegisterCategory::Type::kGeneral:
      FormatGeneralRegisters(options, registers, out);
      return true;
    case RegisterCategory::Type::kFP:
      *err = FormatFPRegisters(registers, out);
      return true;
    case RegisterCategory::Type::kDebug:
      FormatDebugRegisters(registers, out);
      return true;
    default:
      return false;
  }
}

}  // namespace zxdb
