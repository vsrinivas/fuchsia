// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/data_provider.h"

#include <fuchsia/feedback/cpp/fidl.h>
#include <fuchsia/math/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/fit/result.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <memory>
#include <set>
#include <string>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "garnet/public/lib/fostr/fidl/fuchsia/math/formatting.h"
#include "src/developer/forensics/feedback_data/annotations/types.h"
#include "src/developer/forensics/feedback_data/attachments/types.h"
#include "src/developer/forensics/feedback_data/constants.h"
#include "src/developer/forensics/feedback_data/device_id_provider.h"
#include "src/developer/forensics/feedback_data/integrity_reporter.h"
#include "src/developer/forensics/testing/gmatchers.h"
#include "src/developer/forensics/testing/gpretty_printers.h"
#include "src/developer/forensics/testing/stubs/cobalt_logger_factory.h"
#include "src/developer/forensics/testing/stubs/scenic.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"
#include "src/developer/forensics/utils/archive.h"
#include "src/lib/fsl/vmo/file.h"
#include "src/lib/fsl/vmo/sized_vmo.h"
#include "src/lib/fsl/vmo/vector.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/lib/timekeeper/test_clock.h"
// TODO(fxbug.dev/57392): Move it back to //third_party once unification completes.
#include "zircon/third_party/rapidjson/include/rapidjson/document.h"
#include "zircon/third_party/rapidjson/include/rapidjson/schema.h"

namespace forensics {
namespace feedback_data {
namespace {

using fuchsia::feedback::ImageEncoding;
using fuchsia::feedback::Screenshot;
using fuchsia::feedback::Snapshot;
using testing::UnorderedElementsAreArray;

const AnnotationKeys kDefaultAnnotations = {
    kAnnotationBuildBoard,    kAnnotationBuildLatestCommitDate, kAnnotationBuildProduct,
    kAnnotationBuildVersion,  kAnnotationDeviceBoardName,       kAnnotationDeviceUptime,
    kAnnotationDeviceUTCTime,
};
const AttachmentKeys kDefaultAttachments = {
    kAttachmentBuildSnapshot,
};

constexpr bool kSuccess = true;
constexpr bool kFailure = false;

constexpr zx::duration kDefaultBugReportFlowDuration = zx::usec(5);

// Returns a Screenshot with the right dimensions, no image.
std::unique_ptr<Screenshot> MakeUniqueScreenshot(const size_t image_dim_in_px) {
  std::unique_ptr<Screenshot> screenshot = std::make_unique<Screenshot>();
  screenshot->dimensions_in_px.height = image_dim_in_px;
  screenshot->dimensions_in_px.width = image_dim_in_px;
  return screenshot;
}

// Represents arguments for DataProvider::GetScreenshotCallback.
struct GetScreenshotResponse {
  std::unique_ptr<Screenshot> screenshot;

  // This should be kept in sync with DoGetScreenshotResponseMatch() as we only want to display what
  // we actually compare, for now the presence of a screenshot and its dimensions if present.
  operator std::string() const {
    if (!screenshot) {
      return "no screenshot";
    }
    const fuchsia::math::Size& dimensions_in_px = screenshot->dimensions_in_px;
    return fxl::StringPrintf("a %d x %d screenshot", dimensions_in_px.width,
                             dimensions_in_px.height);
  }

  // This is used by gTest to pretty-prints failed expectations instead of the default byte string.
  friend std::ostream& operator<<(std::ostream& os, const GetScreenshotResponse& response) {
    return os << std::string(response);
  }
};

// Compares two GetScreenshotResponse objects.
//
// This should be kept in sync with std::string() as we only want to display what we actually
// compare, for now the presence of a screenshot and its dimensions.
template <typename ResultListenerT>
bool DoGetScreenshotResponseMatch(const GetScreenshotResponse& actual,
                                  const GetScreenshotResponse& expected,
                                  ResultListenerT* result_listener) {
  if (actual.screenshot == nullptr && expected.screenshot == nullptr) {
    return true;
  }
  if (actual.screenshot == nullptr && expected.screenshot != nullptr) {
    *result_listener << "Got no screenshot, expected one";
    return false;
  }
  if (expected.screenshot == nullptr && actual.screenshot != nullptr) {
    *result_listener << "Expected no screenshot, got one";
    return false;
  }
  // actual.screenshot and expected.screenshot are now valid.

  if (!::fidl::Equals(actual.screenshot->dimensions_in_px, expected.screenshot->dimensions_in_px)) {
    *result_listener << "Expected screenshot dimensions " << expected.screenshot->dimensions_in_px
                     << ", got " << actual.screenshot->dimensions_in_px;
    return false;
  }

  // We do not compare the VMOs.

  return true;
}

// Returns true if gMock |arg| matches |expected|, assuming two GetScreenshotResponse objects.
MATCHER_P(MatchesGetScreenshotResponse, expected, "matches " + std::string(expected.get())) {
  return DoGetScreenshotResponseMatch(arg, expected, result_listener);
}

// Unit-tests the implementation of the fuchsia.feedback.DataProvider FIDL interface.
//
// This does not test the environment service. It directly instantiates the class, without
// connecting through FIDL.
class DataProviderTest : public UnitTestFixture {
 public:
  DataProviderTest() : device_id_provider_(kDeviceIdPath) {}

  void SetUp() override {
    // |cobalt_| owns the test clock through a unique_ptr so we need to allocate |clock_| on the
    // heap and then give |cobalt_| ownership of it. This allows us to control the time perceived by
    // |cobalt_|.
    clock_ = new timekeeper::TestClock();
    cobalt_ = std::make_unique<cobalt::Logger>(dispatcher(), services(),
                                               std::unique_ptr<timekeeper::TestClock>(clock_));
    SetUpCobaltServer(std::make_unique<stubs::CobaltLoggerFactory>());
  }

 protected:
  void SetUpDataProvider(const AnnotationKeys& annotation_allowlist = kDefaultAnnotations,
                         const AttachmentKeys& attachment_allowlist = kDefaultAttachments) {
    datastore_ =
        std::make_unique<Datastore>(dispatcher(), services(), cobalt_.get(), annotation_allowlist,
                                    attachment_allowlist, &device_id_provider_);
    data_provider_ = std::make_unique<DataProvider>(
        dispatcher(), services(), IntegrityReporter(annotation_allowlist, attachment_allowlist),
        cobalt_.get(), datastore_.get());
  }

  void SetUpScenicServer(std::unique_ptr<stubs::ScenicBase> server) {
    scenic_server_ = std::move(server);
    if (scenic_server_) {
      InjectServiceProvider(scenic_server_.get());
    }
  }

  GetScreenshotResponse GetScreenshot() {
    FX_CHECK(data_provider_);

    GetScreenshotResponse out_response;
    data_provider_->GetScreenshot(ImageEncoding::PNG,
                                  [&out_response](std::unique_ptr<Screenshot> screenshot) {
                                    out_response.screenshot = std::move(screenshot);
                                  });
    RunLoopUntilIdle();
    return out_response;
  }

  Snapshot GetSnapshot(zx::duration snapshot_flow_duration = kDefaultBugReportFlowDuration) {
    FX_CHECK(data_provider_ && clock_);

    Snapshot snapshot;

    // We can set |clock_|'s start and end times because the call to start the timer happens
    // independently of the loop while the call to end it happens in a task that is posted on the
    // loop. So, as long the end time is set before the loop is run, a non-zero duration will be
    // recorded.
    clock_->Set(zx::time(0));
    data_provider_->GetSnapshot(fuchsia::feedback::GetSnapshotParameters(),
                                [&snapshot](Snapshot res) { snapshot = std::move(res); });
    clock_->Set(zx::time(0) + snapshot_flow_duration);
    RunLoopUntilIdle();
    return snapshot;
  }

  std::map<std::string, std::string> UnpackSnapshot(const Snapshot& snapshot) {
    FX_CHECK(snapshot.has_archive());
    FX_CHECK(snapshot.archive().key == kSnapshotFilename);
    std::map<std::string, std::string> unpacked_attachments;
    FX_CHECK(Unpack(snapshot.archive().value, &unpacked_attachments));
    return unpacked_attachments;
  }

 private:
  DeviceIdProvider device_id_provider_;
  // The lifetime of |clock_| is managed by |cobalt_|.
  timekeeper::TestClock* clock_;
  std::unique_ptr<cobalt::Logger> cobalt_;
  std::unique_ptr<Datastore> datastore_;

 protected:
  std::unique_ptr<DataProvider> data_provider_;

 private:
  std::unique_ptr<stubs::ScenicBase> scenic_server_;
};

TEST_F(DataProviderTest, GetScreenshot_SucceedOnScenicReturningSuccess) {
  const size_t image_dim_in_px = 100;
  std::vector<stubs::TakeScreenshotResponse> scenic_responses;
  scenic_responses.emplace_back(stubs::CreateCheckerboardScreenshot(image_dim_in_px), kSuccess);
  auto scenic = std::make_unique<stubs::Scenic>();
  scenic->set_take_screenshot_responses(std::move(scenic_responses));
  SetUpScenicServer(std::move(scenic));
  SetUpDataProvider();

  GetScreenshotResponse feedback_response = GetScreenshot();

  ASSERT_NE(feedback_response.screenshot, nullptr);
  EXPECT_EQ(static_cast<size_t>(feedback_response.screenshot->dimensions_in_px.height),
            image_dim_in_px);
  EXPECT_EQ(static_cast<size_t>(feedback_response.screenshot->dimensions_in_px.width),
            image_dim_in_px);
  EXPECT_TRUE(feedback_response.screenshot->image.vmo.is_valid());

  fsl::SizedVmo expected_sized_vmo;
  ASSERT_TRUE(fsl::VmoFromFilename("/pkg/data/checkerboard_100.png", &expected_sized_vmo));
  std::vector<uint8_t> expected_pixels;
  ASSERT_TRUE(fsl::VectorFromVmo(expected_sized_vmo, &expected_pixels));
  std::vector<uint8_t> actual_pixels;
  ASSERT_TRUE(fsl::VectorFromVmo(feedback_response.screenshot->image, &actual_pixels));
  EXPECT_EQ(actual_pixels, expected_pixels);
}

TEST_F(DataProviderTest, GetScreenshot_FailOnScenicNotAvailable) {
  SetUpDataProvider();

  GetScreenshotResponse feedback_response = GetScreenshot();
  EXPECT_EQ(feedback_response.screenshot, nullptr);
}

TEST_F(DataProviderTest, GetScreenshot_FailOnScenicReturningFailure) {
  std::vector<stubs::TakeScreenshotResponse> scenic_responses;
  scenic_responses.emplace_back(stubs::CreateEmptyScreenshot(), kFailure);
  auto scenic = std::make_unique<stubs::Scenic>();
  scenic->set_take_screenshot_responses(std::move(scenic_responses));
  SetUpScenicServer(std::move(scenic));
  SetUpDataProvider();

  GetScreenshotResponse feedback_response = GetScreenshot();
  EXPECT_EQ(feedback_response.screenshot, nullptr);
}

TEST_F(DataProviderTest, GetScreenshot_FailOnScenicReturningNonBGRA8Screenshot) {
  std::vector<stubs::TakeScreenshotResponse> scenic_responses;
  scenic_responses.emplace_back(stubs::CreateNonBGRA8Screenshot(), kSuccess);
  auto scenic = std::make_unique<stubs::Scenic>();
  scenic->set_take_screenshot_responses(std::move(scenic_responses));
  SetUpScenicServer(std::move(scenic));
  SetUpDataProvider();

  GetScreenshotResponse feedback_response = GetScreenshot();
  EXPECT_EQ(feedback_response.screenshot, nullptr);
}

TEST_F(DataProviderTest, GetScreenshot_ParallelRequests) {
  // We simulate three calls to DataProvider::GetScreenshot(): one for which the stub Scenic
  // will return a checkerboard 10x10, one for a 20x20 and one failure.
  const size_t num_calls = 3u;
  const size_t image_dim_in_px_0 = 10u;
  const size_t image_dim_in_px_1 = 20u;
  std::vector<stubs::TakeScreenshotResponse> scenic_responses;
  scenic_responses.emplace_back(stubs::CreateCheckerboardScreenshot(image_dim_in_px_0), kSuccess);
  scenic_responses.emplace_back(stubs::CreateCheckerboardScreenshot(image_dim_in_px_1), kSuccess);
  scenic_responses.emplace_back(stubs::CreateEmptyScreenshot(), kFailure);
  ASSERT_EQ(scenic_responses.size(), num_calls);
  auto scenic = std::make_unique<stubs::Scenic>();
  scenic->set_take_screenshot_responses(std::move(scenic_responses));
  SetUpScenicServer(std::move(scenic));
  SetUpDataProvider();

  std::vector<GetScreenshotResponse> feedback_responses;
  for (size_t i = 0; i < num_calls; i++) {
    data_provider_->GetScreenshot(ImageEncoding::PNG,
                                  [&feedback_responses](std::unique_ptr<Screenshot> screenshot) {
                                    feedback_responses.push_back({std::move(screenshot)});
                                  });
  }
  RunLoopUntilIdle();
  EXPECT_EQ(feedback_responses.size(), num_calls);

  // We cannot assume that the order of the DataProvider::GetScreenshot() calls match the order
  // of the Scenic::TakeScreenshot() callbacks because of the async message loop. Thus we need to
  // match them as sets.
  //
  // We set the expectations in advance and then pass a reference to the gMock matcher using
  // testing::ByRef() because the underlying VMO is not copyable.
  const GetScreenshotResponse expected_0 = {MakeUniqueScreenshot(image_dim_in_px_0)};
  const GetScreenshotResponse expected_1 = {MakeUniqueScreenshot(image_dim_in_px_1)};
  const GetScreenshotResponse expected_2 = {nullptr};
  EXPECT_THAT(feedback_responses, testing::UnorderedElementsAreArray({
                                      MatchesGetScreenshotResponse(testing::ByRef(expected_0)),
                                      MatchesGetScreenshotResponse(testing::ByRef(expected_1)),
                                      MatchesGetScreenshotResponse(testing::ByRef(expected_2)),
                                  }));

  // Additionally, we check that in the non-empty responses, the VMO is valid.
  for (const auto& response : feedback_responses) {
    if (response.screenshot == nullptr) {
      continue;
    }
    EXPECT_TRUE(response.screenshot->image.vmo.is_valid());
    EXPECT_GE(response.screenshot->image.size, 0u);
  }
}

TEST_F(DataProviderTest, GetSnapshot_SmokeTest) {
  SetUpDataProvider();

  Snapshot snapshot = GetSnapshot();

  // There is not much we can assert here as no missing annotation nor attachment is fatal and we
  // cannot expect annotations or attachments to be present.

  // If there are annotations, there should also be the snapshot.
  if (snapshot.has_annotations()) {
    ASSERT_TRUE(snapshot.has_archive());
  }

  EXPECT_THAT(ReceivedCobaltEvents(), UnorderedElementsAreArray({
                                          cobalt::Event(cobalt::SnapshotGenerationFlow::kSuccess,
                                                        kDefaultBugReportFlowDuration.to_usecs()),
                                      }));
}

TEST_F(DataProviderTest, GetSnapshot_AnnotationsAsAttachment) {
  SetUpDataProvider();

  Snapshot snapshot = GetSnapshot();
  auto unpacked_attachments = UnpackSnapshot(snapshot);

  // There should be an "annotations.json" attachment present in the snapshot.
  ASSERT_NE(unpacked_attachments.find(kAttachmentAnnotations), unpacked_attachments.end());
  const std::string annotations_json = unpacked_attachments[kAttachmentAnnotations];
  ASSERT_FALSE(annotations_json.empty());

  // JSON verification.
  // We check that the output is a valid JSON and that it matches the schema.
  rapidjson::Document json;
  ASSERT_FALSE(json.Parse(annotations_json.c_str()).HasParseError());
  rapidjson::Document schema_json;
  ASSERT_FALSE(
      schema_json
          .Parse(fxl::StringPrintf(
              R"({
  "type": "object",
 "properties": {
    "%s": {
      "type": "string"
    },
    "%s": {
      "type": "string"
    },
    "%s": {
      "type": "string"
    },
    "%s": {
      "type": "string"
    },
    "%s": {
      "type": "string"
    },
    "%s": {
      "type": "string"
    },
    "%s": {
      "type": "string"
    },
    "%s": {
      "type": "string"
    }
  },
  "additionalProperties": false
})",
              kAnnotationBuildBoard, kAnnotationBuildIsDebug, kAnnotationBuildLatestCommitDate,
              kAnnotationBuildProduct, kAnnotationBuildVersion, kAnnotationDeviceBoardName,
              kAnnotationDeviceUptime, kAnnotationDeviceUTCTime))
          .HasParseError());
  rapidjson::SchemaDocument schema(schema_json);
  rapidjson::SchemaValidator validator(schema);
  EXPECT_TRUE(json.Accept(validator));
}

TEST_F(DataProviderTest, GetSnapshot_ManifestAsAttachment) {
  SetUpDataProvider();

  Snapshot snapshot = GetSnapshot();
  auto unpacked_attachments = UnpackSnapshot(snapshot);

  // There should be a "manifest.json" attachment present in the snapshot.
  ASSERT_NE(unpacked_attachments.find(kAttachmentManifest), unpacked_attachments.end());
}

TEST_F(DataProviderTest, GetSnapshot_SingleAttachmentOnEmptyAttachmentAllowlist) {
  SetUpDataProvider(kDefaultAnnotations, /*attachment_allowlist=*/{});

  Snapshot snapshot = GetSnapshot();
  auto unpacked_attachments = UnpackSnapshot(snapshot);
  ASSERT_NE(unpacked_attachments.find(kAttachmentAnnotations), unpacked_attachments.end());
}

TEST_F(DataProviderTest, GetSnapshot_NoDataOnEmptyAllowlists) {
  SetUpDataProvider(/*annotation_allowlist=*/{}, /*attachment_allowlist=*/{});

  Snapshot snapshot = GetSnapshot();
  EXPECT_FALSE(snapshot.has_archive());
  EXPECT_TRUE(snapshot.has_annotations());
  EXPECT_THAT(snapshot.annotations(), UnorderedElementsAreArray({
                                          MatchesAnnotation(kAnnotationDebugSnapshotPoolSize, "1"),
                                      }));
}

}  // namespace
}  // namespace feedback_data
}  // namespace forensics
