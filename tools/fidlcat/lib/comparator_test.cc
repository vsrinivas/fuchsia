// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/fidlcat/lib/comparator.h"

#include <array>
#include <fstream>
#include <string>

#include "gtest/gtest.h"

namespace fidlcat {

class TestComparator : public Comparator {
  using Comparator::Comparator;

 public:
  void AddOutput(std::string_view output) { output_stream() << output; }
};

TEST(Comparator, OutputStream) {
  fidlcat::TestComparator comparator("file_name");
  std::ostringstream& output_stream = comparator.output_stream();
  output_stream << "line1";
  ASSERT_STREQ(comparator.output_stream().str().c_str(), "line1");
}

TEST(Comparator, SameContent) {
  // create file to compare to
  std::fstream compare_file;
  char file_name[] = "/tmp/compareXXXXXX";
  int status = mkstemp(file_name);
  ASSERT_NE(status, -1) << "Failed to create output file";
  compare_file.open(file_name);
  compare_file << "line 1\nline 2\n";
  compare_file.close();

  // Create comparator and add output to its internal actual stream directly
  fidlcat::TestComparator comparator(file_name);
  comparator.AddOutput("line 1\nline 2\n");

  // Compare compare_file and output
  std::ostringstream compare_result;
  comparator.Compare(compare_result);
  std::string expected = "Identical output and expected\n";
  ASSERT_STREQ(expected.c_str(), compare_result.str().c_str());
}

TEST(Comparator, ShorterActualOutput) {
  // create file to compare to
  std::fstream compare_file;
  char file_name[] = "/tmp/compareXXXXXX";
  int status = mkstemp(file_name);
  ASSERT_NE(status, -1) << "Failed to create output file";
  compare_file.open(file_name);
  compare_file << "line 1\nline 2\nline 3\n";
  compare_file.close();

  // Create comparator and add output to its internal actual stream directly
  fidlcat::TestComparator comparator(file_name);
  comparator.AddOutput("line 1\nline 2\n");

  // Compare compare_file and output
  std::ostringstream compare_result;
  comparator.Compare(compare_result);
  std::string expected = "Expected output was longer\n";
  ASSERT_STREQ(expected.c_str(), compare_result.str().c_str());
}

TEST(Comparator, LongerActualOutput) {
  // create file to compare to
  std::fstream compare_file;
  char file_name[] = "/tmp/compareXXXXXX";
  int status = mkstemp(file_name);
  ASSERT_NE(status, -1) << "Failed to create output file";
  compare_file.open(file_name);
  compare_file << "line 1\nline 2\n";
  compare_file.close();

  // Create comparator and add output to its internal actual stream directly
  fidlcat::TestComparator comparator(file_name);
  comparator.AddOutput("line 1\nline 2\nline 3\n");

  // Compare compare_file and output
  std::ostringstream compare_result;
  comparator.Compare(compare_result);
  std::string expected = "Actual output was longer\n";
  ASSERT_STREQ(expected.c_str(), compare_result.str().c_str());
}

TEST(Comparator, SameLengthDiffContent) {
  // create file to compare to
  std::fstream compare_file;
  char file_name[] = "/tmp/compareXXXXXX";
  int status = mkstemp(file_name);
  ASSERT_NE(status, -1) << "Failed to create output file";
  compare_file.open(file_name);
  compare_file << "line 1\nline b\n";
  compare_file.close();

  // Create comparator and add output to its internal actual stream directly
  fidlcat::TestComparator comparator(file_name);
  comparator.AddOutput("line 1\nline 2\n");

  // Compare compare_file and output
  std::ostringstream compare_result;
  comparator.Compare(compare_result);
  std::string expected = "Expected: \"line b\"\nActual:   \"line 2\"\n";
  ASSERT_STREQ(expected.c_str(), compare_result.str().c_str());
}

}  // namespace fidlcat
