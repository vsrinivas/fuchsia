// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_SOURCE_UTIL_TEST_SUPPORT_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_SOURCE_UTIL_TEST_SUPPORT_H_

#include <map>
#include <string>

#include "src/developer/debug/zxdb/console/source_util.h"

namespace zxdb {

// Mock implementation of SourceFileProvider that returns canned input for known file names.
// No handling of paths is done and the file_build_dir is ignored: the file names must match
// exactly.
class MockSourceFileProvider : public SourceFileProvider {
 public:
  // Sets the expected contents for the given file.
  void SetFileContents(const std::string& file_name, std::string contents);

  ErrOr<std::string> GetFileContents(const std::string& file_name,
                                     const std::string& file_build_dir) const override;

 private:
  // Maps file names to their hardcoded contents.
  std::map<std::string, std::string> contents_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_SOURCE_UTIL_TEST_SUPPORT_H_
