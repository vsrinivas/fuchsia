// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/format_handle.h"

#include "src/developer/debug/ipc/records.h"
#include "src/developer/debug/shared/handle_info.h"
#include "src/developer/debug/zxdb/common/string_util.h"
#include "src/developer/debug/zxdb/console/format_table.h"
#include "src/developer/debug/zxdb/console/string_util.h"

namespace zxdb {

namespace {

// Appends a two element string vector to the given output.
void AppendTwoEltRow(const std::string& a, const std::string& b,
                     std::vector<std::vector<std::string>>& rows) {
  auto& row = rows.emplace_back();
  row.push_back(a);
  row.push_back(b);
}

template <class T>
std::string NumToString(T value, bool hex) {
  if (hex)
    return to_hex_string(value);
  return std::to_string(value);
}

}  // namespace

OutputBuffer FormatHandles(const std::vector<debug_ipc::InfoHandleExtended>& handles, bool hex) {
  if (handles.empty())
    return OutputBuffer("No handles.");

  std::vector<std::vector<std::string>> rows;
  for (const auto& handle : handles) {
    auto& row = rows.emplace_back();
    row.push_back(NumToString(handle.handle_value, hex));
    row.push_back(debug_ipc::HandleTypeToString(handle.type));
    row.push_back(NumToString(handle.koid, hex));
  }

  OutputBuffer out;
  FormatTable({ColSpec(Align::kRight, 0, "Handle", 2), ColSpec(Align::kLeft, 0, "Type", 1),
               ColSpec(Align::kRight, 0, "Koid", 1)},
              rows, &out);
  return out;
}

OutputBuffer FormatHandle(const debug_ipc::InfoHandleExtended& handle, bool hex) {
  std::vector<std::vector<std::string>> rows;
  AppendTwoEltRow("Type", debug_ipc::HandleTypeToString(handle.type), rows);
  AppendTwoEltRow("Value", NumToString(handle.handle_value, hex), rows);

  // Put each right on a separate line.
  std::vector<std::string> rights = debug_ipc::HandleRightsToStrings(handle.rights);
  for (size_t i = 0; i < rights.size(); i++) {
    if (i == 0)
      AppendTwoEltRow("Rights", rights[i], rows);
    else
      AppendTwoEltRow(std::string(), rights[i], rows);
  }

  AppendTwoEltRow("Koid", NumToString(handle.koid, hex), rows);
  if (handle.related_koid)
    AppendTwoEltRow("Related koid", NumToString(handle.related_koid, hex), rows);
  if (handle.peer_owner_koid)
    AppendTwoEltRow("Peer-owner koid", NumToString(handle.peer_owner_koid, hex), rows);

  OutputBuffer out;
  FormatTable({ColSpec(Align::kRight, 0, std::string(), 2, Syntax::kHeading),
               ColSpec(Align::kLeft, 0, std::string(), 1)},
              rows, &out);
  return out;
}

}  // namespace zxdb
