// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_agent/device_id_provider.h"

#include <memory>
#include <optional>
#include <string>

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
 public:
  void SetUp() override {
    ASSERT_TRUE(uuid::IsValid(kDefaultDeviceId));
    SetDeviceIdFileContentsTo(kDefaultDeviceId);
  }

 protected:
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

  std::optional<std::string> GetDeviceId() {
    // Because the constructor of DeviceIdProvider does work to read/initialize the device id, we
    // don't set up a DeviceIdProvider until the file is in the state we want.
    DeviceIdProvider device_id_provider(device_id_path_);
    return device_id_provider.GetId();
  }

  std::string device_id_path_;

 private:
  files::ScopedTempDir tmp_dir_;
};

TEST_F(DeviceIdTest, Check_ValidDeviceIdPresent) {
  SetDeviceIdFileContentsTo(kDefaultDeviceId);

  const std::optional<std::string> device_id = GetDeviceId();

  ASSERT_TRUE(device_id.has_value());
  EXPECT_EQ(device_id.value(), kDefaultDeviceId);
  CheckDeviceIdFileContentsAre(kDefaultDeviceId);
}

TEST_F(DeviceIdTest, Check_InvalidDeviceIdPresent) {
  SetDeviceIdFileContentsTo("INVALID ID");

  const std::optional<std::string> device_id = GetDeviceId();

  ASSERT_TRUE(device_id.has_value());
  CheckDeviceIdFileContentsAre(device_id.value());
}

TEST_F(DeviceIdTest, Check_FileNotPresent) {
  DeleteDeviceIdFile();

  const std::optional<std::string> device_id = GetDeviceId();

  ASSERT_TRUE(device_id.has_value());
  CheckDeviceIdFileContentsAre(device_id.value());
}

TEST_F(DeviceIdTest, Fail_IfPathIsADirectory) {
  DeleteDeviceIdFile();
  ASSERT_TRUE(files::CreateDirectory(device_id_path_));
  EXPECT_FALSE(GetDeviceId().has_value());
}

}  // namespace
}  // namespace feedback
