// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/format_memory.h"

#include <limits>

#include "gtest/gtest.h"
#include "src/developer/debug/zxdb/client/memory_dump.h"

namespace zxdb {

TEST(MemoryFormat, Simple) {
  std::vector<debug_ipc::MemoryBlock> block;
  block.resize(1);
  block[0].address = 0x1000;
  block[0].valid = true;
  block[0].size = 0x1000;
  for (uint64_t i = 0; i < block[0].size; i++) {
    block[0].data.push_back(static_cast<uint8_t>(i % 0x100));
  }

  MemoryDump dump(std::move(block));

  MemoryFormatOptions opts;

  // Simple 2-line output with no addresses or ascii.
  std::string output = FormatMemory(dump, 0x1000, 0x20, opts);
  char expected1[] =
      "00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f\n"
      "10 11 12 13 14 15 16 17 18 19 1a 1b 1c 1d 1e 1f\n";
  EXPECT_EQ(expected1, output);

  // 1 and a half lines with ascii, separator every 8.
  opts.show_ascii = true;
  opts.separator_every = 8;
  output = FormatMemory(dump, 0x1000, 0x18, opts);
  char expected2[] =
      "00 01 02 03 04 05 06 07-08 09 0a 0b 0c 0d 0e 0f  |                \n"
      "10 11 12 13 14 15 16 17                          |                \n";
  EXPECT_EQ(expected2, output);

  // With addresses and printable ASCII.
  opts.show_addrs = true;
  output = FormatMemory(dump, 0x1010, 0x20, opts);
  char expected3[] =
      "0x1010:  10 11 12 13 14 15 16 17-18 19 1a 1b 1c 1d 1e 1f  |             "
      "  "
      " \n"
      "0x1020:  20 21 22 23 24 25 26 27-28 29 2a 2b 2c 2d 2e 2f  | "
      "!\"#$%&'()*+,-./\n";
  EXPECT_EQ(expected3, output);

  // Out-of-block bytes, addresses should be padded to the same length.
  opts.show_addrs = true;
  opts.show_ascii = false;
  output = FormatMemory(dump, 0xF0, 0x20, opts);
  char expected4[] =
      "0x0f0:  ?? ?? ?? ?? ?? ?? ?? ??"
      "-?? ?? ?? ?? ?? ?? ?? ??\n"
      "0x100:  ?? ?? ?? ?? ?? ?? ?? ??"
      "-?? ?? ?? ?? ?? ?? ?? ??\n";
  EXPECT_EQ(expected4, output);

  // Non-aligned start offset, crosses valid/invalid boundary, weird separator
  // width.
  opts.show_addrs = true;
  opts.show_ascii = true;
  opts.separator_every = 5;
  output = FormatMemory(dump, 0xFFA, 0x19, opts);
  char expected5[] =
      "0x0ffa:  ?? ?? ?? ?? ??"
      "-?? 00 01 02 03-04 05 06 07 08-09  |               "
      " \n"
      "0x100a:  0a 0b 0c 0d 0e-0f 10 11 12                       |             "
      "  "
      " \n";
  EXPECT_EQ(expected5, output);

  // Weird column width, separator every time.
  opts.show_addrs = true;
  opts.show_ascii = true;
  opts.values_per_line = 3;
  opts.separator_every = 1;
  output = FormatMemory(dump, 0x1000, 10, opts);
  char expected6[] =
      "0x1000:  00-01-02  |   \n"
      "0x1003:  03-04-05  |   \n"
      "0x1006:  06-07-08  |   \n"
      "0x1009:  09        |   \n";
  EXPECT_EQ(expected6, output);
}

TEST(MemoryFormat, Limits) {
  uint64_t max = std::numeric_limits<uint64_t>::max();

  // This block goes right up to the edge of the 64-bit address space.
  std::vector<debug_ipc::MemoryBlock> block;
  block.resize(1);
  block[0].size = 0x1000;
  block[0].address = max - block[0].size + 1;
  block[0].valid = true;
  // This one has the same data to make things simpler below.
  for (uint64_t i = 0; i < block[0].size; i++) {
    block[0].data.push_back(0x11);
  }

  MemoryDump dump(std::move(block));

  MemoryFormatOptions opts;
  opts.show_addrs = true;

  // Simple 2-line output with no addresses or ascii against end of address
  // space.
  std::string output = FormatMemory(dump, max - 0x1F, 0x20, opts);
  char expected1[] =
      "0xffffffffffffffe0:  11 11 11 11 11 11 11 11 11 11 11 11 11 11 11 11\n"
      "0xfffffffffffffff0:  11 11 11 11 11 11 11 11 11 11 11 11 11 11 11 11\n";
  EXPECT_EQ(expected1, output);

  // Asking for data past the end of the address space should just stop output.
  output = FormatMemory(dump, max - 0xF, 0x20, opts);
  char expected2[] =
      "0xfffffffffffffff0:  11 11 11 11 11 11 11 11 11 11 11 11 11 11 11 11\n";
  EXPECT_EQ(expected2, output);
}

}  // namespace zxdb
