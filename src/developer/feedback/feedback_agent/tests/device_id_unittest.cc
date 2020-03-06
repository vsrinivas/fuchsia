// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_agent/device_id.h"

#include <memory>

#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/files/scoped_temp_dir.h"
#include "src/lib/uuid/uuid.h"
#include "third_party/googletest/googlemock/include/gmock/gmock.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace feedback {
namespace {

constexpr char kDefaultDeviceId[] = "00000000-0000-4000-a000-000000000001";

class DeviceIdTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(uuid::IsValid(kDefaultDeviceId));
    SetDeviceIdFileContentsTo(kDefaultDeviceId);
  }

  void SetDeviceIdFileContentsTo(const std::string& contents) {
    ASSERT_TRUE(tmp_dir_.NewTempFileWithData(contents, &device_id_path_));
  }

  void CheckDeviceIdFileContentsAre(const std::string& expected_contents) {
    std::string file_contents;
    ASSERT_TRUE(files::ReadFileToString(device_id_path_, &file_contents));
    EXPECT_EQ(file_contents, expected_contents);
  }

  void CheckDeviceIdFileContentsAreValid() {
    std::string file_contents;
    ASSERT_TRUE(files::ReadFileToString(device_id_path_, &file_contents));
    EXPECT_TRUE(uuid::IsValid(file_contents));
  }

  void DeleteDeviceIdFile() {
    ASSERT_TRUE(files::DeletePath(device_id_path_, /*recursive=*/false));
  }

  std::string device_id_path_;

 private:
  files::ScopedTempDir tmp_dir_;
};

TEST_F(DeviceIdTest, LeaveFileUntouchedIfPresent) {
  EXPECT_TRUE(InitializeDeviceId(device_id_path_));
  CheckDeviceIdFileContentsAre(kDefaultDeviceId);
}

TEST_F(DeviceIdTest, CheckFileIfNotPresent) {
  DeleteDeviceIdFile();
  EXPECT_TRUE(InitializeDeviceId(device_id_path_));
  CheckDeviceIdFileContentsAreValid();
}

TEST_F(DeviceIdTest, OverwriteFileIfInvalid) {
  SetDeviceIdFileContentsTo("INVALID ID");
  EXPECT_TRUE(InitializeDeviceId(device_id_path_));
  CheckDeviceIdFileContentsAreValid();
}

TEST_F(DeviceIdTest, FailsIfPathIsADirectory) {
  DeleteDeviceIdFile();
  ASSERT_TRUE(files::CreateDirectory(device_id_path_));
  EXPECT_FALSE(InitializeDeviceId(device_id_path_));
}

}  // namespace
}  // namespace feedback
