// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_agent/datastore.h"

#include <lib/async/cpp/executor.h>
#include <lib/fit/result.h>
#include <lib/zx/time.h>

#include <memory>
#include <string>

#include "src/developer/feedback/feedback_agent/annotations/aliases.h"
#include "src/developer/feedback/feedback_agent/attachments/aliases.h"
#include "src/developer/feedback/feedback_agent/constants.h"
#include "src/developer/feedback/feedback_agent/device_id_provider.h"
#include "src/developer/feedback/testing/cobalt_test_fixture.h"
#include "src/developer/feedback/testing/stubs/stub_cobalt_logger_factory.h"
#include "src/developer/feedback/testing/unit_test_fixture.h"
#include "src/developer/feedback/utils/cobalt.h"
#include "src/lib/files/file.h"
#include "src/lib/syslog/cpp/logger.h"
#include "third_party/googletest/googlemock/include/gmock/gmock.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace feedback {
namespace {

using ::testing::ElementsAreArray;
using ::testing::IsEmpty;
using ::testing::Not;
using ::testing::Pair;

constexpr zx::duration kTimeout = zx::sec(30);

class DatastoreTest : public UnitTestFixture, public CobaltTestFixture {
 public:
  DatastoreTest() : CobaltTestFixture(/*unit_test_fixture=*/this), executor_(dispatcher()) {}

  void SetUp() override {
    SetUpCobaltLoggerFactory(std::make_unique<StubCobaltLoggerFactory>());
    cobalt_ = std::make_unique<Cobalt>(dispatcher(), services());
  }

 protected:
  void SetUpDatastore(const AnnotationKeys& annotation_allowlist,
                      const AttachmentKeys& attachment_allowlist) {
    datastore_ = std::make_unique<Datastore>(dispatcher(), services(), cobalt_.get(), kTimeout,
                                             annotation_allowlist, attachment_allowlist);
  }

  fit::result<Annotations> GetAnnotations() {
    FX_CHECK(datastore_);

    fit::result<Annotations> result;
    executor_.schedule_task(datastore_->GetAnnotations().then(
        [&result](fit::result<Annotations>& res) { result = std::move(res); }));
    RunLoopFor(kTimeout);
    return result;
  }

  fit::result<Attachments> GetAttachments() {
    FX_CHECK(datastore_);

    fit::result<Attachments> result;
    executor_.schedule_task(datastore_->GetAttachments().then(
        [&result](fit::result<Attachments>& res) { result = std::move(res); }));
    RunLoopFor(kTimeout);
    return result;
  }

 private:
  std::unique_ptr<Cobalt> cobalt_;
  std::unique_ptr<Datastore> datastore_;
  async::Executor executor_;
};

TEST_F(DatastoreTest, GetAnnotations_DeviceId) {
  const std::optional<std::string> device_id = DeviceIdProvider(kDeviceIdPath).GetId();
  ASSERT_TRUE(device_id.has_value());

  SetUpDatastore(
      {
          kAnnotationDeviceFeedbackId,
      },
      {});

  fit::result<Annotations> annotations = GetAnnotations();
  ASSERT_TRUE(annotations.is_ok());
  EXPECT_THAT(annotations.take_value(), ElementsAreArray({
                                            Pair(kAnnotationDeviceFeedbackId, device_id.value()),
                                        }));
}

TEST_F(DatastoreTest, GetAnnotations_Time) {
  SetUpDatastore(
      {
          kAnnotationDeviceUptime,
          kAnnotationDeviceUTCTime,
      },
      {});

  fit::result<Annotations> annotations = GetAnnotations();
  ASSERT_TRUE(annotations.is_ok());
  EXPECT_THAT(annotations.take_value(), ElementsAreArray({
                                            Pair(kAnnotationDeviceUptime, Not(IsEmpty())),
                                            Pair(kAnnotationDeviceUTCTime, Not(IsEmpty())),
                                        }));
}

TEST_F(DatastoreTest, GetAnnotations_EmptyAnnotationAllowlist) {
  SetUpDatastore({}, {});

  fit::result<Annotations> annotations = GetAnnotations();
  ASSERT_TRUE(annotations.is_error());
}

TEST_F(DatastoreTest, GetAnnotations_OnlyUnknownAnnotationInAllowlist) {
  SetUpDatastore({"unknown.annotation"}, {});

  fit::result<Annotations> annotations = GetAnnotations();
  ASSERT_TRUE(annotations.is_error());
}

TEST_F(DatastoreTest, GetAttachments_EmptyAttachmentAllowlist) {
  SetUpDatastore({}, {});

  fit::result<Attachments> attachments = GetAttachments();
  ASSERT_TRUE(attachments.is_error());
}

TEST_F(DatastoreTest, GetAttachments_OnlyUnknownAttachmentInAllowlist) {
  SetUpDatastore({}, {"unknown.attachment"});

  fit::result<Attachments> attachments = GetAttachments();
  ASSERT_TRUE(attachments.is_error());
}

}  // namespace
}  // namespace feedback
