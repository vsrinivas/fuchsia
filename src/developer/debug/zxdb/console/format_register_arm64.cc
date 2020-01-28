// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/format_register_arm64.h"

#include <inttypes.h>

#include "src/developer/debug/shared/arch_arm64.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/common/string_util.h"
#include "src/developer/debug/zxdb/console/format_register.h"
#include "src/developer/debug/zxdb/console/format_table.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/console/string_formatters.h"
#include "src/lib/fxl/strings/string_printf.h"

using debug_ipc::Register;
using debug_ipc::RegisterCategory;
using debug_ipc::RegisterID;

namespace zxdb {

namespace {

#define FLAG_VALUE(value, shift, mask) (uint8_t)((value >> shift) & mask)

TextForegroundColor GetRowColor(size_t table_len) {
  return table_len % 2 == 0 ? TextForegroundColor::kDefault : TextForegroundColor::kLightGray;
}

// General registers -------------------------------------------------------------------------------

std::vector<OutputBuffer> DescribeCPSR(const Register& cpsr, TextForegroundColor color) {
  std::vector<OutputBuffer> result;
  result.emplace_back(RegisterIDToString(cpsr.id), color);

  uint32_t value = static_cast<uint32_t>(cpsr.GetValue());

  result.emplace_back(fxl::StringPrintf("0x%08x", value), color);

  // Decode individual flags.
  result.emplace_back(
      fxl::StringPrintf("V=%d, C=%d, Z=%d, N=%d", ARM64_FLAG_VALUE(value, Cpsr, V),
                        ARM64_FLAG_VALUE(value, Cpsr, C), ARM64_FLAG_VALUE(value, Cpsr, Z),
                        ARM64_FLAG_VALUE(value, Cpsr, N)),
      color);

  return result;
}

std::vector<OutputBuffer> DescribeCPSRExtended(const Register& cpsr, TextForegroundColor color) {
  std::vector<OutputBuffer> result;
  result.reserve(3);
  result.emplace_back(OutputBuffer());
  result.emplace_back(OutputBuffer());

  uint32_t value = static_cast<uint32_t>(cpsr.GetValue());

  result.emplace_back(
      fxl::StringPrintf("EL=%d, F=%d, I=%d, A=%d, D=%d, IL=%d, SS=%d, PAN=%d, UAO=%d",
                        ARM64_FLAG_VALUE(value, Cpsr, EL), ARM64_FLAG_VALUE(value, Cpsr, F),
                        ARM64_FLAG_VALUE(value, Cpsr, I), ARM64_FLAG_VALUE(value, Cpsr, A),
                        ARM64_FLAG_VALUE(value, Cpsr, D), ARM64_FLAG_VALUE(value, Cpsr, IL),
                        ARM64_FLAG_VALUE(value, Cpsr, SS), ARM64_FLAG_VALUE(value, Cpsr, PAN),
                        ARM64_FLAG_VALUE(value, Cpsr, UAO)),
      color);
  return result;
}

void FormatGeneralRegisters(const FormatRegisterOptions& options,
                            const std::vector<Register>& registers, OutputBuffer* out) {
  std::vector<std::vector<OutputBuffer>> rows;

  for (const Register& reg : registers) {
    auto color = GetRowColor(rows.size());
    if (reg.id == RegisterID::kARMv8_cpsr) {
      rows.push_back(DescribeCPSR(reg, color));
      if (options.extended)
        rows.push_back(DescribeCPSRExtended(reg, color));
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

// DBGBCR ------------------------------------------------------------------------------------------

std::vector<OutputBuffer> FormatDBGBCR(const Register& reg, TextForegroundColor color) {
  std::vector<OutputBuffer> row;
  row.reserve(3);
  row.emplace_back(RegisterIDToString(reg.id), color);

  uint64_t value = reg.GetValue();
  row.emplace_back(to_hex_string(value, 8), color);

  row.emplace_back(
      fxl::StringPrintf("E=%d, PMC=%d, BAS=%d, HMC=%d, SSC=%d, LBN=%d, BT=%d",
                        ARM64_FLAG_VALUE(value, DBGBCR, E), ARM64_FLAG_VALUE(value, DBGBCR, PMC),
                        ARM64_FLAG_VALUE(value, DBGBCR, BAS), ARM64_FLAG_VALUE(value, DBGBCR, HMC),
                        ARM64_FLAG_VALUE(value, DBGBCR, SSC), ARM64_FLAG_VALUE(value, DBGBCR, LBN),
                        ARM64_FLAG_VALUE(value, DBGBCR, BT)),
      color);
  return row;
}

// ID_AA64DFR0_EL1 ---------------------------------------------------------------------------------

std::vector<OutputBuffer> FormatID_AA64FR0_EL1(const Register& reg, TextForegroundColor color) {
  std::vector<OutputBuffer> row;
  row.reserve(3);
  row.emplace_back(RegisterIDToString(reg.id), color);

  uint64_t value = static_cast<uint64_t>(reg.GetValue());
  row.emplace_back(to_hex_string(value, 8), color);

  row.emplace_back(fxl::StringPrintf("DV=%d, TV=%d, PMUV=%d, BRP=%d, WRP=%d, CTX_CMP=%d, PMSV=%d",
                                     ARM64_FLAG_VALUE(value, ID_AA64DFR0_EL1, DV),
                                     ARM64_FLAG_VALUE(value, ID_AA64DFR0_EL1, TV),
                                     ARM64_FLAG_VALUE(value, ID_AA64DFR0_EL1, PMUV),
                                     // The register count values have 1 subtracted to them.
                                     ARM64_FLAG_VALUE(value, ID_AA64DFR0_EL1, BRP) + 1,
                                     ARM64_FLAG_VALUE(value, ID_AA64DFR0_EL1, WRP) + 1,
                                     ARM64_FLAG_VALUE(value, ID_AA64DFR0_EL1, CTX_CMP) + 1,
                                     ARM64_FLAG_VALUE(value, ID_AA64DFR0_EL1, PMSV)),
                   color);
  return row;
}

// MDSCR -------------------------------------------------------------------------------------------

std::vector<OutputBuffer> FormatMDSCR(const Register& reg, TextForegroundColor color) {
  std::vector<OutputBuffer> row;
  row.reserve(3);
  row.emplace_back(RegisterIDToString(reg.id), color);

  uint64_t value = static_cast<uint64_t>(reg.GetValue());
  row.emplace_back(to_hex_string(value, 8), color);

  row.emplace_back(
      fxl::StringPrintf(
          "SS=%d, TDCC=%d, KDE=%d, HDE=%d, MDE=%d, RAZ/WI=%d, "
          "TDA=%d, INTdis=%d, "
          "TXU=%d, RXO=%d, TXfull=%d, RXfull=%d",
          ARM64_FLAG_VALUE(value, MDSCR_EL1, SS), ARM64_FLAG_VALUE(value, MDSCR_EL1, TDCC),
          ARM64_FLAG_VALUE(value, MDSCR_EL1, KDE), ARM64_FLAG_VALUE(value, MDSCR_EL1, HDE),
          ARM64_FLAG_VALUE(value, MDSCR_EL1, MDE), ARM64_FLAG_VALUE(value, MDSCR_EL1, RAZ_WI),
          ARM64_FLAG_VALUE(value, MDSCR_EL1, TDA), ARM64_FLAG_VALUE(value, MDSCR_EL1, INTdis),
          ARM64_FLAG_VALUE(value, MDSCR_EL1, TXU), ARM64_FLAG_VALUE(value, MDSCR_EL1, RXO),
          ARM64_FLAG_VALUE(value, MDSCR_EL1, TXfull), ARM64_FLAG_VALUE(value, MDSCR_EL1, RXfull)),
      color);

  return row;
}

void FormatDebugRegisters(const FormatRegisterOptions& options,
                          const std::vector<Register>& registers, OutputBuffer* out) {
  std::vector<std::vector<OutputBuffer>> rows;

  for (const Register& reg : registers) {
    auto color = GetRowColor(rows.size() + 1);
    switch (reg.id) {
      case RegisterID::kARMv8_dbgbcr0_el1:
      case RegisterID::kARMv8_dbgbcr1_el1:
      case RegisterID::kARMv8_dbgbcr2_el1:
      case RegisterID::kARMv8_dbgbcr3_el1:
      case RegisterID::kARMv8_dbgbcr4_el1:
      case RegisterID::kARMv8_dbgbcr5_el1:
      case RegisterID::kARMv8_dbgbcr6_el1:
      case RegisterID::kARMv8_dbgbcr7_el1:
      case RegisterID::kARMv8_dbgbcr8_el1:
      case RegisterID::kARMv8_dbgbcr9_el1:
      case RegisterID::kARMv8_dbgbcr10_el1:
      case RegisterID::kARMv8_dbgbcr11_el1:
      case RegisterID::kARMv8_dbgbcr12_el1:
      case RegisterID::kARMv8_dbgbcr13_el1:
      case RegisterID::kARMv8_dbgbcr14_el1:
      case RegisterID::kARMv8_dbgbcr15_el1:
        rows.push_back(FormatDBGBCR(reg, color));
        break;
      case RegisterID::kARMv8_id_aa64dfr0_el1:
        rows.push_back(FormatID_AA64FR0_EL1(reg, color));
        break;
      case RegisterID::kARMv8_mdscr_el1:
        rows.push_back(FormatMDSCR(reg, color));
        break;
      default:
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

}  // namespace

bool FormatCategoryARM64(const FormatRegisterOptions& options, debug_ipc::RegisterCategory category,
                         const std::vector<Register>& registers, OutputBuffer* out) {
  switch (category) {
    case RegisterCategory::kGeneral:
      FormatGeneralRegisters(options, registers, out);
      return true;
    case RegisterCategory::kDebug:
      FormatDebugRegisters(options, registers, out);
      return true;
    default:
      return false;
  }
}

}  // namespace zxdb
