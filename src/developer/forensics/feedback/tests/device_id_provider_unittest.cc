// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback/annotations/device_id_provider.h"

#include <memory>
#include <optional>
#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/feedback/annotations/constants.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/files/scoped_temp_dir.h"

namespace forensics::feedback {
namespace {

using ::testing::Not;
using ::testing::Pair;
using ::testing::UnorderedElementsAreArray;

constexpr char kDefaultDeviceId[] = "00000000-0000-4000-a000-000000000001";
constexpr char kInvalidDeviceId[] = "INVALID";

using RemoteDeviceIdProviderTest = UnitTestFixture;

TEST_F(RemoteDeviceIdProviderTest, GetKeys) {
  RemoteDeviceIdProvider device_id_provider(dispatcher(), services(), nullptr);
  EXPECT_THAT(device_id_provider.GetKeys(), UnorderedElementsAreArray({
                                                kDeviceFeedbackIdKey,
                                            }));
}

TEST_F(RemoteDeviceIdProviderTest, DeviceIdToAnnotations) {
  DeviceIdToAnnotations convert;

  EXPECT_THAT(convert(""), UnorderedElementsAreArray({
                               Pair(kDeviceFeedbackIdKey, ErrorOr<std::string>("")),
                           }));
  EXPECT_THAT(convert("id"), UnorderedElementsAreArray({
                                 Pair(kDeviceFeedbackIdKey, ErrorOr<std::string>("id")),
                             }));
}

TEST(LocalDeviceIdProviderTest, GetOnUpdate) {
  files::ScopedTempDir tmp_dir;
  auto ReadFile = [](const std::string& path) {
    std::string file_contents;
    FX_CHECK(files::ReadFileToString(path, &file_contents));
    return file_contents;
  };

  {
    std::string device_id_path;

    ASSERT_TRUE(tmp_dir.NewTempFileWithData(kDefaultDeviceId, &device_id_path));
    LocalDeviceIdProvider device_id_provider(device_id_path);

    Annotations annotations;
    device_id_provider.GetOnUpdate(
        [&annotations](Annotations result) { annotations = std::move(result); });

    EXPECT_THAT(annotations, UnorderedElementsAreArray({
                                 Pair(kDeviceFeedbackIdKey, kDefaultDeviceId),
                             }));
    EXPECT_EQ(ReadFile(device_id_path), kDefaultDeviceId);
  }

  {
    std::string device_id_path;

    ASSERT_TRUE(tmp_dir.NewTempFileWithData(kInvalidDeviceId, &device_id_path));
    LocalDeviceIdProvider device_id_provider(device_id_path);

    Annotations annotations;

    device_id_provider.GetOnUpdate(
        [&annotations](Annotations result) { annotations = std::move(result); });

    ASSERT_TRUE(annotations.at(kDeviceFeedbackIdKey).HasValue());
    EXPECT_NE(annotations.at(kDeviceFeedbackIdKey).Value(), kInvalidDeviceId);
    EXPECT_EQ(ReadFile(device_id_path), annotations.at(kDeviceFeedbackIdKey).Value());
  }

  {
    std::string device_id_path = files::JoinPath(tmp_dir.path(), "device_id_file.txt");

    LocalDeviceIdProvider device_id_provider(device_id_path);

    Annotations annotations;
    device_id_provider.GetOnUpdate(
        [&annotations](Annotations result) { annotations = std::move(result); });

    EXPECT_NE(annotations.count(kDeviceFeedbackIdKey), 0u);
    EXPECT_EQ(ReadFile(device_id_path), annotations.at(kDeviceFeedbackIdKey).Value());
  }
}

}  // namespace
}  // namespace forensics::feedback
