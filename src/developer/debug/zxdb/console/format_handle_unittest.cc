// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/format_handle.h"

#include <gtest/gtest.h>

#include "src/developer/debug/ipc/records.h"

namespace zxdb {

TEST(FormatHandle, Table) {
  std::vector<debug_ipc::InfoHandleExtended> handles;

  // Empty case.
  OutputBuffer out = FormatHandles(handles, false);
  EXPECT_EQ("No handles.", out.AsString());

  // Give it two objects.
  handles.resize(2);
  handles[0].type = 2;  // Thread.
  handles[0].handle_value = 1234;
  handles[0].koid = 7890;

  handles[1].type = 1;  // Process.
  handles[1].handle_value = 1123;
  handles[1].koid = 7891;

  out = FormatHandles(handles, false);
  EXPECT_EQ(
      "  Handle  Type                 Koid\n"
      "    1234  ZX_OBJ_TYPE_THREAD   7890\n"
      "    1123  ZX_OBJ_TYPE_PROCESS  7891\n",
      out.AsString());

  // Hex formatting
  out = FormatHandles(handles, true);
  EXPECT_EQ(
      "  Handle  Type                   Koid\n"
      "   0x4d2  ZX_OBJ_TYPE_THREAD   0x1ed2\n"
      "   0x463  ZX_OBJ_TYPE_PROCESS  0x1ed3\n",
      out.AsString());
}

TEST(FormatHandle, BasicDetails) {
  debug_ipc::InfoHandleExtended handle;
  handle.type = 2;  // Thread.
  handle.handle_value = 1234;
  handle.rights = 3;
  handle.koid = 7890;
  handle.related_koid = 1111;
  handle.peer_owner_koid = 2222;

  OutputBuffer out = FormatHandle(handle, false);
  EXPECT_EQ(
      "             Type  ZX_OBJ_TYPE_THREAD\n"
      "            Value  1234\n"
      "           Rights  ZX_RIGHT_DUPLICATE\n"
      "                   ZX_RIGHT_TRANSFER\n"
      "             Koid  7890\n"
      "     Related koid  1111\n"
      "  Peer-owner koid  2222\n",
      out.AsString());

  // Related and peer owner koid should be omitted when 0 (not all handle types have these and
  // it looks confusing). This one also tests hex formatting.
  handle.related_koid = 0;
  handle.peer_owner_koid = 0;
  out = FormatHandle(handle, true);
  EXPECT_EQ(
      "    Type  ZX_OBJ_TYPE_THREAD\n"
      "   Value  0x4d2\n"
      "  Rights  ZX_RIGHT_DUPLICATE\n"
      "          ZX_RIGHT_TRANSFER\n"
      "    Koid  0x1ed2\n",
      out.AsString());
}

}  // namespace zxdb
