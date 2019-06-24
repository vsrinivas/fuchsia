// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "file_utils.h"

#include <gtest/gtest.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

namespace {

// TODO: Move that to a test support library or reuse an existing one.

// Convenience class to ensure that a FILE* handle is fclose()-ed
// on scope exit. Usage is pretty simple:
//
//    FILE* f = fopen(....);
//    if (!f)
//       return ....;  // error
//
//    ScopedFileGuard guard(f);   // ensures |f| is closed on scope exit.
//
class ScopedFileGuard {
 public:
  ScopedFileGuard(FILE * file) : file_(file)
  {
  }

  ~ScopedFileGuard()
  {
    if (file_)
      {
        fclose(file_);
      }
  }

 private:
  FILE * file_ = NULL;
};

// Convenience class for a temporary directory.
// Ensures that the directory is removed on scope exit.
class ScopedTempDir {
 public:
  ScopedTempDir()
  {
    const char * tmp_dir = getenv("TMPDIR");
    if (!tmp_dir)
      tmp_dir = "/tmp";
    snprintf(temp_root_, sizeof(temp_root_), "%s/XXXXXX", tmp_dir);
    EXPECT_TRUE(mkdtemp(temp_root_));
  }

  // NOTE: Destructor only removes the directory, not any files in it.
  // Use ScopedTempFile() to ensure temporary files are destroyed at the
  // end of tests.
  ~ScopedTempDir()
  {
    rmdir(temp_root_);
  }

  // Return temporary root directory.
  const char *
  root() const
  {
    return temp_root_;
  }

 private:
  char temp_root_[256];
};

// Convenience class for a temporary file that is removed on scoped exit.
class ScopedTempFile {
 public:
  // Constructor, creates file path, but doesn't touch the file system.
  // |name| is a file name (cannot include sub-directory).
  // |root| is the root ScopedTempDir.
  ScopedTempFile(const char * name, const ScopedTempDir & root)
  {
    snprintf(file_path_, sizeof(file_path_), "%s/%s", root.root(), name);
  }

  // Destructor removes the file, if any.
  ~ScopedTempFile()
  {
    (void)unlink(file_path_);
  }

  // Write |data_size| bytes from |data| into the file.
  bool
  WriteData(const void * data, size_t data_size)
  {
    FILE * f = fopen(file_path_, "wb");
    if (!f)
      return false;
    ScopedFileGuard guard(f);
    return fwrite(data, 1, data_size, f) == data_size;
  }

  // Read the file. On success return true and sets |*out|, false/errno
  // otherwise.
  bool
  ReadData(std::string * out)
  {
    FILE * f = fopen(file_path_, "rb");
    if (!f)
      return false;
    ScopedFileGuard guard(f);
    if (fseek(f, 0, SEEK_END) != 0)
      return false;
    long signed_size = ftell(f);
    if (signed_size < 0)
      return false;
    if (fseek(f, 0, SEEK_SET) != 0)
      return false;
    auto size = static_cast<size_t>(signed_size);
    out->resize(size);
    return fread(&out[0], 1, size, f) == size;
  }

  const char *
  path() const
  {
    return file_path_;
  }

 private:
  char file_path_[256];
};

//
//
//

TEST(common, file_read_WithInvalidPath)
{
  int    dummy = 10;
  void * data  = &dummy;  // Pseudo-random value.
  size_t size  = 42;
  EXPECT_FALSE(file_read("/this/path/does/not/exist", &data, &size));
  EXPECT_EQ(NULL, data);
  EXPECT_EQ(0U, size);
}

TEST(common, file_read_EmptyFile)
{
  ScopedTempDir  tmp_dir;
  ScopedTempFile empty("empty.txt", tmp_dir);
  empty.WriteData("", 0);

  int    dummy = 10;
  void * data  = &dummy;
  size_t size  = 42;
  EXPECT_TRUE(file_read(empty.path(), &data, &size));
  EXPECT_EQ(NULL, data);
  EXPECT_EQ(0U, size);
}

TEST(common, file_read_RegularFile)
{
  ScopedTempDir  tmp_dir;
  ScopedTempFile file("example.txt", tmp_dir);
  std::string    text = "Hello World!";
  ASSERT_TRUE(file.WriteData(text.c_str(), text.size()));

  void * data;
  size_t size;
  EXPECT_TRUE(file_read(file.path(), &data, &size));
  ASSERT_TRUE(data);
  ASSERT_EQ(text.size(), size);
  ASSERT_TRUE(!memcmp(data, text.c_str(), size));
  free(data);
}

TEST(common, file_write_WithInvalidPath)
{
  char kData[] = "Hello World!";
  EXPECT_FALSE(file_write("/this/path/does/not/exist", kData, sizeof(kData)));
}

TEST(common, file_write_EmptyFile)
{
  ScopedTempDir  tmp_dir;
  ScopedTempFile empty("empty.txt", tmp_dir);

  EXPECT_TRUE(file_write(empty.path(), "", 0));

  std::string out("dummy");
  ASSERT_TRUE(empty.ReadData(&out));
  EXPECT_EQ("", out);
}

TEST(common, file_write_RegularFile)
{
  ScopedTempDir  tmp_dir;
  ScopedTempFile file("example.txt", tmp_dir);

  const std::string text = "Hello World!";
  EXPECT_TRUE(file_write(file.path(), text.c_str(), text.size()));

  std::string data;
  ASSERT_TRUE(file.ReadData(&data));
  EXPECT_EQ(text, data);
}

}  // namespace
