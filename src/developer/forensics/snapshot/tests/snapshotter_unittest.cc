// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/snapshot/snapshotter.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/testing/service_directory_provider.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/errors.h>

#include <memory>
#include <string>

#include <gtest/gtest.h>

#include "src/developer/forensics/testing/stubs/data_provider.h"
#include "src/lib/files/file.h"
#include "src/lib/files/scoped_temp_dir.h"
#include "src/lib/fsl/vmo/strings.h"
#include "src/lib/testing/loop_fixture/test_loop_fixture.h"

namespace forensics {
namespace snapshot {
namespace {

class SnapshotterTest : public gtest::TestLoopFixture {
 public:
  SnapshotterTest()
      : service_directory_provider_loop_(&kAsyncLoopConfigNoAttachToCurrentThread),
        service_directory_provider_(service_directory_provider_loop_.dispatcher()) {}

  void SetUp() override {
    // We run the service directory provider in a different loop and thread so that the
    // MakeSnapshot can connect to the stub feedback data provider synchronously.
    FX_CHECK(service_directory_provider_loop_.StartThread("service directory provider thread") ==
             ZX_OK);

    FX_CHECK(tmp_dir_.NewTempFile(&snapshot_path_));
  }

 protected:
  void SetUpDataProviderServer(fuchsia::feedback::Attachment snapshot) {
    data_provider_server_ = std::make_unique<stubs::DataProviderSnapshotOnly>(std::move(snapshot));
    FX_CHECK(service_directory_provider_.AddService(data_provider_server_->GetHandler()) == ZX_OK);
  }

 private:
  async::Loop service_directory_provider_loop_;

 protected:
  sys::testing::ServiceDirectoryProvider service_directory_provider_;
  std::string snapshot_path_;

 private:
  std::unique_ptr<stubs::DataProviderBase> data_provider_server_;
  files::ScopedTempDir tmp_dir_;
};

TEST_F(SnapshotterTest, Basic) {
  const std::string payload = "technically a ZIP archive, but it doesn't matter for the unit test";

  fuchsia::feedback::Attachment snapshot;
  snapshot.key = "unused";
  ASSERT_TRUE(fsl::VmoFromString(payload, &snapshot.value));
  SetUpDataProviderServer(std::move(snapshot));

  ASSERT_TRUE(MakeSnapshot(service_directory_provider_.service_directory(), snapshot_path_.data()));

  std::string snapshot_str;
  ASSERT_TRUE(files::ReadFileToString(snapshot_path_, &snapshot_str));
  EXPECT_STREQ(snapshot_str.c_str(), payload.c_str());
}

}  // namespace
}  // namespace snapshot
}  // namespace forensics
