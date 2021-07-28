// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback/device_id_provider.h"

#include <lib/async/cpp/executor.h>
#include <lib/fpromise/result.h>

#include <optional>
#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/testing/stubs/device_id_provider.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/files/scoped_temp_dir.h"

namespace forensics::feedback {
namespace {

constexpr zx::duration kDefaultTimeout = zx::sec(35);

constexpr char kDefaultDeviceId[] = "00000000-0000-4000-a000-000000000001";
constexpr char kInvalidDeviceId[] = "INVALID";

class DeviceIdProviderTest : public UnitTestFixture {
 public:
  DeviceIdProviderTest() : UnitTestFixture(), executor_(dispatcher()) {}

 protected:
  void SetUpDeviceIdProviderServer(
      std::unique_ptr<stubs::DeviceIdProviderBase> device_id_provider_server) {
    device_id_provider_server_ = std::move(device_id_provider_server);
    if (device_id_provider_server_) {
      InjectServiceProvider(device_id_provider_server_.get());
    }
  }

  std::string ReadFile(const std::string& path) {
    std::string file_contents;
    FX_CHECK(files::ReadFileToString(path, &file_contents));
    return file_contents;
  }

  std::optional<std::string> GetId(::fpromise::promise<std::string, Error> get_id) {
    bool is_called = false;
    std::optional<std::string> device_id = std::nullopt;
    executor_.schedule_task(
        get_id.then([&device_id, &is_called](::fpromise::result<std::string, Error>& result) {
          is_called = true;

          if (result.is_ok()) {
            device_id = result.take_value();
          }
        }));
    RunLoopUntilIdle();
    FX_CHECK(is_called) << "The promise chain was never executed";

    return device_id;
  }

  async::Executor executor_;
  files::ScopedTempDir tmp_dir_;

  std::unique_ptr<stubs::DeviceIdProviderBase> device_id_provider_server_;
};

TEST_F(DeviceIdProviderTest, RemoteDeviceIdProvider_CachesDeviceId) {
  SetUpDeviceIdProviderServer(std::make_unique<stubs::DeviceIdProvider>(kDefaultDeviceId));
  RunLoopUntilIdle();

  RemoteDeviceIdProvider device_id_provider(dispatcher(), services());

  const std::optional<std::string> id = GetId(device_id_provider.GetId(kDefaultTimeout));
  EXPECT_EQ(id, kDefaultDeviceId);
}

TEST_F(DeviceIdProviderTest, LocalDeviceIdProvider_WHAT) {
  {
    std::string device_id_path;

    ASSERT_TRUE(tmp_dir_.NewTempFileWithData(kDefaultDeviceId, &device_id_path));
    LocalDeviceIdProvider device_id_provider(device_id_path);

    const std::optional<std::string> id = GetId(device_id_provider.GetId(kDefaultTimeout));
    EXPECT_EQ(id, kDefaultDeviceId);
    EXPECT_EQ(ReadFile(device_id_path), kDefaultDeviceId);
  }

  {
    std::string device_id_path;

    ASSERT_TRUE(tmp_dir_.NewTempFileWithData(kInvalidDeviceId, &device_id_path));
    LocalDeviceIdProvider device_id_provider(device_id_path);

    const std::optional<std::string> id = GetId(device_id_provider.GetId(kDefaultTimeout));
    EXPECT_NE(id, kInvalidDeviceId);
    EXPECT_EQ(ReadFile(device_id_path), id);
  }

  {
    std::string device_id_path = files::JoinPath(tmp_dir_.path(), "device_id_file.txt");

    LocalDeviceIdProvider device_id_provider(device_id_path);

    const std::optional<std::string> id = GetId(device_id_provider.GetId(kDefaultTimeout));
    EXPECT_TRUE(id.has_value());
    EXPECT_EQ(ReadFile(device_id_path), id);
  }
}

}  // namespace
}  // namespace forensics::feedback
