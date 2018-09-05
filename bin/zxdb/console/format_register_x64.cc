// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/format_register_x64.h"
#include "garnet/bin/zxdb/client/register.h"
#include "garnet/bin/zxdb/common/err.h"
#include "garnet/bin/zxdb/console/format_register.h"
#include "garnet/bin/zxdb/console/format_table.h"
#include "garnet/bin/zxdb/console/output_buffer.h"
#include "garnet/bin/zxdb/console/string_formatters.h"
#include "garnet/lib/debug_ipc/records.h"
#include "lib/fxl/strings/string_printf.h"

using debug_ipc::RegisterCategory;
using debug_ipc::RegisterID;

namespace zxdb {


constexpr int kRflagsCarryFlagShift = 0;
constexpr int kRflagsParityFlagShift = 2;
constexpr int kRflagsAuxCarryFlagShift = 4;
constexpr int kRflagsZeroFlagShift = 6;
constexpr int kRflagsSignFlagShift = 7;
constexpr int kRflagsTrapFlagShift = 8;
constexpr int kRflagsInterruptEnableFlagShift = 9;
constexpr int kRflagsDirectionFlagShift = 10;
constexpr int kRflagsOverflowFlagShift = 11;

#ifdef ENABLE_X64_SYSTEM_RFLAGS

constexpr int kRflagsIoPriviledgeLevelShift = 12;
constexpr int kRflagsNestedTaskShift = 14;
constexpr int kRflagsResumeFlagShift = 16;
constexpr int kRflagsVirtual8086ModeShift = 17;
constexpr int kRflagsAlignmentCheckShift = 18;
constexpr int kRflagsVirtualInterruptFlagShift = 19;
constexpr int kRflagsVirtualInterruptPendingShift = 20;
constexpr int kRflagsIdFlagShift = 21;

#endif

#define FLAG_VALUE(value, shift, mask) (uint8_t)((value >> shift) & mask)

namespace {

Err FormatRflags(const Register& rflags, TextForegroundColor color,
                 OutputBuffer* out) {
  uint64_t value = rflags.GetValue();
  std::vector<std::vector<OutputBuffer>> rows(1);
  auto& row = rows.back();

  OutputBuffer buf;
  buf = OutputBuffer(RegisterIDToString(rflags.id()));
  buf.SetForegroundColor(color);
  row.push_back(std::move(buf));

  buf = OutputBuffer();
  buf.SetForegroundColor(color);
  std::string hex_out;
  Err err = GetLittleEndianHexOutput(rflags.data(), &hex_out, 4);
  if (!err.ok())
    return err;
  buf.Append(fxl::StringPrintf(" %s ", hex_out.data()));
  buf.Append(fxl::StringPrintf(
      "(CF=%d, PF=%d, AF=%d, ZF=%d, SF=%d, TF=%d, IF=%d, DF=%d, OF=%d)",
      FLAG_VALUE(value, kRflagsCarryFlagShift , 0x1),
      FLAG_VALUE(value, kRflagsParityFlagShift , 0x1),
      FLAG_VALUE(value, kRflagsAuxCarryFlagShift , 0x1),
      FLAG_VALUE(value, kRflagsZeroFlagShift , 0x1),
      FLAG_VALUE(value, kRflagsSignFlagShift, 0x1),
      FLAG_VALUE(value, kRflagsTrapFlagShift, 0x1),
      FLAG_VALUE(value, kRflagsInterruptEnableFlagShift, 0x1),
      FLAG_VALUE(value, kRflagsDirectionFlagShift, 0x1),
      FLAG_VALUE(value, kRflagsOverflowFlagShift, 0x1)));
  row.push_back(std::move(buf));

  // TODO(donosoc): Do the correct formatting when we enable this
  //                ENABLE_X64_SYSTEM_RFLAGS

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

// Using a vector of output buffers make it easy to not have to worry about
// appending new lines per each new section.
Err FormatGeneralRegisters(const std::vector<Register>& registers,
                           OutputBuffer* out) {
  // Title.
  out->Append(OutputBuffer(Syntax::kHeading, "General Purpose Registers\n"));

  // We need it separate to maintain the indentation.
  OutputBuffer rflags_out;
  std::vector<std::vector<OutputBuffer>> rows;
  for (const Register& reg : registers) {
    auto color = rows.size() % 2 == 0 ? TextForegroundColor::kDefault
                                      : TextForegroundColor::kLightGray;
    // We still want to output the hex value.
    Err err;
    if (reg.id() == RegisterID::kX64_rflags) {
      err = FormatRflags(reg, color, &rflags_out);
    } else {
      // Other register get the default value treatment.
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
    if (!rflags_out.empty())
      out->Append("\n");
  }
  if (!rflags_out.empty())
    out->Append(std::move(rflags_out));
  return Err();
}

}  // namespace

bool FormatCategoryX64(debug_ipc::RegisterCategory::Type category,
                       const std::vector<Register>& registers,
                       OutputBuffer* out, Err* err) {
  // For now, we only specialize the general registers.
  if (category != RegisterCategory::Type::kGeneral)
    return false;
  *err = FormatGeneralRegisters(registers, out);
  return true;
}

}  // namespace zxdb
