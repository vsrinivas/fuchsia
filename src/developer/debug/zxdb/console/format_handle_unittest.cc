// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/format_handle.h"

#include <gtest/gtest.h>

#include "src/developer/debug/ipc/records.h"

namespace zxdb {

TEST(FormatHandle, Table) {
  std::vector<debug_ipc::InfoHandle> handles;

  // Empty case.
  OutputBuffer out = FormatHandles(handles, false);
  EXPECT_EQ("No handles.", out.AsString());

  // Give it several objects.
  handles.resize(3);
  handles[0].type = 3;          // VMO.
  handles[0].handle_value = 0;  // Non-handle-based VMO.
  handles[0].koid = 9999;

  handles[1].type = 2;  // Thread.
  handles[1].handle_value = 1234;
  handles[1].koid = 7890;

  handles[2].type = 1;  // Process.
  handles[2].handle_value = 1123;
  handles[2].koid = 7891;

  out = FormatHandles(handles, false);
  EXPECT_EQ(
      "  Handle  Type                 Koid\n"
      "  <none>  ZX_OBJ_TYPE_VMO      9999\n"
      "    1234  ZX_OBJ_TYPE_THREAD   7890\n"
      "    1123  ZX_OBJ_TYPE_PROCESS  7891\n",
      out.AsString());

  // Hex formatting
  out = FormatHandles(handles, true);
  EXPECT_EQ(
      "  Handle  Type                   Koid\n"
      "  <none>  ZX_OBJ_TYPE_VMO      0x270f\n"
      "   0x4d2  ZX_OBJ_TYPE_THREAD   0x1ed2\n"
      "   0x463  ZX_OBJ_TYPE_PROCESS  0x1ed3\n",
      out.AsString());
}

TEST(FormatHandle, BasicDetails) {
  debug_ipc::InfoHandle handle;
  handle.type = 2;  // Thread.
  handle.handle_value = 1234;
  handle.rights = 3;
  handle.koid = 7890;
  handle.related_koid = 1111;
  handle.peer_owner_koid = 2222;

  OutputBuffer out = FormatHandle(handle, false);
  EXPECT_EQ(
      "           Handle  1234\n"
      "             Type  ZX_OBJ_TYPE_THREAD\n"
      "             Koid  7890\n"
      "           Rights  ZX_RIGHT_DUPLICATE\n"
      "                   ZX_RIGHT_TRANSFER\n"
      "     Related koid  1111\n"
      "  Peer-owner koid  2222\n",
      out.AsString());

  // Related and peer owner koid should be omitted when 0 (not all handle types have these and
  // it looks confusing). This one also tests hex formatting.
  handle.related_koid = 0;
  handle.peer_owner_koid = 0;
  out = FormatHandle(handle, true);
  EXPECT_EQ(
      "  Handle  0x4d2\n"
      "    Type  ZX_OBJ_TYPE_THREAD\n"
      "    Koid  0x1ed2\n"
      "  Rights  ZX_RIGHT_DUPLICATE\n"
      "          ZX_RIGHT_TRANSFER\n",
      out.AsString());
}

TEST(FormatHandle, VmoDetails) {
  debug_ipc::InfoHandle handle;
  handle.type = 3;  // VMO.
  handle.handle_value = 1234;
  handle.rights = 3;
  handle.koid = 7890;
  handle.related_koid = 0;
  handle.peer_owner_koid = 0;

  // VMO-specific data.
  auto& vmo = handle.ext.vmo;
  strcpy(vmo.name, "my name");
  vmo.size_bytes = 262144;
  vmo.parent_koid = 8888;
  vmo.num_children = 0;
  vmo.num_mappings = 1;
  vmo.share_count = 2;
  vmo.flags = 3;
  vmo.committed_bytes = 8192;
  vmo.cache_policy = 0;
  vmo.metadata_bytes = 176;
  vmo.committed_change_events = 0;

  OutputBuffer out = FormatHandle(handle, false);
  EXPECT_EQ(
      "                   Handle  1234\n"
      "                     Type  ZX_OBJ_TYPE_VMO\n"
      "                     Koid  7890\n"
      "                   Rights  ZX_RIGHT_DUPLICATE\n"
      "                           ZX_RIGHT_TRANSFER\n"
      "                     Name  my name\n"
      "        VMO size in bytes  262144\n"
      "              Parent koid  8888\n"
      "               # children  0\n"
      "               # mappings  1\n"
      "              Share count  2\n"
      "                    Flags  ZX_INFO_VMO_TYPE_PAGED\n"
      "                           ZX_INFO_VMO_RESIZABLE\n"
      "          Committed bytes  8192\n"
      "             Cache policy  ZX_CACHE_POLICY_CACHED\n"
      "           Metadata bytes  176\n"
      "  Committed change events  0\n",
      out.AsString());
}

}  // namespace zxdb
