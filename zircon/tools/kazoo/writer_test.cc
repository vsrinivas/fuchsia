// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/kazoo/writer.h"

#include <unistd.h>

#include <string>

#include "tools/kazoo/string_util.h"
#include "tools/kazoo/test.h"

namespace {

class OverrideWriter : public Writer {
 public:
  bool Puts(const std::string& str) override {
    data_ += "PUTS: " + str;
    return true;
  }

  void Clear() { data_ = ""; }

  const std::string& data() const { return data_; }

 private:
  std::string data_;
};

TEST(Writer, CustomImplementation) {
  OverrideWriter* override_writer = new OverrideWriter;
  std::unique_ptr<Writer> writer(override_writer);

  writer->Puts("abc");
  EXPECT_EQ(override_writer->data(), "PUTS: abc");

  override_writer->Clear();
  writer->Printf("%d %x", 123, 999);
  EXPECT_EQ(override_writer->data(), "PUTS: 123 3e7");
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
