// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/format_register_x64.h"

#include <inttypes.h>
#include <lib/syslog/cpp/macros.h>

#include <set>

#include "src/developer/debug/ipc/records.h"
#include "src/developer/debug/ipc/register_desc.h"
#include "src/developer/debug/shared/arch_x86.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/console/format_register.h"
#include "src/developer/debug/zxdb/console/format_table.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/console/string_formatters.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

using debug::RegisterID;
using debug_ipc::RegisterCategory;

void PushName(const debug::RegisterValue& reg, TextForegroundColor color,
              std::vector<OutputBuffer>* row) {
  row->emplace_back(debug_ipc::RegisterIDToString(reg.id), color);
}

// A nonzero length will case that number of bytes to be printed.
void PushHex(const debug::RegisterValue& reg, TextForegroundColor color, int length,
             std::vector<OutputBuffer>* row) {
  containers::array_view<uint8_t> view = reg.data;
  if (length > 0)
    view = view.subview(0, length);
  row->emplace_back(GetLittleEndianHexOutput(view), color);
}

void PushFP(const debug::RegisterValue& reg, TextForegroundColor color,
            std::vector<OutputBuffer>* row) {
  row->emplace_back(GetFPString(reg.data), color);
}

// Function used for interleaving color, for easier reading of a table.
TextForegroundColor GetRowColor(size_t table_len) {
  return table_len % 2 == 0 ? TextForegroundColor::kDefault : TextForegroundColor::kLightGray;
}

// Format General Registers ------------------------------------------------------------------------

std::vector<OutputBuffer> DescribeRflags(const debug::RegisterValue& rflags,
                                         TextForegroundColor color) {
  std::vector<OutputBuffer> result;
  result.emplace_back(debug_ipc::RegisterIDToString(rflags.id), color);

  uint32_t value = static_cast<uint32_t>(rflags.GetValue());

  // Hex value: rflags is a 32 bit value.
  result.emplace_back(fxl::StringPrintf("0x%08x", value), color);

  // Decode individual flags.
  result.emplace_back(
      fxl::StringPrintf("CF=%d, PF=%d, AF=%d, ZF=%d, SF=%d, TF=%d, IF=%d, DF=%d, OF=%d",
                        X86_FLAG_VALUE(value, RflagsCF), X86_FLAG_VALUE(value, RflagsPF),
                        X86_FLAG_VALUE(value, RflagsAF), X86_FLAG_VALUE(value, RflagsZF),
                        X86_FLAG_VALUE(value, RflagsSF), X86_FLAG_VALUE(value, RflagsTF),
                        X86_FLAG_VALUE(value, RflagsIF), X86_FLAG_VALUE(value, RflagsDF),
                        X86_FLAG_VALUE(value, RflagsOF)),
      color);

  return result;
}

std::vector<OutputBuffer> DescribeRflagsExtended(const debug::RegisterValue& rflags,
                                                 TextForegroundColor color) {
  std::vector<OutputBuffer> result;
  result.reserve(3);
  result.emplace_back(OutputBuffer());
  result.emplace_back(OutputBuffer());

  uint32_t value = rflags.GetValue();

  // Decode individual flags.
  result.emplace_back(
      fxl::StringPrintf("IOPL=%d, NT=%d, RF=%d, VM=%d, AC=%d, VIF=%d, VIP=%d, ID=%d",
                        X86_FLAG_VALUE(value, RflagsIOPL), X86_FLAG_VALUE(value, RflagsNT),
                        X86_FLAG_VALUE(value, RflagsRF), X86_FLAG_VALUE(value, RflagsVM),
                        X86_FLAG_VALUE(value, RflagsAC), X86_FLAG_VALUE(value, RflagsVIF),
                        X86_FLAG_VALUE(value, RflagsVIP), X86_FLAG_VALUE(value, RflagsID)),
      color);

  return result;
}

void FormatGeneralRegisters(const FormatRegisterOptions& options,
                            const std::vector<debug::RegisterValue>& registers, OutputBuffer* out) {
  std::vector<std::vector<OutputBuffer>> rows;

  for (const debug::RegisterValue& reg : registers) {
    auto color = GetRowColor(rows.size());
    if (reg.id == RegisterID::kX64_rflags) {
      rows.push_back(DescribeRflags(reg, color));
      if (options.extended)
        rows.push_back(DescribeRflagsExtended(reg, color));
    } else {
      rows.push_back(DescribeRegister(reg, color));
    }
  }

  // Output the tables.
  if (!rows.empty()) {
    std::vector<ColSpec> colspecs({ColSpec(Align::kRight, 0, std::string(), 2),
                                   ColSpec(Align::kRight, 0, std::string(), 1), ColSpec()});
    FormatTable(colspecs, rows, out);
  }
}

// Format Floating Point (x87) ---------------------------------------------------------------------

inline const std::set<RegisterID>& GetFPControlRegistersSet() {
  static std::set<RegisterID> regs = {RegisterID::kX64_fcw, RegisterID::kX64_fsw,
                                      RegisterID::kX64_ftw, RegisterID::kX64_fop,
                                      RegisterID::kX64_fip, RegisterID::kX64_fdp};
  return regs;
}
inline const std::set<RegisterID>& GetFPValueRegistersSet() {
  static std::set<RegisterID> regs = {
      RegisterID::kX64_st0, RegisterID::kX64_st1, RegisterID::kX64_st2, RegisterID::kX64_st3,
      RegisterID::kX64_st4, RegisterID::kX64_st5, RegisterID::kX64_st6, RegisterID::kX64_st7};
  return regs;
}

void FormatFPRegisters(const std::vector<debug::RegisterValue>& registers, OutputBuffer* out) {
  // We want to look up the registers in two sets: control & values, and display them differently.
  // There is no memory movement the input, so taking pointers is fine.
  const auto& control_set = GetFPControlRegistersSet();
  std::vector<const debug::RegisterValue*> control_registers;
  const auto& value_set = GetFPValueRegistersSet();
  std::vector<const debug::RegisterValue*> value_registers;
  for (const debug::RegisterValue& reg : registers) {
    if (control_set.find(reg.id) != control_set.end()) {
      control_registers.push_back(&reg);
    } else if (value_set.find(reg.id) != value_set.end()) {
      value_registers.push_back(&reg);
    } else {
      FX_NOTREACHED() << "UNCATEGORIZED FP REGISTER: " << debug_ipc::RegisterIDToString(reg.id);
    }
  }

  // Format the control register first.
  if (!control_registers.empty()) {
    std::vector<std::vector<OutputBuffer>> rows;
    rows.reserve(control_registers.size());
    for (size_t i = 0; i < control_registers.size(); i++) {
      const auto& reg = *control_registers[i];
      rows.emplace_back();
      auto& row = rows[i];
      auto color = GetRowColor(rows.size());

      switch (reg.id) {
        default: {
          // TODO: Placeholder. Remove when all control registers have their
          //       custom output implemented.
          PushName(reg, color, &row);
          PushHex(reg, color, 4, &row);
          row.emplace_back();
        }
      }
    }

    // Output the control table.
    OutputBuffer control_out;
    auto colspecs =
        std::vector<ColSpec>({ColSpec(Align::kRight, 0, "", 2), ColSpec(Align::kLeft, 0, "", 1),
                              ColSpec(Align::kLeft, 0, "", 1)});
    FormatTable(colspecs, rows, &control_out);
    out->Append(std::move(control_out));
  }

  // Format the value registers.
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
      PushFP(reg, color, &row);
      PushHex(reg, color, 16, &row);
    }

    // The "value" for the floating point registers is left-aligned here rather
    // than right-aligned like the normal numeric registers because the
    // right-hand digits don't correspond to each other, and usually this will
    // end up aligning the decimal point which is nice.
    OutputBuffer value_out;
    auto colspecs = std::vector<ColSpec>({ColSpec(Align::kRight, 0, std::string(), 2),
                                          ColSpec(Align::kLeft, 0, std::string(), 1),
                                          ColSpec(Align::kLeft, 0, std::string(), 1)});
    FormatTable(std::move(colspecs), rows, &value_out);
    out->Append(std::move(value_out));
  }
}

// Vector Registers --------------------------------------------------------------------------------

void FormatVectorRegistersX64(const FormatRegisterOptions& options,
                              const std::vector<debug::RegisterValue>& registers,
                              OutputBuffer* out) {
  // This uses the standard vector register formatting, but converts from AVX-512 to AVX. Zircon
  // doesn't currently support AVX-512 but our canonical registers use this format. Unnecessarily
  // displaying all those 0's makes things more difficult to follow. If AVX-512 is supported in
  // the future we can show the zmm and xmm/ymm registers >= 16 when the target CPU has them.
  std::vector<debug::RegisterValue> non_vect;  // Control registers.
  std::vector<debug::RegisterValue> filtered;
  filtered.reserve(registers.size());

  for (auto& r : registers) {
    uint32_t id = static_cast<uint32_t>(r.id);
    // Filter out all vector registers >= 16 (these are additions in AVX-512).
    if (id >= static_cast<uint32_t>(RegisterID::kX64_zmm16) &&
        id <= static_cast<uint32_t>(RegisterID::kX64_zmm31))
      continue;

    if (id >= static_cast<uint32_t>(RegisterID::kX64_zmm0) &&
        id <= static_cast<uint32_t>(RegisterID::kX64_zmm15) && r.data.size() == 64) {
      // Convert 512-bit zmm0-15 to 256-bit "ymm" registers.
      RegisterID ymm_id =
          static_cast<RegisterID>(id - static_cast<uint32_t>(RegisterID::kX64_zmm0) +
                                  static_cast<uint32_t>(RegisterID::kX64_ymm0));
      filtered.emplace_back(ymm_id, std::vector<uint8_t>(r.data.begin(), r.data.begin() + 32));
    } else if ((id >= static_cast<uint32_t>(RegisterID::kX64_xmm0) &&
                id <= static_cast<uint32_t>(RegisterID::kX64_xmm15)) ||
               (id >= static_cast<uint32_t>(RegisterID::kX64_ymm0) &&
                id <= static_cast<uint32_t>(RegisterID::kX64_ymm15))) {
      // All other vector registers that happen to be in the list. We don't expect to have other
      // vector registers here, but pass the rest through unchanged if they appear.
      filtered.push_back(r);
    } else {
      // Control registers get a separate section.
      non_vect.push_back(r);
    }
  }

  // Start with any control registers.
  if (!non_vect.empty()) {
    FormatGeneralRegisters(non_vect, out);

    // Blank line separating sections.
    if (!filtered.empty())
      out->Append("\n");
  }

  if (!filtered.empty())
    FormatGeneralVectorRegisters(options, filtered, out);
}

// Debug Registers ---------------------------------------------------------------------------------

std::vector<OutputBuffer> FormatDr6(const debug::RegisterValue& dr6, TextForegroundColor color) {
  std::vector<OutputBuffer> result;
  result.emplace_back(debug_ipc::RegisterIDToString(dr6.id), color);

  // Write as padded 32-bit value.
  uint32_t value = static_cast<uint32_t>(dr6.GetValue());
  result.emplace_back(fxl::StringPrintf("0x%08x", value), color);

  result.emplace_back(fxl::StringPrintf("B0=%d, B1=%d, B2=%d, B3=%d, BD=%d, BS=%d, BT=%d",
                                        X86_FLAG_VALUE(value, DR6B0), X86_FLAG_VALUE(value, DR6B1),
                                        X86_FLAG_VALUE(value, DR6B2), X86_FLAG_VALUE(value, DR6B3),
                                        X86_FLAG_VALUE(value, DR6BD), X86_FLAG_VALUE(value, DR6BS),
                                        X86_FLAG_VALUE(value, DR6BT)),
                      color);

  return result;
}

// NOTE: This function receives the table because it will append another row.
void FormatDr7(const debug::RegisterValue& dr7, TextForegroundColor color,
               std::vector<std::vector<OutputBuffer>>* rows) {
  rows->emplace_back();
  auto& first_row = rows->back();

  // First row gets the name and raw value (padded 32 bits).
  first_row.emplace_back(debug_ipc::RegisterIDToString(dr7.id), color);
  uint32_t value = static_cast<uint32_t>(dr7.GetValue());
  first_row.emplace_back(fxl::StringPrintf("0x%08x", value), color);

  // First row decoded values.
  first_row.emplace_back(
      fxl::StringPrintf(
          "L0=%d, G0=%d, L1=%d, G1=%d, L2=%d, G2=%d, L3=%d, G4=%d, LE=%d, "
          "GE=%d, "
          "GD=%d",
          X86_FLAG_VALUE(value, DR7L0), X86_FLAG_VALUE(value, DR7G0), X86_FLAG_VALUE(value, DR7L1),
          X86_FLAG_VALUE(value, DR7G1), X86_FLAG_VALUE(value, DR7L2), X86_FLAG_VALUE(value, DR7G2),
          X86_FLAG_VALUE(value, DR7L3), X86_FLAG_VALUE(value, DR7G3), X86_FLAG_VALUE(value, DR7LE),
          X86_FLAG_VALUE(value, DR7GE), X86_FLAG_VALUE(value, DR7GD)),
      color);

  // Second row only gets decoded values in the 3rd column.
  rows->emplace_back();
  auto& second_row = rows->back();
  second_row.resize(2);  // Default-construct two empty cols.

  second_row.emplace_back(
      fxl::StringPrintf("R/W0=%d, LEN0=%d, R/W1=%d, LEN1=%d, R/W2=%d, "
                        "LEN2=%d, R/W3=%d, LEN3=%d",
                        X86_FLAG_VALUE(value, DR7RW0), X86_FLAG_VALUE(value, DR7LEN0),
                        X86_FLAG_VALUE(value, DR7RW1), X86_FLAG_VALUE(value, DR7LEN1),
                        X86_FLAG_VALUE(value, DR7RW2), X86_FLAG_VALUE(value, DR7LEN2),
                        X86_FLAG_VALUE(value, DR7RW3), X86_FLAG_VALUE(value, DR7LEN3)),
      color);
}

void FormatDebugRegisters(const std::vector<debug::RegisterValue>& registers, OutputBuffer* out) {
  // dr[0-3] and dr[6-7] have different formats, so get separate tables.
  std::vector<std::vector<OutputBuffer>> rows;
  std::vector<std::vector<OutputBuffer>> dr67_rows;

  for (const debug::RegisterValue& reg : registers) {
    auto color = GetRowColor(rows.size() + 1);

    // We do special formatting for dr6/dr7
    if (reg.id == RegisterID::kX64_dr6) {
      rows.push_back(FormatDr6(reg, color));
    } else if (reg.id == RegisterID::kX64_dr7) {
      FormatDr7(reg, color, &rows);
    } else {
      // Generic formatting for now.
      rows.push_back(DescribeRegister(reg, color));
    }
  }

  // Output each table if needed.
  auto colspecs =
      std::vector<ColSpec>({ColSpec(Align::kRight, 0, std::string(), 2),
                            ColSpec(Align::kRight, 0, std::string(), 1), ColSpec(Align::kLeft)});
  FormatTable(colspecs, rows, out);
}

}  // namespace

bool FormatCategoryX64(const FormatRegisterOptions& options, RegisterCategory category,
                       const std::vector<debug::RegisterValue>& registers, OutputBuffer* out) {
  switch (category) {
    case RegisterCategory::kGeneral:
      FormatGeneralRegisters(options, registers, out);
      return true;
    case RegisterCategory::kFloatingPoint:
      FormatFPRegisters(registers, out);
      return true;
    case RegisterCategory::kVector:
      FormatVectorRegistersX64(options, registers, out);
      return true;
    case RegisterCategory::kDebug:
      FormatDebugRegisters(registers, out);
      return true;
    default:
      return false;
  }
}

}  // namespace zxdb
