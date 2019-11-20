// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_agent/data_provider.h"

#include <fuchsia/feedback/cpp/fidl.h>
#include <fuchsia/math/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/fit/result.h>
#include <lib/fostr/fidl/fuchsia/math/formatting.h>
#include <lib/sys/cpp/testing/test_with_environment.h>
#include <lib/syslog/logger.h>
#include <lib/zx/time.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <memory>
#include <set>
#include <string>
#include <vector>

#include "src/developer/feedback/feedback_agent/config.h"
#include "src/developer/feedback/feedback_agent/constants.h"
#include "src/developer/feedback/feedback_agent/tests/stub_channel_provider.h"
#include "src/developer/feedback/feedback_agent/tests/stub_logger.h"
#include "src/developer/feedback/feedback_agent/tests/stub_scenic.h"
#include "src/developer/feedback/testing/gmatchers.h"
#include "src/developer/feedback/testing/gpretty_printers.h"
#include "src/developer/feedback/testing/unit_test_fixture.h"
#include "src/developer/feedback/utils/archive.h"
#include "src/lib/fsl/vmo/file.h"
#include "src/lib/fsl/vmo/sized_vmo.h"
#include "src/lib/fsl/vmo/strings.h"
#include "src/lib/fsl/vmo/vector.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/lib/fxl/strings/substitute.h"
#include "src/lib/fxl/test/test_settings.h"
#include "src/lib/syslog/cpp/logger.h"
#include "third_party/googletest/googlemock/include/gmock/gmock.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"
#include "third_party/rapidjson/include/rapidjson/document.h"
#include "third_party/rapidjson/include/rapidjson/schema.h"

namespace feedback {
namespace {

using fuchsia::feedback::Attachment;
using fuchsia::feedback::Data;
using fuchsia::feedback::ImageEncoding;
using fuchsia::feedback::Screenshot;

const std::set<std::string> kDefaultAnnotations = {
    kAnnotationBuildBoard,      kAnnotationBuildIsDebug, kAnnotationBuildLatestCommitDate,
    kAnnotationBuildProduct,    kAnnotationBuildVersion, kAnnotationChannel,
    kAnnotationDeviceBoardName, kAnnotationDeviceUptime,
};
const std::set<std::string> kDefaultAttachments = {
    kAttachmentBuildSnapshot,
    // TODO(fxb/39804): re-enable once using Inspect service.
    // kAttachmentInspect,
    kAttachmentLogKernel,
    kAttachmentLogSystem,
};
const Config kDefaultConfig = Config{kDefaultAnnotations, kDefaultAttachments};

constexpr bool kSuccess = true;
constexpr bool kFailure = false;

constexpr zx::duration kDataProviderIdleTimeout = zx::sec(5);

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

  if (!fidl::Equals(actual.screenshot->dimensions_in_px, expected.screenshot->dimensions_in_px)) {
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
  void SetUp() override { SetUpDataProvider(kDefaultConfig); }

 protected:
  void SetUpDataProvider(const Config& config) {
    data_provider_.reset(new DataProvider(
        dispatcher(), services(), config, [this] { data_provider_timed_out_ = true; },
        kDataProviderIdleTimeout));
  }

  void SetUpDataProviderOnlyRequestingChannel(zx::duration timeout) {
    data_provider_.reset(new DataProvider(
        dispatcher(), services(), Config{{kAnnotationChannel}, {}},
        [this] { data_provider_timed_out_ = true; }, timeout));
  }

  void SetUpScenic(std::unique_ptr<StubScenic> scenic) {
    scenic_ = std::move(scenic);
    if (scenic_) {
      InjectServiceProvider(scenic_.get());
    }
  }

  void SetUpLogger(const std::vector<fuchsia::logger::LogMessage>& messages) {
    logger_.reset(new StubLogger());
    logger_->set_messages(messages);
    InjectServiceProvider(logger_.get());
  }

  void SetUpChannelProvider(std::unique_ptr<StubChannelProvider> channel_provider) {
    channel_provider_ = std::move(channel_provider);
    if (channel_provider_) {
      InjectServiceProvider(channel_provider_.get());
    }
  }

  GetScreenshotResponse GetScreenshot() {
    GetScreenshotResponse out_response;
    data_provider_->GetScreenshot(ImageEncoding::PNG,
                                  [&out_response](std::unique_ptr<Screenshot> screenshot) {
                                    out_response.screenshot = std::move(screenshot);
                                  });
    RunLoopUntilIdle();
    return out_response;
  }

  fit::result<Data, zx_status_t> GetData() {
    fit::result<Data, zx_status_t> out_result;
    data_provider_->GetData(
        [&out_result](fit::result<Data, zx_status_t> result) { out_result = std::move(result); });
    RunLoopUntilIdle();
    return out_result;
  }

  void UnpackAttachmentBundle(const Data& data, std::vector<Attachment>* unpacked_attachments) {
    ASSERT_TRUE(data.has_attachment_bundle());
    const auto& attachment_bundle = data.attachment_bundle();
    EXPECT_STREQ(attachment_bundle.key.c_str(), kAttachmentBundle);
    ASSERT_TRUE(Unpack(attachment_bundle.value, unpacked_attachments));
    EXPECT_EQ(unpacked_attachments->size(), data.attachments().size());
  }

  uint64_t total_num_scenic_bindings() { return scenic_->total_num_bindings(); }
  size_t current_num_scenic_bindings() { return scenic_->current_num_bindings(); }
  const std::vector<TakeScreenshotResponse>& get_scenic_responses() const {
    return scenic_->take_screenshot_responses();
  }

  std::unique_ptr<DataProvider> data_provider_;
  bool data_provider_timed_out_ = false;

 private:
  std::unique_ptr<StubChannelProvider> channel_provider_;
  std::unique_ptr<StubScenic> scenic_;
  std::unique_ptr<StubLogger> logger_;
};

TEST_F(DataProviderTest, GetScreenshot_SucceedOnScenicReturningSuccess) {
  const size_t image_dim_in_px = 100;
  std::vector<TakeScreenshotResponse> scenic_responses;
  scenic_responses.emplace_back(CreateCheckerboardScreenshot(image_dim_in_px), kSuccess);
  auto scenic = std::make_unique<StubScenic>();
  scenic->set_take_screenshot_responses(std::move(scenic_responses));
  SetUpScenic(std::move(scenic));

  GetScreenshotResponse feedback_response = GetScreenshot();

  EXPECT_TRUE(get_scenic_responses().empty());

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
  SetUpScenic(nullptr);
  GetScreenshotResponse feedback_response = GetScreenshot();
  EXPECT_EQ(feedback_response.screenshot, nullptr);
}

TEST_F(DataProviderTest, GetScreenshot_FailOnScenicReturningFailure) {
  std::vector<TakeScreenshotResponse> scenic_responses;
  scenic_responses.emplace_back(CreateEmptyScreenshot(), kFailure);
  auto scenic = std::make_unique<StubScenic>();
  scenic->set_take_screenshot_responses(std::move(scenic_responses));
  SetUpScenic(std::move(scenic));

  GetScreenshotResponse feedback_response = GetScreenshot();

  EXPECT_TRUE(get_scenic_responses().empty());

  EXPECT_EQ(feedback_response.screenshot, nullptr);
}

TEST_F(DataProviderTest, GetScreenshot_FailOnScenicReturningNonBGRA8Screenshot) {
  std::vector<TakeScreenshotResponse> scenic_responses;
  scenic_responses.emplace_back(CreateNonBGRA8Screenshot(), kSuccess);
  auto scenic = std::make_unique<StubScenic>();
  scenic->set_take_screenshot_responses(std::move(scenic_responses));
  SetUpScenic(std::move(scenic));

  GetScreenshotResponse feedback_response = GetScreenshot();

  EXPECT_TRUE(get_scenic_responses().empty());

  EXPECT_EQ(feedback_response.screenshot, nullptr);
}

TEST_F(DataProviderTest, GetScreenshot_ParallelRequests) {
  // We simulate three calls to DataProvider::GetScreenshot(): one for which the stub Scenic
  // will return a checkerboard 10x10, one for a 20x20 and one failure.
  const size_t num_calls = 3u;
  const size_t image_dim_in_px_0 = 10u;
  const size_t image_dim_in_px_1 = 20u;
  std::vector<TakeScreenshotResponse> scenic_responses;
  scenic_responses.emplace_back(CreateCheckerboardScreenshot(image_dim_in_px_0), kSuccess);
  scenic_responses.emplace_back(CreateCheckerboardScreenshot(image_dim_in_px_1), kSuccess);
  scenic_responses.emplace_back(CreateEmptyScreenshot(), kFailure);
  ASSERT_EQ(scenic_responses.size(), num_calls);
  auto scenic = std::make_unique<StubScenic>();
  scenic->set_take_screenshot_responses(std::move(scenic_responses));
  SetUpScenic(std::move(scenic));

  std::vector<GetScreenshotResponse> feedback_responses;
  for (size_t i = 0; i < num_calls; i++) {
    data_provider_->GetScreenshot(ImageEncoding::PNG,
                                  [&feedback_responses](std::unique_ptr<Screenshot> screenshot) {
                                    feedback_responses.push_back({std::move(screenshot)});
                                  });
  }
  RunLoopUntilIdle();
  EXPECT_EQ(feedback_responses.size(), num_calls);
  EXPECT_TRUE(get_scenic_responses().empty());

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

TEST_F(DataProviderTest, GetScreenshot_OneScenicConnectionPerGetScreenshotCall) {
  // We use a stub that always returns false as we are not interested in the responses.
  SetUpScenic(std::make_unique<StubScenicAlwaysReturnsFalse>());

  const size_t num_calls = 5u;
  std::vector<GetScreenshotResponse> feedback_responses;
  for (size_t i = 0; i < num_calls; i++) {
    data_provider_->GetScreenshot(ImageEncoding::PNG,
                                  [&feedback_responses](std::unique_ptr<Screenshot> screenshot) {
                                    feedback_responses.push_back({std::move(screenshot)});
                                  });
  }
  RunLoopUntilIdle();
  EXPECT_EQ(feedback_responses.size(), num_calls);

  EXPECT_EQ(total_num_scenic_bindings(), num_calls);
  // The unbinding is asynchronous so we need to run the loop until all the outstanding connections
  // are actually close in the stub.
  RunLoopUntilIdle();
  EXPECT_EQ(current_num_scenic_bindings(), 0u);
}

TEST_F(DataProviderTest, GetData_SmokeTest) {
  fit::result<Data, zx_status_t> result = GetData();

  ASSERT_TRUE(result.is_ok());

  // There is not much we can assert here as no missing annotation nor attachment is fatal and we
  // cannot expect annotations or attachments to be present.

  const Data& data = result.value();

  // If there are annotations, there should be at least one attachment.
  if (data.has_annotations()) {
    ASSERT_TRUE(data.has_attachments());
  }

  // If there are attachments, there should be an attachment bundle with the same number of
  // attachments once unpacked.
  if (data.has_attachments()) {
    std::vector<Attachment> unpacked_attachments;
    UnpackAttachmentBundle(data, &unpacked_attachments);
  }
}

TEST_F(DataProviderTest, GetData_AnnotationsAsAttachment) {
  fit::result<Data, zx_status_t> result = GetData();

  ASSERT_TRUE(result.is_ok());

  const Data& data = result.value();

  // There should be an "annotations.json" attachment.
  ASSERT_TRUE(data.has_attachments());
  bool found_annotations_attachment = false;
  std::string annotations_json;
  for (const auto& attachment : data.attachments()) {
    if (attachment.key != kAttachmentAnnotations) {
      continue;
    }
    found_annotations_attachment = true;

    ASSERT_TRUE(fsl::StringFromVmo(attachment.value, &annotations_json));
    ASSERT_FALSE(annotations_json.empty());

    // JSON verification.
    // We check that the output is a valid JSON and that it matches the schema.
    rapidjson::Document json;
    ASSERT_FALSE(json.Parse(annotations_json.c_str()).HasParseError());
    rapidjson::Document schema_json;
    ASSERT_FALSE(
        schema_json
            .Parse(fxl::Substitute(R"({
  "type": "object",
  "properties": {
    "$0": {
      "type": "string"
    },
    "$1": {
      "type": "string"
    },
    "$2": {
      "type": "string"
    },
    "$3": {
      "type": "string"
    },
    "$4": {
      "type": "string"
    },
    "$5": {
      "type": "string"
    },
    "$6": {
      "type": "string"
    },
    "$7": {
      "type": "string"
    }
  },
  "additionalProperties": false
})",
                                   kAnnotationBuildBoard, kAnnotationBuildIsDebug,
                                   kAnnotationBuildLatestCommitDate, kAnnotationBuildProduct,
                                   kAnnotationBuildVersion, kAnnotationChannel,
                                   kAnnotationDeviceBoardName, kAnnotationDeviceUptime))
            .HasParseError());
    rapidjson::SchemaDocument schema(schema_json);
    rapidjson::SchemaValidator validator(schema);
    EXPECT_TRUE(json.Accept(validator));
  }
  EXPECT_TRUE(found_annotations_attachment);

  // That same "annotations.json" attachment should be present in the attachment bundle.
  std::vector<Attachment> unpacked_attachments;
  UnpackAttachmentBundle(data, &unpacked_attachments);
  EXPECT_THAT(unpacked_attachments,
              testing::Contains(MatchesAttachment(kAttachmentAnnotations, annotations_json)));
}

TEST_F(DataProviderTest, GetData_SysLog) {
  // CollectSystemLogs() has its own set of unit tests so we only cover one log message here to
  // check that we are attaching the logs.
  SetUpLogger({
      BuildLogMessage(FX_LOG_INFO, "log message",
                      /*timestamp_offset=*/zx::duration(0), {"foo"}),
  });
  const std::string expected_syslog = "[15604.000][07559][07687][foo] INFO: log message\n";

  fit::result<Data, zx_status_t> result = GetData();

  ASSERT_TRUE(result.is_ok());

  const Data& data = result.value();

  // There should be a "log.system.txt" attachment.
  ASSERT_TRUE(data.has_attachments());
  EXPECT_THAT(data.attachments(),
              testing::Contains(MatchesAttachment(kAttachmentLogSystem, expected_syslog)));

  // That same "log.system.txt" attachment should be present in the attachment bundle.
  std::vector<Attachment> unpacked_attachments;
  UnpackAttachmentBundle(data, &unpacked_attachments);
  EXPECT_THAT(unpacked_attachments,
              testing::Contains(MatchesAttachment(kAttachmentLogSystem, expected_syslog)));
}

TEST_F(DataProviderTest, GetData_Channel) {
  auto channel_provider = std::make_unique<StubChannelProvider>();
  channel_provider->set_channel("my-channel");
  SetUpChannelProvider(std::move(channel_provider));

  fit::result<Data, zx_status_t> result = GetData();

  ASSERT_TRUE(result.is_ok());

  const Data& data = result.value();
  ASSERT_TRUE(data.has_annotations());
  EXPECT_THAT(data.annotations(),
              testing::Contains(MatchesAnnotation(kAnnotationChannel, "my-channel")));
}

TEST_F(DataProviderTest, GetData_Uptime) {
  fit::result<Data, zx_status_t> result = GetData();

  ASSERT_TRUE(result.is_ok());

  const Data& data = result.value();
  ASSERT_TRUE(data.has_annotations());
  EXPECT_THAT(data.annotations(), testing::Contains(MatchesKey(kAnnotationDeviceUptime)));
}

TEST_F(DataProviderTest, GetData_EmptyAnnotationAllowlist) {
  SetUpDataProvider(Config{/*annotation_allowlist=*/{}, kDefaultAttachments});

  fit::result<Data, zx_status_t> result = GetData();
  ASSERT_TRUE(result.is_ok());

  const Data& data = result.value();
  EXPECT_FALSE(data.has_annotations());
}

TEST_F(DataProviderTest, GetData_EmptyAttachmentAllowlist) {
  SetUpDataProvider(Config{kDefaultAnnotations, /*attachment_allowlist=*/{}});

  fit::result<Data, zx_status_t> result = GetData();
  ASSERT_TRUE(result.is_ok());

  const Data& data = result.value();
  EXPECT_TRUE(data.has_attachments());
  ASSERT_EQ(data.attachments().size(), 1u);
  EXPECT_STREQ(data.attachments()[0].key.c_str(), kAttachmentAnnotations);
  std::vector<Attachment> unpacked_attachments;
  UnpackAttachmentBundle(data, &unpacked_attachments);
  EXPECT_THAT(unpacked_attachments, testing::Contains(MatchesKey(kAttachmentAnnotations)));
}

TEST_F(DataProviderTest, GetData_EmptyAllowlists) {
  SetUpDataProvider(Config{/*annotation_allowlist=*/{}, /*attachment_allowlist=*/{}});

  fit::result<Data, zx_status_t> result = GetData();
  ASSERT_TRUE(result.is_ok());

  const Data& data = result.value();
  EXPECT_FALSE(data.has_annotations());
  EXPECT_FALSE(data.has_attachments());
  EXPECT_FALSE(data.has_attachment_bundle());
}

TEST_F(DataProviderTest, GetData_UnknownAllowlistedAnnotation) {
  SetUpDataProvider(Config{/*annotation_allowlist=*/{"unknown.annotation"}, kDefaultAttachments});

  fit::result<Data, zx_status_t> result = GetData();
  ASSERT_TRUE(result.is_ok());

  const Data& data = result.value();
  EXPECT_FALSE(data.has_annotations());
}

TEST_F(DataProviderTest, GetData_UnknownAllowlistedAttachment) {
  SetUpDataProvider(Config{kDefaultAnnotations,
                           /*attachment_allowlist=*/{"unknown.attachment"}});

  fit::result<Data, zx_status_t> result = GetData();
  ASSERT_TRUE(result.is_ok());

  const Data& data = result.value();
  EXPECT_TRUE(data.has_attachments());
  ASSERT_EQ(data.attachments().size(), 1u);
  EXPECT_STREQ(data.attachments()[0].key.c_str(), kAttachmentAnnotations);
  std::vector<Attachment> unpacked_attachments;
  UnpackAttachmentBundle(data, &unpacked_attachments);
  EXPECT_THAT(unpacked_attachments, testing::Contains(MatchesKey(kAttachmentAnnotations)));
}

TEST_F(DataProviderTest, Check_IdleTimeout) {
  // This test checks that requests to the data provider properly delay the idle timeout function
  // that data provider executes and that said function runs after data provider is idle for a
  // sufficient period of time.
  //
  // We setup the system such that requests for both data and screentshots hang,
  // relying on their respective timeouts to ensure that an error is returned. Additionally, we set
  // the idle timeout of the data provider to be half as long as the time it takes for a
  // request to return in order to determine that neither is interruped by the idle timeout while
  // completing.
  //
  // We test scenarios in which a single request is made, sequential requests are made, and
  // concurrent requests are made, in that order.

  // Track if requests have completed.
  bool got_data = false;
  bool got_screenshot = false;

  const zx::duration kGetScreenshotTimeout = zx::sec(10);
  const zx::duration kGetDataTimeout = zx::sec(30);

  ASSERT_GE(kGetScreenshotTimeout, kDataProviderIdleTimeout);
  ASSERT_GE(kGetDataTimeout, kDataProviderIdleTimeout);

  SetUpDataProviderOnlyRequestingChannel(kDataProviderIdleTimeout);

  SetUpScenic(std::make_unique<StubScenicNeverReturns>());
  SetUpChannelProvider(std::make_unique<StubChannelProviderNeverReturns>());

  // In the following tests we list the current time of a stopwatch that starts at 0 seconds and the
  // point in time at which the idle timeout function is expected to run. In the circumstance the
  // idle timeout function is blocked from running we denote the timeout as X.

  // Make a single request for a screenshot to check that the idle timeout happens after the
  // screenshot has been returned.

  // TIME = 0; TIMEOUT @ X (unset)
  data_provider_->GetScreenshot(
      ImageEncoding::PNG,
      [&got_screenshot](std::unique_ptr<Screenshot> _) { got_screenshot = true; });
  RunLoopFor(kGetScreenshotTimeout);

  // TIME = 10; TIMEOUT @ 15 (10 + 5, current time + kDataProviderIdleTimeout)
  ASSERT_TRUE(got_screenshot);
  ASSERT_FALSE(data_provider_timed_out_);

  RunLoopFor(kDataProviderIdleTimeout);

  // TIME = 15; TIMEOUT @ 15 (unchanged)
  ASSERT_TRUE(data_provider_timed_out_);

  // Make a single request for data to check that the idle timeout happens after the data has been
  // returned.

  // TIME = 15; TIMEOUT @ X (reset)
  data_provider_timed_out_ = false;
  data_provider_->GetData([&got_data](fit::result<Data, zx_status_t> _) { got_data = true; });
  RunLoopFor(kGetDataTimeout);

  // TIME = 25; TIMEOUT @ 30 (25 + 5, current time + kDataProviderIdleTimeout)
  ASSERT_TRUE(got_data);
  ASSERT_FALSE(data_provider_timed_out_);

  RunLoopFor(kDataProviderIdleTimeout);

  // TIME = 30; TIMEOUT @ 30 (unchanged)
  ASSERT_TRUE(data_provider_timed_out_);

  got_screenshot = false;
  got_data = false;
  data_provider_timed_out_ = false;

  // Check that sequential requests for a screenshot and data properly block the idle timeout
  // function and that it executes when expected.

  // TIME = 30; TIMEOUT @ X (reset)
  data_provider_->GetScreenshot(
      ImageEncoding::PNG,
      [&got_screenshot](std::unique_ptr<Screenshot> _) { got_screenshot = true; });
  RunLoopFor(kGetScreenshotTimeout);

  // TIME = 40; TIMEOUT @ 45 (40 + 5, current time + kDataProviderIdleTimeout)
  ASSERT_TRUE(got_screenshot);
  ASSERT_FALSE(data_provider_timed_out_);

  data_provider_->GetData([&got_data](fit::result<Data, zx_status_t> _) { got_data = true; });
  RunLoopFor(kGetDataTimeout);

  // TIME = 50; TIMEOUT @ 55 (50 + 5, current time + kDataProviderIdleTimeout)

  ASSERT_TRUE(got_data);
  ASSERT_FALSE(data_provider_timed_out_);

  RunLoopFor(kDataProviderIdleTimeout);

  // TIME = 55; TIMEOUT @ 55 (unchanged)
  ASSERT_TRUE(data_provider_timed_out_);

  got_screenshot = false;
  got_data = false;
  data_provider_timed_out_ = false;

  // Check that concurrent requests for a screenshot and data properly block the idle timeout
  // function and that it executes when expected.

  // TIME = 55; TIMEOUT @ X (reset)
  data_provider_->GetScreenshot(
      ImageEncoding::PNG,
      [&got_screenshot](std::unique_ptr<Screenshot> _) { got_screenshot = true; });
  RunLoopFor(kDataProviderIdleTimeout);

  // TIME = 60; TIMEOUT @ X (reset)
  data_provider_->GetData([&got_data](fit::result<Data, zx_status_t> _) { got_data = true; });
  RunLoopFor(kDataProviderIdleTimeout);

  // TIME = 65; TIMEOUT @ X (reset)
  ASSERT_TRUE(got_screenshot);
  ASSERT_FALSE(got_data);
  ASSERT_FALSE(data_provider_timed_out_);
  RunLoopFor(kGetDataTimeout - kDataProviderIdleTimeout);

  // TIME = 90; TIMEOUT @ 95 (90 + 5, current time + kDataProviderIdleTimeout)

  ASSERT_TRUE(got_data);
  ASSERT_FALSE(data_provider_timed_out_);
  RunLoopFor(kDataProviderIdleTimeout);

  // TIME = 95; TIMEOUT @ 95 (unchanged)
  EXPECT_TRUE(data_provider_timed_out_);
}

// Unit-tests the implementation of the fuchsia.feedback.DataProvider FIDL interface when we need
// to control the test environment, e.g. to inject additional components.
//
// This does not test the environment service. It directly instantiates the class, without
// connecting through FIDL.
class DataProviderTestWithEnv : public sys::testing::TestWithEnvironment {
 public:
  void SetUp() override {
    SetUpDataProvider(Config{kDefaultAnnotations,
                             {
                                 kAttachmentBuildSnapshot,
                                 kAttachmentLogKernel,
                                 kAttachmentInspect,
                                 kAttachmentLogSystem,
                             }});
  }

  void TearDown() override {
    if (inspect_test_app_controller_) {
      TerminateInspectTestApp();
    }
  }

 protected:
  void SetUpDataProvider(const Config& config) {
    data_provider_.reset(new DataProvider(
        dispatcher(), service_directory_provider_.service_directory(), config, []() {},
        zx::duration::infinite()));
  }

  // Injects a test app that exposes some Inspect data in the test environment.
  //
  // Useful to guarantee there is a component within the environment that exposes Inspect data as
  // we are excluding system_objects paths from the Inspect discovery and the test component itself
  // only has a system_objects Inspect node.
  void InjectInspectTestApp() {
    fuchsia::sys::LaunchInfo launch_info;
    launch_info.url = "fuchsia-pkg://fuchsia.com/feedback_agent_tests#meta/inspect_test_app.cmx";
    environment_ = CreateNewEnclosingEnvironment("inspect_test_app_environment", CreateServices());
    environment_->CreateComponent(std::move(launch_info),
                                  inspect_test_app_controller_.NewRequest());
    bool ready = false;
    inspect_test_app_controller_.events().OnDirectoryReady = [&ready] { ready = true; };
    RunLoopUntil([&ready] { return ready; });
  }

  fit::result<Data, zx_status_t> GetData() {
    fit::result<Data, zx_status_t> out_result;
    bool has_out_result = false;
    data_provider_->GetData([&out_result, &has_out_result](fit::result<Data, zx_status_t> result) {
      out_result = std::move(result);
      has_out_result = true;
    });
    RunLoopUntil([&has_out_result] { return has_out_result; });
    return out_result;
  }

  void UnpackAttachmentBundle(const Data& data, std::vector<Attachment>* unpacked_attachments) {
    ASSERT_TRUE(data.has_attachment_bundle());
    const auto& attachment_bundle = data.attachment_bundle();
    EXPECT_STREQ(attachment_bundle.key.c_str(), kAttachmentBundle);
    ASSERT_TRUE(Unpack(attachment_bundle.value, unpacked_attachments));
    EXPECT_EQ(unpacked_attachments->size(), data.attachments().size());
  }

 private:
  void TerminateInspectTestApp() {
    inspect_test_app_controller_->Kill();
    bool is_inspect_test_app_terminated = false;
    inspect_test_app_controller_.events().OnTerminated =
        [&is_inspect_test_app_terminated](int64_t code, fuchsia::sys::TerminationReason reason) {
          FXL_CHECK(reason == fuchsia::sys::TerminationReason::EXITED);
          is_inspect_test_app_terminated = true;
        };
    RunLoopUntil([&is_inspect_test_app_terminated] { return is_inspect_test_app_terminated; });
  }

 protected:
  std::unique_ptr<DataProvider> data_provider_;

 private:
  sys::testing::ServiceDirectoryProvider service_directory_provider_;
  std::unique_ptr<sys::testing::EnclosingEnvironment> environment_;
  fuchsia::sys::ComponentControllerPtr inspect_test_app_controller_;
};

TEST_F(DataProviderTestWithEnv, GetData_Inspect) {
  InjectInspectTestApp();

  fit::result<Data, zx_status_t> result = GetData();

  ASSERT_TRUE(result.is_ok());

  const Data& data = result.value();

  // There should be an "inspect.json" attachment.
  ASSERT_TRUE(data.has_attachments());
  bool found_inspect_attachment = false;
  std::string inspect_json;
  for (const auto& attachment : data.attachments()) {
    if (attachment.key != kAttachmentInspect) {
      continue;
    }
    found_inspect_attachment = true;

    ASSERT_TRUE(fsl::StringFromVmo(attachment.value, &inspect_json));
    ASSERT_FALSE(inspect_json.empty());
  }
  EXPECT_TRUE(found_inspect_attachment);

  // That same "inspect.json" attachment should be present in the attachment bundle.
  std::vector<Attachment> unpacked_attachments;
  UnpackAttachmentBundle(data, &unpacked_attachments);
  EXPECT_THAT(unpacked_attachments,
              testing::Contains(MatchesAttachment(kAttachmentInspect, inspect_json)));
}

}  // namespace
}  // namespace feedback

int main(int argc, char** argv) {
  if (!fxl::SetTestSettings(argc, argv)) {
    return EXIT_FAILURE;
  }

  testing::InitGoogleTest(&argc, argv);
  syslog::InitLogger({"feedback", "test"});
  return RUN_ALL_TESTS();
}
