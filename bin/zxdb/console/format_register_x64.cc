// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <set>

#include "garnet/bin/zxdb/console/format_register_x64.h"
#include "garnet/bin/zxdb/client/register.h"
#include "garnet/bin/zxdb/common/err.h"
#include "garnet/bin/zxdb/console/format_register.h"
#include "garnet/bin/zxdb/console/format_table.h"
#include "garnet/bin/zxdb/console/output_buffer.h"
#include "garnet/bin/zxdb/console/string_formatters.h"
#include "garnet/lib/debug_ipc/records.h"
#include "lib/fxl/strings/string_printf.h"
#include "lib/fxl/logging.h"

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

inline Err PushFP(const Register& reg, TextForegroundColor color,
                  std::vector<OutputBuffer>* row) {
  std::string fp_val;
  Err err = GetFPString(reg.data(), &fp_val);
  if (!err.ok())
    return err;
  OutputBuffer fp_out(std::move(fp_val));
  fp_out.SetForegroundColor(color);
  row->emplace_back(std::move(fp_out));
  return Err();
}

// Function used for interleaving color, for easier reading of a table.
TextForegroundColor GetRowColor(size_t table_len) {
  return table_len % 2 == 0 ? TextForegroundColor::kDefault
                            : TextForegroundColor::kLightGray;
}

// Format General Registers -----------------------------------------------------

constexpr int kRflagsCarryFlagShift = 0;
constexpr int kRflagsParityFlagShift = 2;
constexpr int kRflagsAuxCarryFlagShift = 4;
constexpr int kRflagsZeroFlagShift = 6;
constexpr int kRflagsSignFlagShift = 7;
constexpr int kRflagsTrapFlagShift = 8;
constexpr int kRflagsInterruptEnableFlagShift = 9;
constexpr int kRflagsDirectionFlagShift = 10;
constexpr int kRflagsOverflowFlagShift = 11;

#ifdef ENABLE_X64_SYSTEM_FLAGS

constexpr int kRflagsIoPriviledgeLevelShift = 12;
constexpr int kRflagsNestedTaskShift = 14;
constexpr int kRflagsResumeFlagShift = 16;
constexpr int kRflagsVirtual8086ModeShift = 17;
constexpr int kRflagsAlignmentCheckShift = 18;
constexpr int kRflagsVirtualInterruptFlagShift = 19;
constexpr int kRflagsVirtualInterruptPendingShift = 20;
constexpr int kRflagsIdFlagShift = 21;

#endif

Err FormatRflags(const Register& rflags, OutputBuffer* out) {
  TextForegroundColor color = TextForegroundColor::kDefault;

  uint64_t value = rflags.GetValue();
  std::vector<std::vector<OutputBuffer>> rows(1);
  auto& row = rows.back();

  PushName(rflags, color, &row);

  Err err = PushHex(rflags, color, &row, 4);
  if (!err.ok())
    return err;

  OutputBuffer rflags_out(fxl::StringPrintf(
      "CF=%d, PF=%d, AF=%d, ZF=%d, SF=%d, TF=%d, IF=%d, DF=%d, OF=%d",
      FLAG_VALUE(value, kRflagsCarryFlagShift, 0x1),
      FLAG_VALUE(value, kRflagsParityFlagShift, 0x1),
      FLAG_VALUE(value, kRflagsAuxCarryFlagShift, 0x1),
      FLAG_VALUE(value, kRflagsZeroFlagShift, 0x1),
      FLAG_VALUE(value, kRflagsSignFlagShift, 0x1),
      FLAG_VALUE(value, kRflagsTrapFlagShift, 0x1),
      FLAG_VALUE(value, kRflagsInterruptEnableFlagShift, 0x1),
      FLAG_VALUE(value, kRflagsDirectionFlagShift, 0x1),
      FLAG_VALUE(value, kRflagsOverflowFlagShift, 0x1)));
  rflags_out.SetForegroundColor(color);
  row.push_back(std::move(rflags_out));

  // TODO(donosoc): Do the correct formatting when we enable
  //                ENABLE_X64_SYSTEM_FLAGS

  auto colspecs = std::vector<ColSpec>({ColSpec(Align::kLeft, 0, "Name"),
                                        ColSpec(Align::kLeft, 0, "Raw", 1),
                                        ColSpec(Align::kLeft, 0, "Value", 1)});
  FormatTable(std::move(colspecs), rows, out);
  return Err();
}

Err FormatGenericRow(const Register& reg, TextForegroundColor color,
                     std::vector<OutputBuffer>* row) {
  PushName(reg, color, row);
  Err err = PushHex(reg, color, row, 8);
  return err;
}

// Using a vector of output buffers make it easy to not have to worry about
// appending new lines per each new section.
Err FormatGeneralRegisters(const std::vector<Register>& registers,
                           OutputBuffer* out) {
  // We need it separate to maintain the indentation.
  OutputBuffer rflags_out;
  std::vector<std::vector<OutputBuffer>> rows;
  for (const Register& reg : registers) {
    auto color = GetRowColor(rows.size());
    // We still want to output the hex value.
    Err err;
    if (reg.id() == RegisterID::kX64_rflags) {
      err = FormatRflags(reg, &rflags_out);
    } else {
      // Other register get the default value treatment.
      rows.emplace_back();
      err = FormatGenericRow(reg, color, &rows.back());
    }

    if (!err.ok())
      return err;
  }

  // Output the tables.
  if (!rows.empty()) {
    auto colspecs =
        std::vector<ColSpec>({ColSpec(Align::kLeft, 0, "Name"),
                              ColSpec(Align::kLeft, 0, "Value", 1)});
    FormatTable(std::move(colspecs), rows, out);
  }
  if (!rflags_out.empty())
    out->Append(std::move(rflags_out));
  return Err();
}

// Format Floating Point (x87) --------------------------------------------------

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
    auto colspecs = std::vector<ColSpec>(
        {ColSpec(Align::kLeft, 0, "Name"), ColSpec(Align::kLeft, 0, "Raw", 1),
         ColSpec(Align::kLeft, 0, "Value", 1)});
    FormatTable(std::move(colspecs), rows, &control_out);
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

    OutputBuffer value_out;
    auto colspecs = std::vector<ColSpec>({ColSpec(Align::kLeft, 0, "Name"),
                                          ColSpec(Align::kLeft, 0, "Value", 1),
                                          ColSpec(Align::kLeft, 0, "Raw", 1)});
    FormatTable(std::move(colspecs), rows, &value_out);
    out->Append(std::move(value_out));
  }
  return Err();
}

// Format Debug Registers -------------------------------------------------------

constexpr int kDr6B0Shift = 0;
constexpr int kDr6B1Shift = 1;
constexpr int kDr6B2Shift = 2;
constexpr int kDr6B3Shift = 3;
constexpr int kDr6BDShidt = 13;
constexpr int kDr6BSShidt = 14;
constexpr int kDr6BTShidt = 15;

Err FormatDr6(const Register& dr6, TextForegroundColor color,
              std::vector<OutputBuffer>* row) {
  PushName(dr6, color, row);
  Err err = PushHex(dr6, color, row, 4);
  if (!err.ok())
    return err;

  uint64_t value = dr6.GetValue();
  OutputBuffer dr6_out(fxl::StringPrintf(
      "B0=%d, B1=%d, B2=%d, B3=%d, BD=%d, BS=%d, BT=%d",
      FLAG_VALUE(value, kDr6B0Shift, 1), FLAG_VALUE(value, kDr6B1Shift, 1),
      FLAG_VALUE(value, kDr6B2Shift, 1), FLAG_VALUE(value, kDr6B3Shift, 1),
      FLAG_VALUE(value, kDr6BDShidt, 1), FLAG_VALUE(value, kDr6BSShidt, 1),
      FLAG_VALUE(value, kDr6BTShidt, 1)));
  dr6_out.SetForegroundColor(color);
  row->push_back(std::move(dr6_out));
  return Err();
}

constexpr int kDr7L0Shift = 0;
constexpr int kDr7G0Shift = 1;
constexpr int kDr7L1Shift = 2;
constexpr int kDr7G1Shift = 3;
constexpr int kDr7L2Shift = 4;
constexpr int kDr7G2Shift = 5;
constexpr int kDr7L3Shift = 6;
constexpr int kDr7G3Shift = 7;
constexpr int kDr7LEShift = 8;
constexpr int kDr7GEShift = 9;
constexpr int kDr7GDShift = 13;
constexpr int kDr7RW0Shift = 16;
constexpr int kDr7LEN0Shift = 18;
constexpr int kDr7RW1Shift = 20;
constexpr int kDr7LEN1Shift = 22;
constexpr int kDr7RW2Shift = 24;
constexpr int kDr7LEN2Shift = 26;
constexpr int kDr7RW3Shift = 28;
constexpr int kDr7LEN3Shift = 30;

// NOTE: This function receives the table because it will append another row.
Err FormatDr7(const Register& dr7,
              std::vector<std::vector<OutputBuffer>>* rows) {
  uint64_t value = dr7.GetValue();
  rows->emplace_back();
  // The same color for both rows.
  auto color = GetRowColor(rows->size());
  auto& row = rows->back();
  row.reserve(3);
  PushName(dr7, color, &row);
  Err err = PushHex(dr7, color, &row, 4);
  if (!err.ok())
    return err;
  OutputBuffer dr7_out(fxl::StringPrintf(
      "L0=%d, G0=%d, L1=%d, G1=%d, L2=%d, G2=%d, L3=%d, G4=%d, LE=%d, GE=%d, "
      "GD=%d",
      FLAG_VALUE(value, kDr7L0Shift, 1), FLAG_VALUE(value, kDr7G0Shift, 1),
      FLAG_VALUE(value, kDr7L1Shift, 1), FLAG_VALUE(value, kDr7G1Shift, 1),
      FLAG_VALUE(value, kDr7L2Shift, 1), FLAG_VALUE(value, kDr7G2Shift, 1),
      FLAG_VALUE(value, kDr7L3Shift, 1), FLAG_VALUE(value, kDr7G3Shift, 1),
      FLAG_VALUE(value, kDr7LEShift, 1), FLAG_VALUE(value, kDr7GEShift, 1),
      FLAG_VALUE(value, kDr7GDShift, 1)));
  dr7_out.SetForegroundColor(color);
  row.push_back(std::move(dr7_out));

  /* // We push the second row. */
  rows->emplace_back();
  auto& row2 = rows->back();
  row2.reserve(3);
  row2.emplace_back();
  row2.emplace_back();
  dr7_out = OutputBuffer(fxl::StringPrintf(
      "R/W0=%d, LEN0=%d, R/W1=%d, LEN1=%d, R/W2=%d, LEN2=%d, R/W3=%d, LEN3=%d",
      FLAG_VALUE(value, kDr7RW0Shift, 2), FLAG_VALUE(value, kDr7LEN0Shift, 2),
      FLAG_VALUE(value, kDr7RW1Shift, 2), FLAG_VALUE(value, kDr7LEN1Shift, 2),
      FLAG_VALUE(value, kDr7RW2Shift, 2), FLAG_VALUE(value, kDr7LEN2Shift, 2),
      FLAG_VALUE(value, kDr7RW3Shift, 2), FLAG_VALUE(value, kDr7LEN3Shift, 2)));
  dr7_out.SetForegroundColor(color);
  row2.emplace_back(std::move(dr7_out));
  return Err();
}

Err FormatDebugRegisters(const std::vector<Register>& registers,
                         OutputBuffer* out) {
  // dr[0-3] and dr[6-7] have different formats, so get separate tables.
  std::vector<std::vector<OutputBuffer>> dr03_rows;
  std::vector<std::vector<OutputBuffer>> dr67_rows;

  for (const Register& reg : registers) {
    // We do special formatting for dr6/dr7
    if (reg.id() == RegisterID::kX64_dr6) {
      dr67_rows.emplace_back();
      auto color = GetRowColor(dr67_rows.size());
      Err err = FormatDr6(reg, color, &dr67_rows.back());
      if (!err.ok())
        return err;
    } else if (reg.id() == RegisterID::kX64_dr7) {
      // Dr7 adds two lines.
      Err err = FormatDr7(reg, &dr67_rows);
      if (!err.ok())
        return err;
    } else {
      // Generic formatting for now.
      dr03_rows.emplace_back();
      auto color = GetRowColor(dr03_rows.size());
      Err err = FormatGenericRow(reg, color, &dr03_rows.back());
      if (!err.ok())
        return err;
    }
  }

  // Output each table if needed.
  if (!dr03_rows.empty()) {
    auto colspecs =
        std::vector<ColSpec>({ColSpec(Align::kLeft, 0, "Name"),
                              ColSpec(Align::kLeft, 0, "Value", 1)});
    FormatTable(std::move(colspecs), dr03_rows, out);
  }
  if (!dr67_rows.empty()) {
    auto colspecs = std::vector<ColSpec>(
        {ColSpec(Align::kLeft, 0, "Name"), ColSpec(Align::kLeft, 0, "Raw", 1),
         ColSpec(Align::kLeft, 0, "Value", 1)});
    FormatTable(std::move(colspecs), dr67_rows, out);
  }
  return Err();
}

}  // namespace

bool FormatCategoryX64(debug_ipc::RegisterCategory::Type category,
                       const std::vector<Register>& registers,
                       OutputBuffer* out, Err* err) {
  switch (category) {
    case RegisterCategory::Type::kGeneral:
      *err = FormatGeneralRegisters(registers, out);
      return true;
    case RegisterCategory::Type::kFloatingPoint:
      *err = FormatFPRegisters(registers, out);
      return true;
    case RegisterCategory::Type::kDebug:
      *err = FormatDebugRegisters(registers, out);
      return true;
    default:
      return false;
  }
}

}  // namespace zxdb
