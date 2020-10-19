// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/device_id_provider.h"

#include <fuchsia/feedback/cpp/fidl.h>

#include <memory>
#include <optional>
#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/feedback_data/annotations/types.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"
#include "src/developer/forensics/utils/errors.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/files/scoped_temp_dir.h"
#include "src/lib/uuid/uuid.h"

namespace forensics {
namespace feedback_data {
namespace {

constexpr char kDefaultDeviceId[] = "00000000-0000-4000-a000-000000000001";

class DeviceIdTest : public UnitTestFixture {
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

  DeviceIdManager& device_id_manager() {
    // Because the constructor of DeviceIdManager does work to read/initialize the device id, we
    // don't set up a DeviceIdManager until the file is in the state we want.
    if (!device_id_manager_) {
      device_id_manager_ = std::make_unique<DeviceIdManager>(dispatcher(), device_id_path_);
    }

    return *device_id_manager_;
  }

  std::optional<std::string> GetDeviceId() {
    // Because the constructor of DeviceIdManager does work to read/initialize the device id, we
    // don't set up a DeviceIdManager until the file is in the state we want.
    if (!device_id_manager_) {
      device_id_manager_ = std::make_unique<DeviceIdManager>(dispatcher(), device_id_path_);
    }

    fuchsia::feedback::DeviceIdProviderPtr device_id_provider;
    device_id_manager_->AddBinding(device_id_provider.NewRequest(dispatcher()),
                                   /*on_channel_close=*/[](const zx_status_t) {});

    std::optional<std::string> device_id = std::nullopt;
    device_id_provider->GetId([&](std::string feedback_id) { device_id = feedback_id; });

    RunLoopUntilIdle();

    return device_id;
  }

  std::string device_id_path_;

 private:
  files::ScopedTempDir tmp_dir_;
  std::unique_ptr<DeviceIdManager> device_id_manager_;
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

TEST_F(DeviceIdTest, Check_DeviceIdProviderRespondsOnce) {
  SetDeviceIdFileContentsTo(kDefaultDeviceId);

  fuchsia::feedback::DeviceIdProviderPtr client;
  device_id_manager().AddBinding(client.NewRequest(dispatcher()),
                                 /*on_channel_close=*/[](zx_status_t) {});

  std::optional<std::string> device_id{std::nullopt};

  // The first call should be responded to immediately.
  client->GetId([&](std::string result) { device_id = std::move(result); });
  RunLoopUntilIdle();

  ASSERT_TRUE(device_id.has_value());
  EXPECT_EQ(device_id.value(), kDefaultDeviceId);

  device_id = std::nullopt;

  // The second call should hang forever.
  client->GetId([&](std::string result) { device_id = std::move(result); });
  RunLoopUntilIdle();

  ASSERT_FALSE(device_id.has_value());
}

}  // namespace
}  // namespace feedback_data
}  // namespace forensics
