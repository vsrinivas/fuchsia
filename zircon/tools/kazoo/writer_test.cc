// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/kazoo/writer.h"

#include <unistd.h>

#include <string>

#include "tools/kazoo/string_util.h"
#include "tools/kazoo/test.h"

namespace {

TEST(Writer, PrintSpacerLine) {
  Writer writer;
  EXPECT_EQ(writer.Out(), "");

  // When there is no previous line, PrintSpacerLine() should have no effect.
  writer.PrintSpacerLine();
  EXPECT_EQ(writer.Out(), "");

  // When the last line is non-empty, PrintSpacerLine() should print an empty
  // line.
  writer.Puts("Non-empty line\n");
  writer.PrintSpacerLine();
  EXPECT_EQ(writer.Out(), "Non-empty line\n\n");

  // When the last line is empty, PrintSpacerLine() should have no effect.
  writer.PrintSpacerLine();
  EXPECT_EQ(writer.Out(), "Non-empty line\n\n");
}

TEST(Writer, WriteFileIfChanged) {
  // Create a temporary directory so that we can safely test (i.e. without
  // /tmp race conditions) writing a file that does not exist yet.
  char dir_path[] = "/tmp/kazoo_writer_test_dir_XXXXXX";
  ASSERT_TRUE(mkdtemp(dir_path));

  std::string filename(dir_path);
  filename += "/test_file";

  std::string contents;

  // Write data and check that the data was written.

  // Test the case where the file did not exist.
  EXPECT_TRUE(WriteFileIfChanged(filename, "data1"));
  EXPECT_TRUE(ReadFileToString(filename, &contents));
  EXPECT_EQ(contents, "data1");

  // Test the case of writing different file contents.
  EXPECT_TRUE(WriteFileIfChanged(filename, "data2"));
  EXPECT_TRUE(ReadFileToString(filename, &contents));
  EXPECT_EQ(contents, "data2");

  // Test the case where the file contents are unchanged.
  EXPECT_TRUE(WriteFileIfChanged(filename, "data2"));
  EXPECT_TRUE(ReadFileToString(filename, &contents));
  EXPECT_EQ(contents, "data2");

  // Clean up.
  ASSERT_EQ(unlink(filename.c_str()), 0);
  ASSERT_EQ(rmdir(dir_path), 0);
}

}  // namespace
