// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/extractor/cpp/hex_dump_generator.h"

#include <lib/cksum.h>
#include <lib/zx/result.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <zircon/errors.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#include <fbl/unique_fd.h>
#include <gtest/gtest.h>

namespace extractor {
namespace {

constexpr size_t kBytesPerLine = 64;
const std::array<uint8_t, kBytesPerLine> kData1{
    {0x16, 0xc3, 0xb8, 0x72, 0x0e, 0x76, 0xfc, 0x0f, 0x2c, 0xad, 0xb6, 0x7b, 0x80,
     0xd9, 0x44, 0xa9, 0xe7, 0x92, 0x30, 0x3a, 0x22, 0xc8, 0xb6, 0xfe, 0x50, 0xe4,
     0xb5, 0x1e, 0xcc, 0xa1, 0x20, 0xe1, 0xef, 0xb7, 0x81, 0x03, 0x3e, 0x5a, 0x9a,
     0x65, 0x3d, 0xbf, 0x69, 0x82, 0xf4, 0xa5, 0xaa, 0x05, 0x33, 0x7a, 0x29, 0x4f,
     0x9d, 0xfc, 0x85, 0x9c, 0x50, 0xfa, 0x80, 0x9d, 0x81, 0xdd, 0x53, 0xc4}};
const std::string kLine1 = std::string(
    "16c3b8720e76fc0f2cadb67b80d944a9e792303a22c8b6fe50e4b51ecca120e1efb781033e5a9a653dbf6982f4a5"
    "aa05337a294f9dfc859c50fa809d81dd53c4\n");

const std::array<uint8_t, kBytesPerLine> kData2{
    {0x73, 0xf1, 0xc4, 0x56, 0xea, 0xab, 0xc1, 0xed, 0x40, 0x5b, 0x40, 0x66, 0x9a,
     0xe1, 0x9d, 0xc6, 0x74, 0xac, 0x79, 0x53, 0xed, 0xda, 0xa3, 0x38, 0x32, 0xcc,
     0x14, 0x32, 0x58, 0x5b, 0x97, 0x1d, 0x6c, 0x40, 0xaf, 0x03, 0xca, 0x99, 0x75,
     0x83, 0x3f, 0x98, 0x64, 0x8f, 0x56, 0x45, 0x79, 0xb7, 0x5a, 0x1c, 0xfe, 0x32,
     0xe8, 0x2d, 0xe3, 0x05, 0x3d, 0xe0, 0x8a, 0x83, 0x01, 0x27, 0xe9, 0xf2}};
const std::string kLine2 = std::string(
    "73f1c456eaabc1ed405b40669ae19dc674ac7953eddaa33832cc1432585b971d6c40af03ca9975833f98648f5645"
    "79b75a1cfe32e82de3053de08a830127e9f2\n");

fbl::unique_fd setup_fd(const std::vector<uint8_t>& buffer) {
  char kInFile[20] = "/tmp/payload.XXXXXX";

  fbl::unique_fd in_file(mkstemp(kInFile));
  EXPECT_TRUE(in_file);
  EXPECT_EQ(write(in_file.get(), buffer.data(), buffer.size()),
            static_cast<ssize_t>(buffer.size()));
  return in_file;
}

const std::string kTag = std::string("hello");

TEST(HexDumpGenerator, InvalidArgs) {
  const HexDumpGeneratorOptions kOptions = {
      .bytes_per_line = 0,
      .dump_offset = true,
      .dump_checksum = true,
  };
  fbl::unique_fd fd;
  auto hex = HexDumpGenerator::Create(std::move(fd), kOptions);
  ASSERT_EQ(hex.error_value(), ZX_ERR_INVALID_ARGS);
  std::vector<uint8_t> data(kData1.cbegin(), kData1.cend());
  auto hex2 = HexDumpGenerator::Create(setup_fd(data), kOptions);
  ASSERT_EQ(hex2.error_value(), ZX_ERR_INVALID_ARGS);
}

TEST(HexDumpGenerator, ZeroSizeFile) {
  const HexDumpGeneratorOptions kOptions = {
      .tag = kTag,
      .bytes_per_line = kBytesPerLine,
      .dump_offset = true,
      .dump_checksum = true,
  };
  std::vector<uint8_t> buffer;
  auto hex = HexDumpGenerator::Create(setup_fd(buffer), kOptions);
  ASSERT_EQ(hex->GetNextLine().error_value(), ZX_ERR_STOP);
  ASSERT_EQ(hex->GetNextLine().error_value(), ZX_ERR_STOP);
}

TEST(HexDumpGenerator, OneLineFile) {
  std::vector<uint8_t> data(kData1.cbegin(), kData1.cend());
  const HexDumpGeneratorOptions kOptions = {
      .tag = "",
      .bytes_per_line = kBytesPerLine,
      .dump_offset = false,
      .dump_checksum = false,
  };

  auto hex = HexDumpGenerator::Create(setup_fd(data), kOptions);

  auto result_line = hex->GetNextLine().value();
  ASSERT_EQ(kLine1, result_line);
  ASSERT_EQ(hex->GetNextLine().error_value(), ZX_ERR_STOP);
}

TEST(HexDumpGenerator, OneAndHalfLineFile) {
  const HexDumpGeneratorOptions kOptions = {
      .tag = "",
      .bytes_per_line = kBytesPerLine,
      .dump_offset = false,
      .dump_checksum = false,
  };
  std::vector<uint8_t> data(kData1.cbegin(), kData1.cend());
  data.insert(data.end(), kData2.cbegin(), kData2.cbegin() + (sizeof(kData2) / 2));

  auto hex = HexDumpGenerator::Create(setup_fd(data), kOptions);

  ASSERT_EQ(kLine1, hex->GetNextLine().value());
  ASSERT_EQ(kLine2.substr(0, kLine2.size() / 2) + "\n", hex->GetNextLine().value());
  ASSERT_EQ(hex->GetNextLine().error_value(), ZX_ERR_STOP);
}

TEST(HexDumpGenerator, WithOffset) {
  std::vector<uint8_t> data(kData1.cbegin(), kData1.cend());
  const HexDumpGeneratorOptions kOptions = {
      .tag = "",
      .bytes_per_line = kBytesPerLine,
      .dump_offset = true,
      .dump_checksum = false,
  };

  auto hex = HexDumpGenerator::Create(setup_fd(data), kOptions);

  auto result_line = hex->GetNextLine().value();
  ASSERT_EQ("0-" + std::to_string(kBytesPerLine - 1) + ":" + kLine1, result_line);
  ASSERT_EQ(hex->GetNextLine().error_value(), ZX_ERR_STOP);
}

TEST(HexDumpGenerator, WithChecksum) {
  std::vector<uint8_t> data(kData1.cbegin(), kData1.cend());
  const HexDumpGeneratorOptions kOptions = {
      .tag = "",
      .bytes_per_line = kBytesPerLine,
      .dump_offset = false,
      .dump_checksum = true,
  };

  auto hex = HexDumpGenerator::Create(setup_fd(data), kOptions);

  auto result_line = hex->GetNextLine().value();
  ASSERT_EQ(kLine1 + "checksum: " + std::to_string(crc32(0, data.data(), data.size())) + "\n",
            result_line);
  ASSERT_EQ(hex->GetNextLine().error_value(), ZX_ERR_STOP);
}

std::string BuildLine(const std::string& hex_string, size_t start, size_t end, bool dump_offset,
                      const std::string& tag) {
  std::stringstream line;

  if (tag.length() > 0) {
    line << tag << " ";
  }
  if (dump_offset) {
    line << start << "-" << end << ":";
  }
  line << hex_string;

  return line.str();
}

std::string BuildChecksumLine(const std::vector<uint8_t>& data, bool dump_checksum,
                              bool dump_offset, const std::string& tag) {
  std::stringstream line;
  if (!dump_checksum) {
    return line.str();
  }

  if (tag.length() > 0) {
    line << tag << " ";
  }
  if (dump_offset) {
    line << 0 << "-" << data.size() - 1 << ":";
  }
  line << "checksum: " << crc32(0, data.data(), data.size()) << std::endl;

  return line.str();
}

void DuplicateLineTestHelper(size_t line_count, bool test_duplicate_lines, size_t duplicate_start,
                             size_t duplicate_end) {
  const HexDumpGeneratorOptions kOptions = {
      .tag = kTag,
      .bytes_per_line = kBytesPerLine,
      .dump_offset = true,
      .dump_checksum = true,
  };
  std::vector<uint8_t> data;
  std::stringstream lines;
  const std::array<uint8_t, kBytesPerLine>* current_data = nullptr;
  for (size_t i = 0; i < line_count; i++) {
    auto start = i * kBytesPerLine;
    auto end = start + kBytesPerLine - 1;

    if (test_duplicate_lines && i > duplicate_start && i <= duplicate_end) {
      if (i == duplicate_end) {
        lines << BuildLine("*\n", (duplicate_start + 1) * kBytesPerLine,
                           ((duplicate_end + 1) * kBytesPerLine) - 1, kOptions.dump_offset,
                           kOptions.tag);
      }
    } else if (i % 2 == 0) {
      current_data = &kData1;
      lines << BuildLine(kLine1, start, end, kOptions.dump_offset, kOptions.tag);
    } else {
      current_data = &kData2;
      lines << BuildLine(kLine2, start, end, kOptions.dump_offset, kOptions.tag);
    }
    data.insert(data.end(), current_data->cbegin(), current_data->cend());
  }

  lines << BuildChecksumLine(data, kOptions.dump_checksum, kOptions.dump_offset, kOptions.tag);

  auto hex = HexDumpGenerator::Create(setup_fd(data), kOptions);
  std::stringstream found_lines;
  while (true) {
    auto line_or = hex->GetNextLine();
    if (line_or.is_error()) {
      ASSERT_EQ(line_or.error_value(), ZX_ERR_STOP);
      break;
    }
    found_lines << line_or.value();
  }

  ASSERT_EQ(lines.str(), found_lines.str());
}

TEST(HexDumpGenerator, MultipleUniqueLines) { DuplicateLineTestHelper(10, false, 0, 0); }

TEST(HexDumpGenerator, WithDuplicateLinesInBeginning) { DuplicateLineTestHelper(10, true, 0, 6); }

TEST(HexDumpGenerator, WithDuplicateLinesInTheMiddle) { DuplicateLineTestHelper(10, true, 6, 8); }

TEST(HexDumpGenerator, WithDuplicateLinesInTheEnd) { DuplicateLineTestHelper(10, true, 7, 9); }

}  // namespace
}  // namespace extractor
