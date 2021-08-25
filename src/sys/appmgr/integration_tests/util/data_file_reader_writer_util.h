// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_APPMGR_INTEGRATION_TESTS_UTIL_DATA_FILE_READER_WRITER_UTIL_H_
#define SRC_SYS_APPMGR_INTEGRATION_TESTS_UTIL_DATA_FILE_READER_WRITER_UTIL_H_

#include <lib/sys/cpp/testing/test_with_environment_fixture.h>

#include <string>

#include <test/appmgr/integration/cpp/fidl.h>

namespace component {
namespace testing {

class DataFileReaderWriterUtil : virtual public gtest::TestWithEnvironmentFixture {
 protected:
  fidl::StringPtr ReadFileSync(const test::appmgr::integration::DataFileReaderWriterPtr& util,
                               std::string path);

  zx_status_t WriteFileSync(const test::appmgr::integration::DataFileReaderWriterPtr& util,
                            std::string path, std::string contents);

  fidl::StringPtr ReadTmpFileSync(const test::appmgr::integration::DataFileReaderWriterPtr& util,
                                  std::string path);

  zx_status_t WriteTmpFileSync(const test::appmgr::integration::DataFileReaderWriterPtr& util,
                               std::string path, std::string contents);
};

}  // namespace testing
}  // namespace component

#endif  // SRC_SYS_APPMGR_INTEGRATION_TESTS_UTIL_DATA_FILE_READER_WRITER_UTIL_H_
