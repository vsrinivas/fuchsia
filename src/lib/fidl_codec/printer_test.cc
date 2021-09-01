// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/fidl_codec/printer.h"

#include <sstream>

#include <gtest/gtest.h>

namespace fidl_codec {

TEST(PrettyPrinter, Uint64Print) {
  std::stringstream out;
  PrettyPrinter printer(out, WithoutColors, false, "", 100, false);
  // We use variables to get the proper type to <<.
  constexpr uint64_t n = 255;
  constexpr uint64_t zero = 0;
  constexpr uint64_t sixteen = 16;
  constexpr uint64_t ten = 10;
  ASSERT_EQ(printer.remaining_size(), 100U);
  printer << n;
  ASSERT_EQ(printer.remaining_size(), 97U);
  printer << zero;
  ASSERT_EQ(printer.remaining_size(), 96U);
  printer << std::hex << n;
  ASSERT_EQ(printer.remaining_size(), 94U);
  printer << zero;
  ASSERT_EQ(printer.remaining_size(), 93U);
  printer << sixteen;
  ASSERT_EQ(printer.remaining_size(), 91U);
  printer << std::dec << ten;
  ASSERT_EQ(printer.remaining_size(), 89U);
  ASSERT_EQ(out.str(), "2550ff01010");
}

TEST(PrettyPrinter, OpenRightPrint) {
  std::stringstream out;
  PrettyPrinter printer(out, WithoutColors, false, "", 100, false);
  printer.DisplayDirectoryOpenFlags(0x7ffffff);
  out << '\n';
  printer.DisplayDirectoryOpenFlags(0);
  out << '\n';
  ASSERT_EQ(out.str(),
            "OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITEABLE | OPEN_RIGHT_ADMIN | "
            "OPEN_RIGHT_EXECUTABLE | OPEN_FLAG_CREATE | OPEN_FLAG_CREATE_IF_ABSENT | "
            "OPEN_FLAG_TRUNCATE | OPEN_FLAG_DIRECTORY | OPEN_FLAG_APPEND | OPEN_FLAG_NO_REMOTE | "
            "OPEN_FLAG_NODE_REFERENCE | OPEN_FLAG_DESCRIBE | OPEN_FLAG_POSIX | "
            "CLONE_FLAGS_SAME_RIGHTS | 0x200fff0\n"
            "0\n");
}

TEST(PrettyPrinter, OpenModePrint) {
  std::stringstream out;
  PrettyPrinter printer(out, WithoutColors, false, "", 100, false);
  printer.DisplayDirectoryOpenMode(0x10fff);
  out << '\n';
  printer.DisplayDirectoryOpenMode(0xc000);
  out << '\n';
  printer.DisplayDirectoryOpenMode(0x8000);
  out << '\n';
  printer.DisplayDirectoryOpenMode(0x6000);
  out << '\n';
  printer.DisplayDirectoryOpenMode(0x4000);
  out << '\n';
  printer.DisplayDirectoryOpenMode(0);
  out << '\n';
  ASSERT_EQ(out.str(),
            "MODE_TYPE_SERVICE | S_ISUID | S_ISGID | S_IRWXU | S_IRWXG | S_IRWXO | 0x200\n"
            "MODE_TYPE_SOCKET\n"
            "MODE_TYPE_FILE\n"
            "MODE_TYPE_BLOCK_DEVICE\n"
            "MODE_TYPE_DIRECTORY\n"
            "0\n");
}

}  // namespace fidl_codec
