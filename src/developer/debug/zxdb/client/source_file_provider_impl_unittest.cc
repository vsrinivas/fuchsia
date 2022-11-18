// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/source_file_provider_impl.h"

#include <stdlib.h>

#include <gtest/gtest.h>

#include "src/developer/debug/zxdb/common/file_util.h"
#include "src/developer/debug/zxdb/common/scoped_temp_file.h"

namespace zxdb {

TEST(SourceUtil, SourceFileProviderImpl) {
  // Make a temp file with known contents.
  ScopedTempFile temp_file;
  ASSERT_NE(-1, temp_file.fd());
  const std::string expected = "contents";
  write(temp_file.fd(), expected.data(), expected.size());

  std::string file_part(ExtractLastFileComponent(temp_file.name()));

  // Test with full input path.
  SourceFileProviderImpl provider_no_build_dirs({});
  ErrOr<SourceFileProviderImpl::FileData> result =
      provider_no_build_dirs.GetFileData(temp_file.name(), "");
  ASSERT_FALSE(result.has_error()) << result.err().msg();
  EXPECT_EQ(expected, result.value().contents);

  // With just file part, should not be found.
  result = provider_no_build_dirs.GetFileData(file_part, "");
  EXPECT_TRUE(result.has_error());

  // With DWARF compilation dir of "/tmp" it should be found again.
  result = provider_no_build_dirs.GetFileData(file_part, "/tmp");
  ASSERT_FALSE(result.has_error()) << result.err().msg();
  EXPECT_EQ(expected, result.value().contents);

  // With symbol search path it should be found.
  SourceFileProviderImpl provider_tmp_build_dir({"/tmp"});
  result = provider_tmp_build_dir.GetFileData(file_part, "");
  ASSERT_FALSE(result.has_error()) << result.err().msg();
  EXPECT_EQ(expected, result.value().contents);

  // Combination of preference and relative search path.
  SourceFileProviderImpl provider_slash_build_dir({"/"});
  result = provider_slash_build_dir.GetFileData(file_part, "tmp");
  ASSERT_FALSE(result.has_error()) << result.err().msg();
  EXPECT_EQ(expected, result.value().contents);
}

}  // namespace zxdb
