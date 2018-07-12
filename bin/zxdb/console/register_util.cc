// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>

#include <map>

#include "garnet/bin/zxdb/client/err.h"
#include "garnet/bin/zxdb/console/format_table.h"
#include "garnet/bin/zxdb/console/output_buffer.h"
#include "garnet/bin/zxdb/console/register_util.h"
#include "garnet/public/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

// Using a vector of output buffers make it easy to not have to worry about
// appending new lines per each new section.
Err InternalFormatRegisters(
    const std::vector<debug_ipc::RegisterCategory>& categories,
    const std::string& searched_register,
    std::vector<OutputBuffer>* out_buffers) {
  using CatToRegisters = std::map<const debug_ipc::RegisterCategory*,
                                  std::vector<const debug_ipc::Register*>>;
  CatToRegisters cat_to_registers;
  for (const auto& cat : categories) {

    std::vector<const debug_ipc::Register*> found_registers;
    for (const auto& reg : cat.registers) {
      if (searched_register.empty()) {
        found_registers.push_back(&reg);
      } else {
        // TODO(donosoc): Enable more permissive comparison.
        if (reg.name == searched_register) {
          found_registers.push_back(&reg);
          break;
        }
      }
    }

    // If this category didn't find registers, skip it.
    if (found_registers.empty()) {
      continue;
    }

    cat_to_registers[&cat] = std::move(found_registers);
  }

  // If we didn't find anything at all, this is an error.
  if (cat_to_registers.empty()) {
    return Err(fxl::StringPrintf("Unknown register \"%s\"",
                                 searched_register.c_str()));
  }


  for (const auto& kv : cat_to_registers) {
    // Title.
    auto category_title = fxl::StringPrintf(
        "%s Registers", RegisterCategoryTypeToString(kv.first->type).c_str());
    out_buffers->push_back(OutputBuffer::WithContents(Syntax::kHeading,
                                                      category_title));

    // Registers.
    std::vector<std::vector<OutputBuffer>> rows;
    for (const auto* reg : kv.second) {
      rows.emplace_back();
      auto& row = rows.back();

      row.push_back(OutputBuffer::WithContents(reg->name));
      auto val = fxl::StringPrintf("0x%016" PRIx64, reg->value);
      row.push_back(OutputBuffer::WithContents(val));
    }

    out_buffers->push_back({});
    OutputBuffer& out = out_buffers->back();
    FormatTable({ColSpec(Align::kRight, 0, "Name"),
                 ColSpec(Align::kLeft, 0, "Value", 2)},
                rows, &out);
  }

  return Err();
}

}   // namespace

Err FormatRegisters(const std::vector<debug_ipc::RegisterCategory>& categories,
                    const std::string& searched_register, OutputBuffer* out) {
  std::vector<OutputBuffer> out_buffers;
  Err err = InternalFormatRegisters(categories, searched_register, &out_buffers);
  if (err.has_error()) {
    return err;
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

}   // namespace zxdb
