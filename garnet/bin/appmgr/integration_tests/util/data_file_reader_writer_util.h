// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_APPMGR_INTEGRATION_TESTS_UTIL_DATA_FILE_READER_WRITER_UTIL_H_
#define GARNET_BIN_APPMGR_INTEGRATION_TESTS_UTIL_DATA_FILE_READER_WRITER_UTIL_H_

#include <lib/sys/cpp/testing/test_with_environment.h>
#include <test/appmgr/integration/cpp/fidl.h>
#include <string>

namespace component {
namespace testing {

class DataFileReaderWriterUtil : virtual public sys::testing::TestWithEnvironment {
 protected:
  fidl::StringPtr ReadFileSync(const test::appmgr::integration::DataFileReaderWriterPtr& util,
                               std::string path);

  zx_status_t WriteFileSync(const test::appmgr::integration::DataFileReaderWriterPtr& util,
                            std::string path, std::string contents);
};

}  // namespace testing
}  // namespace component

#endif  // GARNET_BIN_APPMGR_INTEGRATION_TESTS_UTIL_DATA_FILE_READER_WRITER_UTIL_H_
