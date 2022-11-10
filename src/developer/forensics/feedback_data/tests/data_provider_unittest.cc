// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/data_provider.h"

#include <fuchsia/feedback/cpp/fidl.h>
#include <fuchsia/math/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/fpromise/result.h>
#include <lib/inspect/cpp/vmo/types.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>
#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/feedback/annotations/annotation_manager.h"
#include "src/developer/forensics/feedback/annotations/types.h"
#include "src/developer/forensics/feedback/attachments/types.h"
#include "src/developer/forensics/feedback_data/constants.h"
#include "src/developer/forensics/feedback_data/metadata.h"
#include "src/developer/forensics/testing/gmatchers.h"
#include "src/developer/forensics/testing/gpretty_printers.h"
#include "src/developer/forensics/testing/stubs/cobalt_logger_factory.h"
#include "src/developer/forensics/testing/stubs/scenic.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"
#include "src/developer/forensics/utils/archive.h"
#include "src/lib/fostr/fidl/fuchsia/math/formatting.h"
#include "src/lib/fsl/vmo/file.h"
#include "src/lib/fsl/vmo/sized_vmo.h"
#include "src/lib/fsl/vmo/vector.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/lib/timekeeper/test_clock.h"
#include "third_party/rapidjson/include/rapidjson/document.h"
#include "third_party/rapidjson/include/rapidjson/schema.h"

namespace forensics {
namespace feedback_data {
namespace {

using fuchsia::feedback::Annotation;
using fuchsia::feedback::ImageEncoding;
using fuchsia::feedback::Screenshot;
using fuchsia::feedback::Snapshot;
using testing::UnorderedElementsAreArray;

const std::set<std::string> kDefaultAnnotations = {
    feedback::kBuildBoardKey, feedback::kBuildLatestCommitDateKey, feedback::kBuildProductKey,
    feedback::kBuildVersionKey, feedback::kDeviceBoardNameKey};

constexpr bool kSuccess = true;
constexpr bool kFailure = false;

constexpr zx::duration kDefaultSnapshotFlowDuration = zx::usec(5);

// Timeout for a single asynchronous piece of data, e.g., syslog collection, if the client didn't
// specify one.
//
// 30s seems reasonable to collect everything.
constexpr zx::duration kDefaultDataTimeout = zx::sec(30);

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
  void SetUp() override {
    cobalt_ = std::make_unique<cobalt::Logger>(dispatcher(), services(), &clock_);
    SetUpCobaltServer(std::make_unique<stubs::CobaltLoggerFactory>());

    inspect_node_manager_ = std::make_unique<InspectNodeManager>(&InspectRoot());
    inspect_data_budget_ = std::make_unique<InspectDataBudget>(
        "non-existent_path", inspect_node_manager_.get(), cobalt_.get());
  }

 protected:
  void SetUpDataProvider(
      const std::set<std::string>& annotation_allowlist = kDefaultAnnotations,
      const feedback::AttachmentKeys& attachment_allowlist = {},
      const std::map<std::string, ErrorOr<std::string>>& startup_annotations = {},
      const std::map<std::string, feedback::AttachmentValue>& static_attachments = {}) {
    std::set<std::string> allowlist;
    for (const auto& [k, v] : startup_annotations) {
      allowlist.insert(k);
    }
    annotation_manager_ =
        std::make_unique<feedback::AnnotationManager>(dispatcher(), allowlist, startup_annotations);
    attachment_manager_ = std::make_unique<feedback::AttachmentManager>(
        dispatcher(), attachment_allowlist, static_attachments);
    data_provider_ = std::make_unique<DataProvider>(
        dispatcher(), services(), &clock_, &redactor_, /*is_first_instance=*/true,
        annotation_allowlist, attachment_allowlist, cobalt_.get(), annotation_manager_.get(),
        attachment_manager_.get(), inspect_data_budget_.get());
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

  Snapshot GetSnapshot(std::optional<zx::channel> channel = std::nullopt,
                       zx::duration snapshot_flow_duration = kDefaultSnapshotFlowDuration) {
    FX_CHECK(data_provider_);

    Snapshot snapshot;

    // We can set |clock_|'s start and end times because the call to start the timer happens
    // independently of the loop while the call to end it happens in a task that is posted on the
    // loop. So, as long the end time is set before the loop is run, a non-zero duration will be
    // recorded.
    clock_.Set(zx::time(0));
    fuchsia::feedback::GetSnapshotParameters params;
    if (channel) {
      params.set_response_channel(*std::move(channel));
    }
    data_provider_->GetSnapshot(std::move(params),
                                [&snapshot](Snapshot res) { snapshot = std::move(res); });
    clock_.Set(zx::time(0) + snapshot_flow_duration);
    RunLoopUntilIdle();
    return snapshot;
  }

  std::pair<feedback::Annotations, fuchsia::feedback::Attachment> GetSnapshotInternal(
      zx::duration snapshot_flow_duration = kDefaultSnapshotFlowDuration) {
    FX_CHECK(data_provider_);

    feedback::Annotations annotations;
    fuchsia::feedback::Attachment archive;

    // We can set |clock_|'s start and end times because the call to start the timer happens
    // independently of the loop while the call to end it happens in a task that is posted on the
    // loop. So, as long the end time is set before the loop is run, a non-zero duration will be
    // recorded.
    clock_.Set(zx::time(0));
    data_provider_->GetSnapshotInternal(
        kDefaultDataTimeout, [&annotations, &archive](feedback::Annotations resultAnnotations,
                                                      fuchsia::feedback::Attachment resultArchive) {
          annotations = std::move(resultAnnotations);
          archive = std::move(resultArchive);
        });
    clock_.Set(zx::time(0) + snapshot_flow_duration);
    RunLoopUntilIdle();
    return {std::move(annotations), std::move(archive)};
  }

  size_t NumCurrentServedArchives() { return data_provider_->NumCurrentServedArchives(); }

  std::map<std::string, std::string> UnpackSnapshot(const Snapshot& snapshot) {
    FX_CHECK(snapshot.has_archive());
    FX_CHECK(snapshot.archive().key == kSnapshotFilename);
    std::map<std::string, std::string> unpacked_attachments;
    FX_CHECK(Unpack(snapshot.archive().value, &unpacked_attachments));
    return unpacked_attachments;
  }

 private:
  timekeeper::TestClock clock_;
  std::unique_ptr<feedback::AnnotationManager> annotation_manager_;
  std::unique_ptr<cobalt::Logger> cobalt_;
  IdentityRedactor redactor_{inspect::BoolProperty()};
  std::unique_ptr<feedback::AttachmentManager> attachment_manager_;

 protected:
  std::unique_ptr<DataProvider> data_provider_;

 private:
  std::unique_ptr<stubs::ScenicBase> scenic_server_;
  std::unique_ptr<InspectNodeManager> inspect_node_manager_;
  std::unique_ptr<InspectDataBudget> inspect_data_budget_;
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

  // There will always be a "manifest.json" so there will always be an archive.

  ASSERT_TRUE(snapshot.has_archive());

  const auto archive_size = snapshot.archive().value.size;
  ASSERT_TRUE(archive_size > 0);

  EXPECT_THAT(ReceivedCobaltEvents(),
              UnorderedElementsAreArray({
                  cobalt::Event(cobalt::SnapshotGenerationFlow::kSuccess,
                                kDefaultSnapshotFlowDuration.to_usecs()),
                  cobalt::Event(cobalt::SnapshotVersion::kV_01, archive_size),
              }));
}

TEST_F(DataProviderTest, GetSnapshotInvalidChannel) {
  SetUpDataProvider();

  zx::channel server_end;

  ASSERT_EQ(NumCurrentServedArchives(), 0u);
  GetSnapshot(std::optional<zx::channel>(std::move(server_end)));

  RunLoopUntilIdle();
  ASSERT_EQ(NumCurrentServedArchives(), 0u);
}

TEST_F(DataProviderTest, GetSnapshotViaChannel) {
  SetUpDataProvider();

  zx::channel server_end, client_end;
  ZX_ASSERT(zx::channel::create(0, &client_end, &server_end) == ZX_OK);

  ASSERT_EQ(NumCurrentServedArchives(), 0u);
  Snapshot snapshot = GetSnapshot(std::optional<zx::channel>(std::move(server_end)));

  RunLoopUntilIdle();
  ASSERT_EQ(NumCurrentServedArchives(), 1u);

  {
    // Archive sent through channel, so no archive here in snapshot.
    ASSERT_FALSE(snapshot.has_archive());

    fuchsia::io::FilePtr archive;
    archive.Bind(std::move(client_end));
    ASSERT_TRUE(archive.is_bound());

    // Get archive attributes.
    uint64_t archive_size;
    archive->GetAttr([&archive_size](zx_status_t status, fuchsia::io::NodeAttributes attributes) {
      ASSERT_EQ(ZX_OK, status);
      archive_size = attributes.content_size;
    });

    RunLoopUntilIdle();
    ASSERT_TRUE(archive_size > 0);

    uint64_t read_count = 0;
    uint64_t increment = 0;
    do {
      archive->Read(fuchsia::io::MAX_BUF, [&increment](fuchsia::io::Readable_Read_Result result) {
        EXPECT_TRUE(result.is_response()) << zx_status_get_string(result.err());
        increment = result.response().data.size();
      });
      RunLoopUntilIdle();
      read_count += increment;
    } while (increment);

    ASSERT_EQ(archive_size, read_count);

    EXPECT_THAT(ReceivedCobaltEvents(),
                UnorderedElementsAreArray({
                    cobalt::Event(cobalt::SnapshotGenerationFlow::kSuccess,
                                  kDefaultSnapshotFlowDuration.to_usecs()),
                    cobalt::Event(cobalt::SnapshotVersion::kV_01, archive_size),
                }));
  }

  // The channel went out of scope
  RunLoopUntilIdle();
  ASSERT_EQ(NumCurrentServedArchives(), 0u);
}

TEST_F(DataProviderTest, GetMultipleSnapshotViaChannel) {
  SetUpDataProvider();

  zx::channel server_end_1, client_end_1;
  zx::channel server_end_2, client_end_2;
  zx::channel server_end_3, client_end_3;
  ZX_ASSERT(zx::channel::create(0, &client_end_1, &server_end_1) == ZX_OK);
  ZX_ASSERT(zx::channel::create(0, &client_end_2, &server_end_2) == ZX_OK);
  ZX_ASSERT(zx::channel::create(0, &client_end_3, &server_end_3) == ZX_OK);

  ASSERT_EQ(NumCurrentServedArchives(), 0u);

  // Serve clients.
  GetSnapshot(std::optional<zx::channel>(std::move(server_end_1)));
  RunLoopUntilIdle();
  ASSERT_EQ(NumCurrentServedArchives(), 1u);

  GetSnapshot(std::optional<zx::channel>(std::move(server_end_2)));
  RunLoopUntilIdle();
  ASSERT_EQ(NumCurrentServedArchives(), 2u);

  GetSnapshot(std::optional<zx::channel>(std::move(server_end_3)));
  RunLoopUntilIdle();
  ASSERT_EQ(NumCurrentServedArchives(), 3u);

  // Close clients.
  client_end_2.reset();
  RunLoopUntilIdle();
  ASSERT_EQ(NumCurrentServedArchives(), 2u);

  client_end_1.reset();
  RunLoopUntilIdle();
  ASSERT_EQ(NumCurrentServedArchives(), 1u);

  client_end_3.reset();
  RunLoopUntilIdle();
  ASSERT_EQ(NumCurrentServedArchives(), 0u);
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
  ASSERT_FALSE(schema_json
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
    }
  },
  "additionalProperties": false
})",
                       feedback::kBuildBoardKey, feedback::kBuildIsDebugKey,
                       feedback::kBuildLatestCommitDateKey, feedback::kBuildProductKey,
                       feedback::kBuildVersionKey, feedback::kDeviceBoardNameKey))
                   .HasParseError());
  rapidjson::SchemaDocument schema(schema_json);
  rapidjson::SchemaValidator validator(schema);
  EXPECT_TRUE(json.Accept(validator));
}

TEST_F(DataProviderTest, GetSnapshot_ManifestAsAttachment) {
  SetUpDataProvider();

  Snapshot snapshot = GetSnapshot();
  auto unpacked_attachments = UnpackSnapshot(snapshot);

  // There should be a "metadata.json" attachment present in the snapshot.
  ASSERT_NE(unpacked_attachments.find(kAttachmentMetadata), unpacked_attachments.end());
}

TEST_F(DataProviderTest, GetSnapshot_SingleAttachmentOnEmptyAttachmentAllowlist) {
  SetUpDataProvider(kDefaultAnnotations, /*attachment_allowlist=*/{});

  Snapshot snapshot = GetSnapshot();
  auto unpacked_attachments = UnpackSnapshot(snapshot);
  EXPECT_EQ(unpacked_attachments.count(kAttachmentAnnotations), 1u);
}

TEST_F(DataProviderTest, GetSnapshot_ErrorAnnotationsNotInFidl) {
  SetUpDataProvider(kDefaultAnnotations, /*attachment_allowlist=*/{},
                    {{"annotation1", Error::kMissingValue}});

  Snapshot snapshot = GetSnapshot();
  EXPECT_FALSE(snapshot.has_annotations());
}

TEST_F(DataProviderTest, GetSnapshotUnfilteredAnnotations_DoesNotFilterMissingAnnotations) {
  SetUpDataProvider(kDefaultAnnotations, /*attachment_allowlist=*/{},
                    {{"annotation1", Error::kMissingValue}});

  auto [annotations, archive] = GetSnapshotInternal();
  EXPECT_EQ(annotations.size(), 1u);
  EXPECT_TRUE(annotations.find("annotation1") != annotations.end());
}

TEST_F(DataProviderTest, GetSnapshotUnfilteredAnnotations_ReturnsFilledArchive) {
  SetUpDataProvider(kDefaultAnnotations, /*attachment_allowlist=*/{});

  auto [annotations, archive] = GetSnapshotInternal();
  EXPECT_TRUE(archive.value.size > 0u);
}

}  // namespace
}  // namespace feedback_data
}  // namespace forensics
