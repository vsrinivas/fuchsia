// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/sized_data_reader.h"

#include <gtest/gtest.h>

#include "src/lib/files/file.h"
#include "third_party/crashpad/util/file/string_file.h"

namespace forensics {
namespace crash_reports {
namespace {

const std::string kLargeData = R"(This is an example of a very large string
that may occupy a file read by sized_data.

  A large string that has a diverse set of characters in it, but alas it doesn't
contain any raw BYTES. But, this shouldn't be a problem since the underlying content doesn't matter.

REPEAT ME 10 MORE TIMES!!!
REPEAT ME 09 MORE TIMES!!!
REPEAT ME 08 MORE TIMES!!!
REPEAT ME 07 MORE TIMES!!!
REPEAT ME 06 MORE TIMES!!!
REPEAT ME 05 MORE TIMES!!!
REPEAT ME 04 MORE TIMES!!!
REPEAT ME 03 MORE TIMES!!!
REPEAT ME 02 MORE TIMES!!!
REPEAT ME 01 MORE TIMES!!!

_done_.
)";

using crashpad::StringFile;

TEST(SizedDataReaderTest, EmptyData) {
  const std::string empty_data{""};

  SizedData sized_data(empty_data.begin(), empty_data.end());
  SizedDataReader data_reader(sized_data);

  EXPECT_EQ(data_reader.Seek(0, SEEK_CUR), 0u);

  // Check a Read doesn't modify the output parameter.
  uint8_t c = '6';
  EXPECT_EQ(data_reader.Read(&c, 1), 0);
  EXPECT_EQ(c, '6');
  EXPECT_EQ(data_reader.Seek(0, SEEK_CUR), 0);
}

TEST(SizedDataReaderTest, CheckReadStringConformance) {
  SizedData sized_data(kLargeData.begin(), kLargeData.end());
  SizedDataReader data_reader(sized_data);

  StringFile string_file;
  string_file.SetString(kLargeData);

  // Perform reads of varying sizes to make sure SizedDataReader agrees with StringFile. For
  // the sake of simplicity the read size is monotonically increased until all bytes have been read.
  for (size_t i = 0; true; ++i) {
    std::string data_reader_str(i, '\0');
    std::string string_file_str(i, '\0');

    const auto data_result = data_reader.Read(data_reader_str.data(), i);
    const auto string_result = string_file.Read(string_file_str.data(), i);

    EXPECT_EQ(data_reader_str, string_file_str);

    EXPECT_EQ(data_result, string_result);
    if ((size_t)string_result != i) {
      break;
    }
  }
}

TEST(SizedDataReaderTest, CheckReadZipConformance) {
  std::string zip_file;
  ASSERT_TRUE(files::ReadFileToString("/pkg/data/test_data.zip", &zip_file));

  SizedData sized_data(zip_file.begin(), zip_file.end());
  SizedDataReader data_reader(sized_data);

  StringFile string_file;
  string_file.SetString(zip_file);

  // Perform reads of varying sizes to make sure SizedDataReader agrees with StringFile. For
  // the sake of simplicity the read size is monotonically increased until all bytes have been read.
  for (size_t i = 0; true; ++i) {
    const size_t read_size = i % 500;

    std::string data_reader_str(read_size, '\0');
    std::string string_file_str(read_size, '\0');

    const auto data_result = data_reader.Read(data_reader_str.data(), read_size);
    const auto string_result = string_file.Read(string_file_str.data(), read_size);

    EXPECT_EQ(data_reader_str, string_file_str);

    EXPECT_EQ(data_result, string_result);
    if ((size_t)string_result < read_size) {
      break;
    }
  }
}

}  // namespace
}  // namespace crash_reports
}  // namespace forensics
