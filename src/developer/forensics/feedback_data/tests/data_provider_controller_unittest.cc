// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/data_provider_controller.h"

#include <lib/fidl/cpp/binding.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/feedback_data/constants.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"
#include "src/lib/files/file.h"

namespace forensics {
namespace feedback_data {
namespace {

class FakeSystemLogRecorder : public fuchsia::feedback::DataProviderController {
 public:
  FakeSystemLogRecorder(async_dispatcher_t* dispatcher, zx::channel channel)
      : binding_(this, std::move(channel), dispatcher) {}

  bool Running() const { return running_; }

  // fuchsia.feedback.DataProviderController
  void DisableAndDropPersistentLogs(DisableAndDropPersistentLogsCallback callback) override {
    running_ = false;
    callback();
  }

 private:
  bool running_{true};
  ::fidl::Binding<fuchsia::feedback::DataProviderController> binding_;
};

using DataProviderControllerTest = UnitTestFixture;

TEST_F(DataProviderControllerTest, DisableAndDropPersistentLogs_SystemLogRecorder) {
  zx::channel client, server;
  ASSERT_EQ(zx::channel::create(0, &client, &server), ZX_OK);

  FakeSystemLogRecorder system_log_recorder(dispatcher(), std::move(server));
  ASSERT_TRUE(system_log_recorder.Running());

  bool success{false};

  DataProviderController data_provider_controller;
  data_provider_controller.BindSystemLogRecorderController(std::move(client), dispatcher());

  data_provider_controller.DisableAndDropPersistentLogs([&] { success = true; });

  RunLoopUntilIdle();

  EXPECT_TRUE(success);
  EXPECT_FALSE(system_log_recorder.Running());
  EXPECT_TRUE(files::IsFile(kDoNotLaunchSystemLogRecorder));
}

TEST_F(DataProviderControllerTest, DisableAndDropPersistentLogs_NoSystemLogRecorder) {
  bool success{false};

  DataProviderController data_provider_controller;
  data_provider_controller.DisableAndDropPersistentLogs([&] { success = true; });

  RunLoopUntilIdle();

  EXPECT_TRUE(success);
  EXPECT_TRUE(files::IsFile(kDoNotLaunchSystemLogRecorder));
}

}  // namespace
}  // namespace feedback_data
}  // namespace forensics
