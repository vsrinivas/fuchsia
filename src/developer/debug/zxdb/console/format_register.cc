// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/format_register.h"

#include <inttypes.h>
#include <stdlib.h>

#include <map>

#include "src/developer/debug/shared/regex.h"
#include "src/developer/debug/zxdb/client/register.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/format_register_arm64.h"
#include "src/developer/debug/zxdb/console/format_register_x64.h"
#include "src/developer/debug/zxdb/console/format_table.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/console/string_formatters.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

using debug_ipc::RegisterCategory;
using debug_ipc::RegisterID;

namespace {

void InternalFormatGeneric(const std::vector<Register>& registers,
                           OutputBuffer* out) {
  std::vector<std::vector<OutputBuffer>> rows;
  for (const Register& reg : registers) {
    auto color = rows.size() % 2 == 1 ? TextForegroundColor::kDefault
                                      : TextForegroundColor::kLightGray;
    rows.push_back(DescribeRegister(reg, color));
  }

  // Pad left by two spaces so the headings make more sense.
  FormatTable({ColSpec(Align::kRight, 0, std::string(), 2),
               ColSpec(Align::kRight), ColSpec()},
              rows, out);
}

Err FormatCategory(const FormatRegisterOptions& options,
                   RegisterCategory::Type category,
                   const std::vector<Register>& registers, OutputBuffer* out) {
  auto title = fxl::StringPrintf(
      "%s Registers\n", debug_ipc::RegisterCategory::TypeToString(category));
  out->Append(OutputBuffer(Syntax::kHeading, std::move(title)));

  if (registers.empty()) {
    out->Append("No registers to show in this category.");
    return Err();
  }

  // We see if architecture specific printing wants to take over.
  Err err;
  OutputBuffer category_out;
  if (options.arch == debug_ipc::Arch::kX64) {
    if (FormatCategoryX64(options, category, registers, &category_out, &err)) {
      if (err.ok())
        out->Append(std::move(category_out));
      return err;
    }
  } else if (options.arch == debug_ipc::Arch::kArm64) {
    if (FormatCategoryARM64(options, category, registers, &category_out,
                            &err)) {
      if (err.ok())
        out->Append(std::move(category_out));
      return err;
    }
  }

  // Generic case.
  InternalFormatGeneric(registers, &category_out);

  out->Append(std::move(category_out));
  return Err();
}

}  // namespace

Err FilterRegisters(const FormatRegisterOptions& options,
                    const RegisterSet& register_set, FilteredRegisterSet* out) {
  const auto& category_map = register_set.category_map();
  // Used to track how many registers we found when filtering.
  int registers_found = 0;
  for (const auto& category : options.categories) {
    auto it = category_map.find(category);
    if (it == category_map.end())
      continue;

    out->insert({category, {}});
    (*out)[category] = {};
    auto& registers = (*out)[category];

    if (options.filter_regexp.empty()) {
      // Add all registers.
      registers.reserve(it->second.size());
      for (const auto& reg : it->second) {
        registers.emplace_back(reg);
        registers_found++;
      }
    } else {
      // We use insensitive case regexp matching.
      debug_ipc::Regex regex;
      if (!regex.Init(options.filter_regexp)) {
        return Err("Could not initialize regex %s.",
                   options.filter_regexp.c_str());
      }

      for (const auto& reg : it->second) {
        const char* reg_name = RegisterIDToString(reg.id());
        if (regex.Match(reg_name)) {
          registers.push_back(reg);
          registers_found++;
        }
      }
    }
  }

  if (registers_found == 0) {
    if (options.filter_regexp.empty()) {
      return Err("Could not find registers in the selected categories");
    } else {
      return Err(
          "Could not find any registers that match \"%s\" in the selected "
          "categories",
          options.filter_regexp.data());
    }
  }

  return Err();
}

Err FormatRegisters(const FormatRegisterOptions& options,
                    const FilteredRegisterSet& filtered_set,
                    OutputBuffer* out) {
  // We should have detected on the filtering stage that we didn't find any
  // register.
  FXL_DCHECK(!filtered_set.empty());

  std::vector<OutputBuffer> out_buffers;
  out_buffers.reserve(filtered_set.size());
  for (auto kv : filtered_set) {
    if (kv.second.empty())
      continue;
    OutputBuffer cat_out;
    Err err = FormatCategory(options, kv.first, kv.second, &cat_out);
    if (!err.ok())
      return err;
    out_buffers.emplace_back(std::move(cat_out));
  }

  // Each section is separated by a new line.
  for (const auto& buf : out_buffers) {
    out->Append(std::move(buf));
    out->Append("\n");
  }
  return Err();
}

// Formatting helpers ----------------------------------------------------------

std::vector<OutputBuffer> DescribeRegister(const Register& reg,
                                           TextForegroundColor color) {
  std::vector<OutputBuffer> result;
  result.emplace_back(RegisterIDToString(reg.id()), color);

  if (reg.size() <= 8) {
    // Treat <= 64 bit registers as numbers.
    uint64_t value = reg.GetValue();
    result.emplace_back(fxl::StringPrintf("0x%" PRIx64, value), color);

    // For plausible small integers, show the decimal value also. This size
    // check is intended to avoid cluttering up the results with large numbers
    // corresponding to pointers.
    constexpr uint64_t kMaxSmallMagnitude = 0xffff;
    if (value <= kMaxSmallMagnitude ||
        llabs(static_cast<long long int>(value)) <=
            static_cast<long long int>(kMaxSmallMagnitude)) {
      result.emplace_back(fxl::StringPrintf("= %d", static_cast<int>(value)),
                          color);
    } else {
      result.emplace_back();
    }
  } else {
    // Assume anything bigger than 64 bits is a vector and print with grouping.
    std::string hex_out;
    Err err = GetLittleEndianHexOutput(reg.data(), &hex_out);
    if (!err.ok())
      result.emplace_back(err.msg(), color);
    else
      result.emplace_back(std::move(hex_out), color);
  }

  return result;
}

}  // namespace zxdb
