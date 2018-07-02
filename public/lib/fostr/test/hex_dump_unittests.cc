// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fostr/hex_dump.h"

#include <sstream>

#include "gtest/gtest.h"

namespace fostr {
namespace {

// Tests formatting hex dump of null pointer.
TEST(HexDump, Null) {
  std::ostringstream os;
  static const char* null = nullptr;

  // Actually passing nullptr (instead of null) doesn't work.
  os << Indent << "null:" << fostr::HexDump(null, 0, 0);

  EXPECT_EQ("null:<null>", os.str());
}

// Tests formatting hex dump of a zero-length buffer.
TEST(HexDump, ZeroLength) {
  std::ostringstream os;
  static const char buffer[] = "turducken";

  os << Indent << "empty:" << fostr::HexDump(buffer, 0, 0);

  EXPECT_EQ("empty:<zero bytes at 0000>", os.str());
}

// Tests formatting hex dump of a nominal buffer.
TEST(HexDump, Normal) {
  std::ostringstream os;
  static const char buffer[] = "turducken";

  os << Indent << "full:" << fostr::HexDump(buffer, sizeof(buffer), 0);

  EXPECT_EQ(
      "full:"
      "\n    0000  74 75 72 64 75 63 6b 65  6e 00                    "
      "turducken.      ",
      os.str());
}

// Tests formatting hex dump of a nominal buffer filling one line.
TEST(HexDump, NormalFillOneLine) {
  std::ostringstream os;
  static const char buffer[] = "cold turducken";

  os << Indent << "one full line:" << fostr::HexDump(buffer, sizeof(buffer), 0);

  EXPECT_EQ(
      "one full line:"
      "\n    0000  63 6f 6c 64 20 74 75 72  64 75 63 6b 65 6e 00     "
      "cold turducken. ",
      os.str());
}

// Tests formatting hex dump of a nominal buffer that fills more than one line.
TEST(HexDump, NormalMoreThanOneLine) {
  std::ostringstream os;
  static const char buffer[] = "cold turducken leftovers";

  os << Indent
     << "more than one line:" << fostr::HexDump(buffer, sizeof(buffer), 0);

  EXPECT_EQ(
      "more than one line:"
      "\n    0000  63 6f 6c 64 20 74 75 72  64 75 63 6b 65 6e 20 6c  "
      "cold turducken l"
      "\n    0010  65 66 74 6f 76 65 72 73  00                       "
      "eftovers.       ",
      os.str());
}

// Tests formatting hex dump of a nominal buffer with a non-zero initial
// address.
TEST(HexDump, InitialAddress) {
  std::ostringstream os;
  static const char buffer[] = "turducken";

  os << Indent << "full:" << fostr::HexDump(buffer, sizeof(buffer), 0x1234);

  EXPECT_EQ(
      "full:"
      "\n    1234  74 75 72 64 75 63 6b 65  6e 00                    "
      "turducken.      ",
      os.str());
}

// Tests formatting hex dump of a nominal buffer with a large initial address.
TEST(HexDump, LargeInitialAddress) {
  std::ostringstream os;
  static const char buffer[] = "turducken";

  os << Indent << "full:" << fostr::HexDump(buffer, sizeof(buffer), 0x12345);

  EXPECT_EQ(
      "full:"
      "\n    00012345  74 75 72 64 75 63 6b 65  6e 00                    "
      "turducken.      ",
      os.str());
}

// Tests formatting hex dump of a nominal buffer with a huge initial address.
TEST(HexDump, HugeInitialAddress) {
  std::ostringstream os;
  static const char buffer[] = "turducken";

  os << Indent
     << "full:" << fostr::HexDump(buffer, sizeof(buffer), 0x123456789);

  EXPECT_EQ(
      "full:"
      "\n    0000000123456789  74 75 72 64 75 63 6b 65  6e 00                 "
      "   turducken.      ",
      os.str());
}

// Tests formatting hex dump of a nominal buffer using the actual address of
// the buffer.
TEST(HexDump, ActualAddress) {
  std::ostringstream os;
  std::ostringstream os_expected;
  static const char buffer[] = "turducken";

  os << Indent << "full:" << fostr::HexDump(buffer, sizeof(buffer));

  size_t address = reinterpret_cast<size_t>(&buffer[0]);
  int address_width = 16;
  if (address < 0x10000) {
    address_width = 4;
  } else if (address < 0x100000000) {
    address_width = 8;
  }

  os_expected << "full:"
                 "\n    "
              << std::hex << std::setw(address_width) << std::setfill('0')
              << address << std::setfill(' ') << std::dec
              << "  74 75 72 64 75 63 6b 65  6e 00                    "
                 "turducken.      ";

  EXPECT_EQ(os_expected.str(), os.str());
}

}  // namespace
}  // namespace fostr
