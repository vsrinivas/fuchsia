// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/format_register.h"

#include <stdlib.h>

#include <algorithm>
#include <map>

#include "src/developer/debug/shared/regex.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/common/string_util.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/format_register_arm64.h"
#include "src/developer/debug/zxdb/console/format_register_x64.h"
#include "src/developer/debug/zxdb/console/format_table.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/console/string_formatters.h"
#include "src/developer/debug/zxdb/expr/eval_context_impl.h"
#include "src/developer/debug/zxdb/expr/format.h"
#include "src/developer/debug/zxdb/expr/format_options.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

using debug_ipc::Register;
using debug_ipc::RegisterCategory;
using debug_ipc::RegisterID;

namespace {

void FormatCategory(const FormatRegisterOptions& options, RegisterCategory category,
                    const std::vector<Register>& registers, OutputBuffer* out) {
  auto title = fxl::StringPrintf("%s Registers\n", debug_ipc::RegisterCategoryToString(category));
  out->Append(OutputBuffer(Syntax::kHeading, std::move(title)));

  if (registers.empty()) {
    out->Append("No registers to show in this category.");
    return;
  }

  // Check for architecture-specific printing.
  if (options.arch == debug_ipc::Arch::kX64) {
    if (FormatCategoryX64(options, category, registers, out))
      return;
  } else if (options.arch == debug_ipc::Arch::kArm64) {
    if (FormatCategoryARM64(options, category, registers, out))
      return;
  }

  // General formatting.
  FormatGeneralRegisters(registers, out);
}

}  // namespace

OutputBuffer FormatRegisters(const FormatRegisterOptions& options,
                             const std::vector<Register>& registers) {
  OutputBuffer out;

  // Group register by category.
  std::map<RegisterCategory, std::vector<Register>> categorized;
  for (const Register& reg : registers)
    categorized[RegisterIDToCategory(reg.id)].push_back(reg);

  for (auto& [category, cat_regs] : categorized) {
    // Ensure the registers appear in a consistent order.
    std::sort(cat_regs.begin(), cat_regs.end(), [](const Register& a, const Register& b) {
      return static_cast<uint32_t>(a.id) < static_cast<uint32_t>(b.id);
    });

    FormatCategory(options, category, cat_regs, &out);
    out.Append("\n");
  }
  return out;
}

void FormatGeneralRegisters(const std::vector<Register>& registers, OutputBuffer* out) {
  std::vector<std::vector<OutputBuffer>> rows;
  for (const Register& reg : registers) {
    auto color =
        rows.size() % 2 == 1 ? TextForegroundColor::kDefault : TextForegroundColor::kLightGray;
    rows.push_back(DescribeRegister(reg, color));
  }

  // Pad left by two spaces so the headings make more sense.
  FormatTable({ColSpec(Align::kRight, 0, std::string(), 2), ColSpec(Align::kRight), ColSpec()},
              rows, out);
}

void FormatGeneralVectorRegisters(const FormatRegisterOptions& options,
                                  const std::vector<debug_ipc::Register>& registers,
                                  OutputBuffer* out) {
  bool is_float = options.vector_format == VectorRegisterFormat::kFloat ||
                  options.vector_format == VectorRegisterFormat::kDouble;

  FormatOptions format_options;
  if (!is_float) {
    // Force padded hex output for all non-floating-point values.
    format_options.num_format = FormatOptions::NumFormat::kHex;
    format_options.zero_pad_hex = true;
  }

  // The formatter needs an eval context but we don't need it to have any capabilities.
  auto eval_context = fxl::MakeRefCounted<EvalContextImpl>(
      fxl::WeakPtr<const ProcessSymbols>(), fxl::RefPtr<SymbolDataProvider>(), Location());

  // Largest number of vector elements of all registers.
  size_t max_children = 0;

  // Convert each register to a FormatNode. It will have each vector element as the children.
  std::vector<std::unique_ptr<FormatNode>> formatted;
  for (const auto& r : registers) {
    // Use the expression formatter to format the vector members.
    ExprValue vector_value = VectorRegisterToValue(r.id, options.vector_format, r.data);
    auto node = std::make_unique<FormatNode>(RegisterIDToString(r.id), std::move(vector_value));

    // In general formatting is asynchronous but a vector of numbers should always be completable
    // synchronously.
    bool completed = false;
    FillFormatNodeDescription(node.get(), format_options, eval_context,
                              fit::defer_callback([&completed]() { completed = true; }));
    FXL_DCHECK(completed);

    max_children = std::max(max_children, node->children().size());
    formatted.push_back(std::move(node));
  }

  // Convert the formatted registers to a table.
  std::vector<std::vector<OutputBuffer>> rows;
  for (auto& node : formatted) {
    // Each row is the name + the children elements.
    std::vector<OutputBuffer>& row = rows.emplace_back();
    row.resize(max_children + 1);

    auto color =
        rows.size() % 2 == 1 ? TextForegroundColor::kLightGray : TextForegroundColor::kDefault;
    row[0] = OutputBuffer(node->name(), color);  // Register name.

    // Add each child to the row.
    for (size_t i = 0; i < node->children().size(); i++) {
      bool completed = false;
      FillFormatNodeDescription(node->children()[i].get(), format_options, eval_context,
                                fit::defer_callback([&completed]() { completed = true; }));
      FXL_DCHECK(completed);

      // The table is filled with the low index on the right.
      row[row.size() - i - 1] = OutputBuffer(node->children()[i]->description(), color);
    }
  }

  std::vector<ColSpec> spec;
  spec.emplace_back(Align::kRight, 0, "Name", 2);
  for (size_t i = 0; i < max_children; i++)
    spec.emplace_back(Align::kRight, 0, fxl::StringPrintf("[%zu]", max_children - i - 1));

  FormatTable(spec, rows, out);

  out->Append(Syntax::kComment,
              fxl::StringPrintf(
                  "    (Use \"get/set vector-format\" to control vector register intepretation.\n"
                  "     Currently showing vectors of \"%s\".)\n",
                  VectorRegisterFormatToString(options.vector_format)));
}

std::vector<OutputBuffer> DescribeRegister(const Register& reg, TextForegroundColor color) {
  std::vector<OutputBuffer> result;
  result.emplace_back(RegisterIDToString(reg.id), color);

  if (reg.data.size() <= 8) {
    // Treat <= 64 bit registers as numbers.
    uint64_t value = static_cast<uint64_t>(reg.GetValue());
    result.emplace_back(to_hex_string(value), color);

    // For plausible small integers, show the decimal value also. This size check is intended to
    // avoid cluttering up the results with large numbers corresponding to pointers.
    constexpr uint64_t kMaxSmallMagnitude = 0xffff;
    if (value <= kMaxSmallMagnitude || llabs(static_cast<long long int>(value)) <=
                                           static_cast<long long int>(kMaxSmallMagnitude)) {
      result.emplace_back(fxl::StringPrintf("= %d", static_cast<int>(value)), color);
    } else {
      result.emplace_back();
    }
  } else {
    // Assume anything bigger than 64 bits is a vector and print with grouping.
    result.emplace_back(GetLittleEndianHexOutput(reg.data));
  }

  return result;
}

}  // namespace zxdb
