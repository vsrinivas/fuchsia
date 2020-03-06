// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_agent/annotations/device_id_provider.h"

#include "src/developer/feedback/feedback_agent/constants.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/uuid/uuid.h"
#include "third_party/googletest/googlemock/include/gmock/gmock.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace feedback {
namespace {

constexpr char kDefaultDeviceId[] = "00000000-0000-4000-a000-000000000001";

class DeviceIdProviderTest : public testing::Test {
 protected:
  void SetUp() override { SetDeviceIdFileContentsTo(kDefaultDeviceId); }

  void TearDown() override { DeleteDeviceIdFile(); }

  void SetDeviceIdFileContentsTo(const std::string& contents) {
    ASSERT_TRUE(files::WriteFile(kDeviceIdPath, contents.c_str(), contents.size()));
  }

  void DeleteDeviceIdFile() { ASSERT_TRUE(files::DeletePath(kDeviceIdPath, /*recursive=*/false)); }

  std::optional<std::string> GetDeviceId() {
    DeviceIdProvider provider;
    return provider.GetAnnotation();
  }
};

TEST_F(DeviceIdProviderTest, FileExists) {
  std::optional<std::string> device_id = GetDeviceId();
  ASSERT_TRUE(device_id.has_value());
  EXPECT_EQ(device_id.value(), kDefaultDeviceId);
}

TEST_F(DeviceIdProviderTest, FailsIfFileDoesNotExist) {
  DeleteDeviceIdFile();
  std::optional<std::string> device_id = GetDeviceId();
  ASSERT_FALSE(device_id.has_value());
}

TEST_F(DeviceIdProviderTest, FailsIfIdIsInvalid) {
  SetDeviceIdFileContentsTo("BAD ID");
  std::optional<std::string> device_id = GetDeviceId();
  ASSERT_FALSE(device_id.has_value());
}

TEST_F(DeviceIdProviderTest, FailsIfPathIsADirectory) {
  DeleteDeviceIdFile();
  files::CreateDirectory(kDeviceIdPath);
  std::optional<std::string> device_id = GetDeviceId();
  ASSERT_FALSE(device_id.has_value());
}

}  // namespace
}  // namespace feedback
