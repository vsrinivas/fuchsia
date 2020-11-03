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

// Handle values can be 0 in the case of VMOs that are mapped but don't have open handles. This
// can be confusing so replace 0 with "<none>".
std::string HandleValueToString(uint64_t handle, bool hex) {
  if (handle == 0)
    return "<none>";
  return NumToString(handle, hex);
}

// Appends the given array of flags, one-per line, using the heading as the key for the first.
void AppendFlags(const std::string& heading, const std::vector<std::string>& flags,
                 std::vector<std::vector<std::string>>& rows) {
  for (size_t i = 0; i < flags.size(); i++) {
    if (i == 0)
      AppendTwoEltRow(heading, flags[i], rows);
    else
      AppendTwoEltRow(std::string(), flags[i], rows);
  }
}

void AppendVmoInfo(const debug_ipc::InfoHandleVmo& vmo, std::vector<std::vector<std::string>>& rows,
                   bool hex) {
  AppendTwoEltRow("Name", std::string(vmo.name, strnlen(vmo.name, std::size(vmo.name))), rows);
  AppendTwoEltRow("VMO size in bytes", NumToString(vmo.size_bytes, hex), rows);
  AppendTwoEltRow("Parent koid", NumToString(vmo.parent_koid, hex), rows);
  AppendTwoEltRow("# children", NumToString(vmo.num_children, hex), rows);
  AppendTwoEltRow("# mappings", NumToString(vmo.num_mappings, hex), rows);
  AppendTwoEltRow("Share count", NumToString(vmo.share_count, hex), rows);
  AppendFlags("Flags", debug_ipc::VmoFlagsToStrings(vmo.flags), rows);
  AppendTwoEltRow("Committed bytes", NumToString(vmo.committed_bytes, hex), rows);
  AppendTwoEltRow("Cache policy", debug_ipc::CachePolicyToString(vmo.cache_policy), rows);
  AppendTwoEltRow("Metadata bytes", NumToString(vmo.metadata_bytes, hex), rows);
  AppendTwoEltRow("Committed change events", NumToString(vmo.committed_change_events, hex), rows);
}

}  // namespace

OutputBuffer FormatHandles(const std::vector<debug_ipc::InfoHandle>& handles, bool hex) {
  if (handles.empty())
    return OutputBuffer("No handles.");

  std::vector<std::vector<std::string>> rows;
  for (const auto& handle : handles) {
    auto& row = rows.emplace_back();
    row.push_back(HandleValueToString(handle.handle_value, hex));
    row.push_back(debug_ipc::HandleTypeToString(handle.type));
    row.push_back(NumToString(handle.koid, hex));
  }

  OutputBuffer out;
  FormatTable({ColSpec(Align::kRight, 0, "Handle", 2), ColSpec(Align::kLeft, 0, "Type", 1),
               ColSpec(Align::kRight, 0, "Koid", 1)},
              rows, &out);
  return out;
}

OutputBuffer FormatHandle(const debug_ipc::InfoHandle& handle, bool hex) {
  std::vector<std::vector<std::string>> rows;
  AppendTwoEltRow("Handle", HandleValueToString(handle.handle_value, hex), rows);
  AppendTwoEltRow("Type", debug_ipc::HandleTypeToString(handle.type), rows);
  AppendTwoEltRow("Koid", NumToString(handle.koid, hex), rows);
  AppendFlags("Rights", debug_ipc::HandleRightsToStrings(handle.rights), rows);
  if (handle.related_koid)
    AppendTwoEltRow("Related koid", NumToString(handle.related_koid, hex), rows);
  if (handle.peer_owner_koid)
    AppendTwoEltRow("Peer-owner koid", NumToString(handle.peer_owner_koid, hex), rows);

  // Type-specific information.
  if (handle.type == 3u) {  // ZX_OBJ_TYPE_VMO == 3
    AppendVmoInfo(handle.ext.vmo, rows, hex);
  }

  OutputBuffer out;
  FormatTable({ColSpec(Align::kRight, 0, std::string(), 2, Syntax::kHeading),
               ColSpec(Align::kLeft, 0, std::string(), 1)},
              rows, &out);
  return out;
}

}  // namespace zxdb
