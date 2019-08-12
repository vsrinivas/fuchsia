// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/kazoo/writer.h"

#include <string>

#include "gtest/gtest.h"
#include "src/lib/files/file.h"
#include "src/lib/files/scoped_temp_dir.h"

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

TEST(Writer, FileWriter) {
  files::ScopedTempDir temp_dir;
  std::string filename;
  ASSERT_TRUE(temp_dir.NewTempFile(&filename));

  FileWriter* file_writer = new FileWriter;
  ASSERT_TRUE(file_writer->Open(filename));
  {
    std::unique_ptr<Writer> writer(file_writer);
    writer->Puts("xyz\n");
    for (int i = 0; i < 20; ++i) {
      writer->Printf("%d %x\n", i, i);
    }
  }

  std::string result;
  ASSERT_TRUE(files::ReadFileToString(filename, &result));
  EXPECT_EQ(result, R"(xyz
0 0
1 1
2 2
3 3
4 4
5 5
6 6
7 7
8 8
9 9
10 a
11 b
12 c
13 d
14 e
15 f
16 10
17 11
18 12
19 13
)");
}

}  // namespace
