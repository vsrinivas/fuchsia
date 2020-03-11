// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/io.h>
#include <fbl/unique_fd.h>
#include <lib/zx/vmo.h>
#include <zircon/sanitizer.h>
#include <zxtest/zxtest.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <string>

namespace {

constexpr char kTestName[] = "15822697145192797690.profraw";
constexpr char kTestData[] = "llvm-profile";
constexpr char kTestMessage[] = "{{{dumpfile:llvm-profile:15822697145192797690.profraw}}}";

TEST(RunTestHelper, PublishData) {
  std::string test_root_dir{getenv("TEST_ROOT_DIR")};
  std::string test_data_dir{test_root_dir + "/" + "test/sys/runtests-utils-testdata/profile"};

  fbl::unique_fd fd{open((test_data_dir + "/" + kTestName).c_str(), O_RDONLY)};
	ASSERT_TRUE(fd.is_valid());

  zx::vmo vmo;
  ASSERT_OK(fdio_get_vmo_copy(fd.get(), vmo.reset_and_get_address()));
  vmo.set_property(ZX_PROP_NAME, kTestName, sizeof(kTestName) - 1);
  __sanitizer_publish_data(kTestData, vmo.release());
  __sanitizer_log_write(kTestMessage, sizeof(kTestMessage) - 1);
}

}  // anonymous namespace
