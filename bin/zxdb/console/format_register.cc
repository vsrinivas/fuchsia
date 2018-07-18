// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>

#include <map>

#include "garnet/bin/zxdb/client/err.h"
#include "garnet/bin/zxdb/client/register.h"
#include "garnet/bin/zxdb/client/session.h"
#include "garnet/bin/zxdb/console/console.h"
#include "garnet/bin/zxdb/console/format_register.h"
#include "garnet/bin/zxdb/console/format_table.h"
#include "garnet/bin/zxdb/console/output_buffer.h"
#include "garnet/public/lib/fxl/strings/string_printf.h"
#include "garnet/public/lib/fxl/logging.h"

namespace zxdb {

using debug_ipc::RegisterID;

namespace {

inline OutputBuffer RegisterValueToOutputBuffer(const Register& reg) {
  std::string val;
  // Print in groups 32 bits.
  switch (reg.size()) {
    case 1:
    case 2:
    case 4:
      return OutputBuffer::WithContents(
          fxl::StringPrintf("%.8" PRIx64, reg.GetValue()));
      break;
    case 8:
    case 16:
    case 32:
    case 64:
      std::vector<std::string> chunks;
      auto it = reg.begin();
      while (it < reg.end()) {
        chunks.push_back(fxl::StringPrintf(
            "%.8x", *reinterpret_cast<const uint32_t*>(it)));
        it += 4;
      }

      // We append them backwards
      OutputBuffer out;
      auto cit = chunks.rbegin();
      while (cit != chunks.rend()) {
        out.Append(*cit);
        cit++;
        if (cit != chunks.rend())
          out.Append(" ");
      }

      return out;
  }

  FXL_NOTREACHED() << "Invalid size for " << __PRETTY_FUNCTION__ << ": "
                   << reg.size();
  return OutputBuffer();
}

// Using a vector of output buffers make it easy to not have to worry about
// appending new lines per each new section.
Err InternalFormatCategory(debug_ipc::RegisterCategory::Type cat,
                           const std::vector<Register>& registers,
                           const std::string& searched_register,
                           std::vector<OutputBuffer>* out_buffers) {
  std::vector<std::pair<const Register*, std::string>> found_registers;
  for (const auto& reg : registers) {
    std::string reg_name = RegisterIDToString(reg.id());
    if (searched_register.empty()) {
      found_registers.push_back({&reg, reg_name});
    } else {
      // TODO(donosoc): Enable more permissive comparison.
      if (reg_name == searched_register) {
        found_registers.push_back({&reg, reg_name});
        break;
      }
    }
  }

  // If this category didn't find registers, skip it.
  if (found_registers.empty())
    return Err();

  // Title.
  auto category_title = fxl::StringPrintf(
      "%s Registers", RegisterCategoryTypeToString(cat).c_str());
  out_buffers->push_back(
      OutputBuffer::WithContents(Syntax::kHeading, category_title));

  // Registers.
  std::vector<std::vector<OutputBuffer>> rows;
  for (const auto reg_pair : found_registers) {
    rows.emplace_back();
    auto& row = rows.back();

    row.push_back(OutputBuffer::WithContents(reg_pair.second));
    row.push_back(OutputBuffer::WithContents(
        fxl::StringPrintf("%" PRIx64, reg_pair.first->size())));
    row.push_back(RegisterValueToOutputBuffer(*reg_pair.first));
  }

  out_buffers->push_back({});
  OutputBuffer& out = out_buffers->back();
  FormatTable(
      {ColSpec(Align::kLeft, 0, "Name"), ColSpec(Align::kRight, 0, "Size"),
       ColSpec(Align::kRight, 0, "Value", 2)},
      rows, &out);

  return Err();
}

}  // namespace

Err FormatRegisters(const RegisterSet& registers,
                    const std::string& searched_register, OutputBuffer* out,
                    std::vector<debug_ipc::RegisterCategory::Type> categories) {
  std::vector<OutputBuffer> out_buffers;
  const auto& register_map = registers.register_map();

  // If no category was set, print all of them.
  if (categories.empty()) {
    categories = {debug_ipc::RegisterCategory::Type::kGeneral,
                  debug_ipc::RegisterCategory::Type::kFloatingPoint,
                  debug_ipc::RegisterCategory::Type::kVector,
                  debug_ipc::RegisterCategory::Type::kMisc};
  }

  // Go category to category trying to print.
  for (const auto& category : categories) {
    auto it = register_map.find(category);
    if (it == register_map.end()) {
      continue;
    }
    Err err = InternalFormatCategory(category, it->second, searched_register,
                                     &out_buffers);
    if (err.has_error()) {
      return err;
    }
  }
  // If nothing was printed, it means that we couldn't find the register.
  if (out_buffers.empty()) {
    return Err(fxl::StringPrintf("Unknown register \"%s\"",
                                 searched_register.c_str()));
  }

  // Each section is separated by a new line.
  for (const auto& buf : out_buffers) {
    out->Append(buf);
    out->Append("\n");
  }

  return Err();
}

std::string RegisterCategoryTypeToString(debug_ipc::RegisterCategory::Type type) {
  switch (type) {
    case debug_ipc::RegisterCategory::Type::kGeneral:
      return "General Purpose";
    case debug_ipc::RegisterCategory::Type::kFloatingPoint:
      return "Floating Point";
    case debug_ipc::RegisterCategory::Type::kVector:
      return "Vector";
    case debug_ipc::RegisterCategory::Type::kMisc:
      return "Miscellaneous";
  }
}

std::string RegisterIDToString(RegisterID id) {
  switch (id) {
    // ARMv8 -------------------------------------------------------------------

    case RegisterID::ARMv8_x0: return "x0";
    case RegisterID::ARMv8_x1: return "x1";
    case RegisterID::ARMv8_x2: return "x2";
    case RegisterID::ARMv8_x3: return "x3";
    case RegisterID::ARMv8_x4: return "x4";
    case RegisterID::ARMv8_x5: return "x5";
    case RegisterID::ARMv8_x6: return "x6";
    case RegisterID::ARMv8_x7: return "x7";
    case RegisterID::ARMv8_x8: return "x8";
    case RegisterID::ARMv8_x9: return "x9";
    case RegisterID::ARMv8_x10: return "x10";
    case RegisterID::ARMv8_x11: return "x11";
    case RegisterID::ARMv8_x12: return "x12";
    case RegisterID::ARMv8_x13: return "x13";
    case RegisterID::ARMv8_x14: return "x14";
    case RegisterID::ARMv8_x15: return "x15";
    case RegisterID::ARMv8_x16: return "x16";
    case RegisterID::ARMv8_x17: return "x17";
    case RegisterID::ARMv8_x18: return "x18";
    case RegisterID::ARMv8_x19: return "x19";
    case RegisterID::ARMv8_x20: return "x20";
    case RegisterID::ARMv8_x21: return "x21";
    case RegisterID::ARMv8_x22: return "x22";
    case RegisterID::ARMv8_x23: return "x23";
    case RegisterID::ARMv8_x24: return "x24";
    case RegisterID::ARMv8_x25: return "x25";
    case RegisterID::ARMv8_x26: return "x26";
    case RegisterID::ARMv8_x27: return "x27";
    case RegisterID::ARMv8_x28: return "x28";
    case RegisterID::ARMv8_x29: return "x29";
    case RegisterID::ARMv8_lr: return "lr";
    case RegisterID::ARMv8_sp: return "sp";
    case RegisterID::ARMv8_pc: return "pc";
    case RegisterID::ARMv8_cpsr: return "cpsr";

    // x64 ---------------------------------------------------------------------

    case RegisterID::x64_rax: return "rax";
    case RegisterID::x64_rbx: return "rbx";
    case RegisterID::x64_rcx: return "rcx";
    case RegisterID::x64_rdx: return "rdx";
    case RegisterID::x64_rsi: return "rsi";
    case RegisterID::x64_rdi: return "rdi";
    case RegisterID::x64_rbp: return "rbp";
    case RegisterID::x64_rsp: return "rsp";
    case RegisterID::x64_r8: return "r8";
    case RegisterID::x64_r9: return "r9";
    case RegisterID::x64_r10: return "r10";
    case RegisterID::x64_r11: return "r11";
    case RegisterID::x64_r12: return "r12";
    case RegisterID::x64_r13: return "r13";
    case RegisterID::x64_r14: return "r14";
    case RegisterID::x64_r15: return "r15";
    case RegisterID::x64_rip: return "rip";
    case RegisterID::x64_rflags: return "rflags";
  }

  FXL_NOTREACHED();
  return std::string();
}

}   // namespace zxdb
