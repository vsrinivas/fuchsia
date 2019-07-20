// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/feedback/feedback_agent/data_provider.h"

#include <fuchsia/feedback/cpp/fidl.h>
#include <fuchsia/math/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/fostr/fidl/fuchsia/math/formatting.h>
#include <lib/fostr/fidl/fuchsia/mem/formatting.h>
#include <lib/fostr/indent.h>
#include <lib/fsl/vmo/file.h>
#include <lib/fsl/vmo/sized_vmo.h>
#include <lib/fsl/vmo/strings.h>
#include <lib/fsl/vmo/vector.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/sys/cpp/testing/service_directory_provider.h>
#include <lib/sys/cpp/testing/test_with_environment.h>
#include <lib/syslog/cpp/logger.h>
#include <lib/syslog/logger.h>
#include <lib/zx/time.h>
#include <zircon/errors.h>

#include <memory>
#include <ostream>
#include <set>
#include <string>
#include <vector>

#include "src/developer/feedback/feedback_agent/config.h"
#include "src/developer/feedback/feedback_agent/tests/stub_channel_provider.h"
#include "src/developer/feedback/feedback_agent/tests/stub_logger.h"
#include "src/developer/feedback/feedback_agent/tests/stub_scenic.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/lib/fxl/test/test_settings.h"
#include "third_party/googletest/googlemock/include/gmock/gmock.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"
#include "third_party/rapidjson/include/rapidjson/document.h"
#include "third_party/rapidjson/include/rapidjson/schema.h"

namespace fuchsia {
namespace feedback {
namespace {

const std::set<std::string> kDefaultAnnotations = {
    "build.board", "build.latest-commit-date", "build.product", "build.version",
    "channel",     "device.board-name",
};
const std::set<std::string> kDefaultAttachments = {
    "build.snapshot.xml",
    "inspect.json",
    "log.kernel.txt",
    "log.system.txt",
};
const Config kDefaultConfig = Config{kDefaultAnnotations, kDefaultAttachments};

constexpr bool kSuccess = true;
constexpr bool kFailure = false;

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

// Compares two Attachment objects.
template <typename ResultListenerT>
bool DoAttachmentMatch(const Attachment& actual, const std::string& expected_key,
                       const std::string& expected_value, ResultListenerT* result_listener) {
  if (actual.key != expected_key) {
    *result_listener << "Expected key " << expected_key << ", got " << actual.key;
    return false;
  }

  std::string actual_value;
  if (!fsl::StringFromVmo(actual.value, &actual_value)) {
    *result_listener << "Cannot parse actual VMO for key " << actual.key << " to string";
    return false;
  }

  if (actual_value.compare(expected_value) != 0) {
    *result_listener << "Expected value " << expected_value << ", got " << actual_value;
    return false;
  }

  return true;
}

// Returns true if gMock |arg|.key matches |expected_key| and str(|arg|.value) matches
// |expected_value|, assuming two Attachment objects.
MATCHER_P2(MatchesAttachment, expected_key, expected_value,
           "matches an attachment with key '" + std::string(expected_key) + "' and value '" +
               std::string(expected_value) + "'") {
  return DoAttachmentMatch(arg, expected_key, expected_value, result_listener);
}

// Compares two Annotation objects.
template <typename ResultListenerT>
bool DoAnnotationMatch(const Annotation& actual, const std::string& expected_key,
                       const std::string& expected_value, ResultListenerT* result_listener) {
  if (actual.key != expected_key) {
    *result_listener << "Expected key " << expected_key << ", got " << actual.key;
    return false;
  }

  if (actual.value.compare(expected_value) != 0) {
    *result_listener << "Expected value " << expected_value << ", got " << actual.value;
    return false;
  }

  return true;
}

// Returns true if gMock |arg|.key matches |expected_key| and str(|arg|.value) matches
// |expected_value|, assuming two Annotation objects.
MATCHER_P2(MatchesAnnotation, expected_key, expected_value,
           "matches an annotation with key '" + std::string(expected_key) + "' and value '" +
               std::string(expected_value) + "'") {
  return DoAnnotationMatch(arg, expected_key, expected_value, result_listener);
}

// Unit-tests the implementation of the fuchsia.feedback.DataProvider FIDL interface.
//
// This does not test the environment service. It directly instantiates the class, without
// connecting through FIDL.
class DataProviderImplTest : public ::sys::testing::TestWithEnvironment {
 public:
  void SetUp() override { ResetDataProvider(kDefaultConfig); }

  void TearDown() override {
    if (!controller_) {
      return;
    }
    controller_->Kill();
    bool done = false;
    controller_.events().OnTerminated = [&done](int64_t code,
                                                fuchsia::sys::TerminationReason reason) {
      FXL_CHECK(reason == fuchsia::sys::TerminationReason::EXITED);
      done = true;
    };
    RunLoopUntil([&done] { return done; });
  }

 protected:
  // Resets the underlying |data_provider_| using the given |config|.
  void ResetDataProvider(const Config& config) {
    data_provider_.reset(new DataProviderImpl(
        dispatcher(), service_directory_provider_.service_directory(), config));
  }

  // Resets the underlying |stub_scenic_| using the given |scenic|.
  void ResetScenic(std::unique_ptr<StubScenic> stub_scenic) {
    stub_scenic_ = std::move(stub_scenic);
    if (stub_scenic_) {
      FXL_CHECK(service_directory_provider_.AddService(stub_scenic_->GetHandler()) == ZX_OK);
    }
  }

  // Resets the underlying |stub_logger_| with the given log |messages|.
  void ResetLogger(const std::vector<fuchsia::logger::LogMessage>& messages) {
    stub_logger_.reset(new StubLogger());
    stub_logger_->set_messages(messages);
    FXL_CHECK(service_directory_provider_.AddService(stub_logger_->GetHandler()) == ZX_OK);
  }

  // Resets the underlying |stub_channel_provider| with the given |channel|.
  void ResetChannelProvider(std::string channel) {
    stub_channel_provider_.reset(new StubUpdateInfo());
    stub_channel_provider_->set_channel(channel);
    FXL_CHECK(service_directory_provider_.AddService(stub_channel_provider_->GetHandler()) ==
              ZX_OK);
  }

  // Injects a test app that exposes some Inspect data in the test environment.
  //
  // Useful to guarantee there is a component within the environment that exposes Inspect data as we
  // are excluding system_objects paths from the Inspect discovery and the test component itself
  // only has a system_objects Inspect node.
  void InjectInspectTestApp() {
    fuchsia::sys::LaunchInfo launch_info;
    launch_info.url =
        "fuchsia-pkg://fuchsia.com/feedback_agent_tests#meta/"
        "inspect_test_app.cmx";
    environment_ = CreateNewEnclosingEnvironment("inspect_test_app_environment", CreateServices());
    environment_->CreateComponent(std::move(launch_info), controller_.NewRequest());
    bool ready = false;
    controller_.events().OnDirectoryReady = [&ready] { ready = true; };
    RunLoopUntil([&ready] { return ready; });
  }

  GetScreenshotResponse GetScreenshot() {
    GetScreenshotResponse out_response;
    bool has_out_response = false;
    data_provider_->GetScreenshot(ImageEncoding::PNG, [&out_response, &has_out_response](
                                                          std::unique_ptr<Screenshot> screenshot) {
      out_response.screenshot = std::move(screenshot);
      has_out_response = true;
    });
    RunLoopUntil([&has_out_response] { return has_out_response; });
    return out_response;
  }

  DataProvider_GetData_Result GetData() {
    DataProvider_GetData_Result out_result;
    bool has_out_result = false;
    data_provider_->GetData([&out_result, &has_out_result](DataProvider_GetData_Result result) {
      out_result = std::move(result);
      has_out_result = true;
    });
    RunLoopUntil([&has_out_result] { return has_out_result; });
    return out_result;
  }

  uint64_t total_num_scenic_bindings() { return stub_scenic_->total_num_bindings(); }
  size_t current_num_scenic_bindings() { return stub_scenic_->current_num_bindings(); }
  const std::vector<TakeScreenshotResponse>& get_scenic_responses() const {
    return stub_scenic_->take_screenshot_responses();
  }

  std::unique_ptr<DataProviderImpl> data_provider_;

 private:
  ::sys::testing::ServiceDirectoryProvider service_directory_provider_;
  std::unique_ptr<::sys::testing::EnclosingEnvironment> environment_;
  fuchsia::sys::ComponentControllerPtr controller_;

  std::unique_ptr<StubScenic> stub_scenic_;
  std::unique_ptr<StubLogger> stub_logger_;
  std::unique_ptr<StubUpdateInfo> stub_channel_provider_;
};

TEST_F(DataProviderImplTest, GetScreenshot_SucceedOnScenicReturningSuccess) {
  const size_t image_dim_in_px = 100;
  std::vector<TakeScreenshotResponse> scenic_responses;
  scenic_responses.emplace_back(CreateCheckerboardScreenshot(image_dim_in_px), kSuccess);
  std::unique_ptr<StubScenic> stub_scenic = std::make_unique<StubScenic>();
  stub_scenic->set_take_screenshot_responses(std::move(scenic_responses));
  ResetScenic(std::move(stub_scenic));

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

TEST_F(DataProviderImplTest, GetScreenshot_FailOnScenicNotAvailable) {
  ResetScenic(nullptr);

  GetScreenshotResponse feedback_response = GetScreenshot();

  EXPECT_EQ(feedback_response.screenshot, nullptr);
}

TEST_F(DataProviderImplTest, GetScreenshot_FailOnScenicReturningFailure) {
  std::vector<TakeScreenshotResponse> scenic_responses;
  scenic_responses.emplace_back(CreateEmptyScreenshot(), kFailure);
  std::unique_ptr<StubScenic> stub_scenic = std::make_unique<StubScenic>();
  stub_scenic->set_take_screenshot_responses(std::move(scenic_responses));
  ResetScenic(std::move(stub_scenic));

  GetScreenshotResponse feedback_response = GetScreenshot();

  EXPECT_TRUE(get_scenic_responses().empty());

  EXPECT_EQ(feedback_response.screenshot, nullptr);
}

TEST_F(DataProviderImplTest, GetScreenshot_FailOnScenicReturningNonBGRA8Screenshot) {
  std::vector<TakeScreenshotResponse> scenic_responses;
  scenic_responses.emplace_back(CreateNonBGRA8Screenshot(), kSuccess);
  std::unique_ptr<StubScenic> stub_scenic = std::make_unique<StubScenic>();
  stub_scenic->set_take_screenshot_responses(std::move(scenic_responses));
  ResetScenic(std::move(stub_scenic));

  GetScreenshotResponse feedback_response = GetScreenshot();

  EXPECT_TRUE(get_scenic_responses().empty());

  EXPECT_EQ(feedback_response.screenshot, nullptr);
}

TEST_F(DataProviderImplTest, GetScreenshot_ParallelRequests) {
  // We simulate three calls to DataProviderImpl::GetScreenshot(): one for which the stub Scenic
  // will return a checkerboard 10x10, one for a 20x20 and one failure.
  const size_t num_calls = 3u;
  const size_t image_dim_in_px_0 = 10u;
  const size_t image_dim_in_px_1 = 20u;
  std::vector<TakeScreenshotResponse> scenic_responses;
  scenic_responses.emplace_back(CreateCheckerboardScreenshot(image_dim_in_px_0), kSuccess);
  scenic_responses.emplace_back(CreateCheckerboardScreenshot(image_dim_in_px_1), kSuccess);
  scenic_responses.emplace_back(CreateEmptyScreenshot(), kFailure);
  ASSERT_EQ(scenic_responses.size(), num_calls);
  std::unique_ptr<StubScenic> stub_scenic = std::make_unique<StubScenic>();
  stub_scenic->set_take_screenshot_responses(std::move(scenic_responses));
  ResetScenic(std::move(stub_scenic));

  std::vector<GetScreenshotResponse> feedback_responses;
  for (size_t i = 0; i < num_calls; i++) {
    data_provider_->GetScreenshot(ImageEncoding::PNG,
                                  [&feedback_responses](std::unique_ptr<Screenshot> screenshot) {
                                    feedback_responses.push_back({std::move(screenshot)});
                                  });
  }
  RunLoopUntil([&feedback_responses] { return feedback_responses.size() == num_calls; });

  EXPECT_TRUE(get_scenic_responses().empty());

  // We cannot assume that the order of the DataProviderImpl::GetScreenshot() calls match the order
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

TEST_F(DataProviderImplTest, GetScreenshot_OneScenicConnectionPerGetScreenshotCall) {
  // We use a stub that always returns false as we are not interested in the responses.
  ResetScenic(std::make_unique<StubScenicAlwaysReturnsFalse>());

  const size_t num_calls = 5u;
  std::vector<GetScreenshotResponse> feedback_responses;
  for (size_t i = 0; i < num_calls; i++) {
    data_provider_->GetScreenshot(ImageEncoding::PNG,
                                  [&feedback_responses](std::unique_ptr<Screenshot> screenshot) {
                                    feedback_responses.push_back({std::move(screenshot)});
                                  });
  }
  RunLoopUntil([&feedback_responses] { return feedback_responses.size() == num_calls; });

  EXPECT_EQ(total_num_scenic_bindings(), num_calls);
  // The unbinding is asynchronous so we need to run the loop until all the outstanding connections
  // are actually close in the stub.
  RunLoopUntil([this] { return current_num_scenic_bindings() == 0u; });
}

TEST_F(DataProviderImplTest, GetData_SmokeTest) {
  DataProvider_GetData_Result result = GetData();

  ASSERT_TRUE(result.is_response());
  // There is nothing else we can assert here as no missing annotation nor attachment is fatal.
}

TEST_F(DataProviderImplTest, GetData_SysLog) {
  // CollectSystemLogs() has its own set of unit tests so we only cover one log message here to
  // check that we are attaching the logs.
  ResetLogger({
      BuildLogMessage(FX_LOG_INFO, "log message",
                      /*timestamp_offset=*/zx::duration(0), {"foo"}),
  });

  DataProvider_GetData_Result result = GetData();

  ASSERT_TRUE(result.is_response());
  ASSERT_TRUE(result.response().data.has_attachments());
  EXPECT_THAT(result.response().data.attachments(),
              testing::Contains(MatchesAttachment(
                  "log.system.txt", "[15604.000][07559][07687][foo] INFO: log message\n")));
}

constexpr char kInspectJsonSchema[] = R"({
  "type": "array",
  "items": {
    "type": "object",
    "properties": {
      "path": {
        "type": "string"
      },
      "contents": {
        "type": "object"
      }
    },
    "required": [
      "path",
      "contents"
    ],
    "additionalProperties": false
  },
  "uniqueItems": true
})";

TEST_F(DataProviderImplTest, GetData_Inspect) {
  InjectInspectTestApp();

  DataProvider_GetData_Result result = GetData();

  ASSERT_TRUE(result.is_response());
  ASSERT_TRUE(result.response().data.has_attachments());

  bool found_inspect_attachment = false;
  for (const auto& attachment : result.response().data.attachments()) {
    if (attachment.key.compare("inspect.json") != 0) {
      continue;
    }
    found_inspect_attachment = true;

    std::string inspect_str;
    ASSERT_TRUE(fsl::StringFromVmo(attachment.value, &inspect_str));
    ASSERT_FALSE(inspect_str.empty());

    // JSON verification.
    // We check that the output is a valid JSON and that it matches the schema.
    rapidjson::Document inspect_json;
    ASSERT_FALSE(inspect_json.Parse(inspect_str.c_str()).HasParseError());
    rapidjson::Document inspect_schema_json;
    ASSERT_FALSE(inspect_schema_json.Parse(kInspectJsonSchema).HasParseError());
    rapidjson::SchemaDocument schema(inspect_schema_json);
    rapidjson::SchemaValidator validator(schema);
    EXPECT_TRUE(inspect_json.Accept(validator));

    // We then check that we get the expected Inspect data for the injected test app.
    bool has_entry_for_test_app = false;
    for (const auto& obj : inspect_json.GetArray()) {
      const std::string path = obj["path"].GetString();
      if (path.find("inspect_test_app.cmx") != std::string::npos) {
        has_entry_for_test_app = true;
        const auto contents = obj["contents"].GetObject();
        ASSERT_TRUE(contents.HasMember("root"));
        const auto root = contents["root"].GetObject();
        ASSERT_TRUE(root.HasMember("obj1"));
        ASSERT_TRUE(root.HasMember("obj2"));
        const auto obj1 = root["obj1"].GetObject();
        const auto obj2 = root["obj2"].GetObject();
        ASSERT_TRUE(obj1.HasMember("version"));
        ASSERT_TRUE(obj2.HasMember("version"));
        EXPECT_STREQ(obj1["version"].GetString(), "1.0");
        EXPECT_STREQ(obj2["version"].GetString(), "1.0");
        ASSERT_TRUE(obj1.HasMember("value"));
        ASSERT_TRUE(obj2.HasMember("value"));
        EXPECT_EQ(obj1["value"].GetUint(), 100u);
        EXPECT_EQ(obj2["value"].GetUint(), 200u);
      }
    }
    EXPECT_TRUE(has_entry_for_test_app);
  }
  EXPECT_TRUE(found_inspect_attachment);
}

TEST_F(DataProviderImplTest, GetData_Channel) {
  ResetChannelProvider("my-channel");

  DataProvider_GetData_Result result = GetData();

  ASSERT_TRUE(result.is_response());
  ASSERT_TRUE(result.response().data.has_annotations());
  EXPECT_THAT(result.response().data.annotations(),
              testing::Contains(MatchesAnnotation("channel", "my-channel")));
}

TEST_F(DataProviderImplTest, GetData_EmptyAnnotationAllowlist) {
  ResetDataProvider(Config{/*annotation_allowlist=*/{}, kDefaultAttachments});

  DataProvider_GetData_Result result = GetData();
  ASSERT_TRUE(result.is_response());
  EXPECT_FALSE(result.response().data.has_annotations());
}

TEST_F(DataProviderImplTest, GetData_EmptyAttachmentAllowlist) {
  ResetDataProvider(Config{kDefaultAnnotations, /*attachment_allowlist=*/{}});

  DataProvider_GetData_Result result = GetData();
  ASSERT_TRUE(result.is_response());
  EXPECT_FALSE(result.response().data.has_attachments());
}

TEST_F(DataProviderImplTest, GetData_EmptyAllowlists) {
  ResetDataProvider(Config{/*annotation_allowlist=*/{}, /*attachment_allowlist=*/{}});

  DataProvider_GetData_Result result = GetData();
  ASSERT_TRUE(result.is_response());
  EXPECT_FALSE(result.response().data.has_annotations());
  EXPECT_FALSE(result.response().data.has_attachments());
}

TEST_F(DataProviderImplTest, GetData_UnknownAllowlistedAnnotation) {
  ResetDataProvider(Config{/*annotation_allowlist=*/{"unknown.annotation"}, kDefaultAttachments});

  DataProvider_GetData_Result result = GetData();
  ASSERT_TRUE(result.is_response());
  EXPECT_FALSE(result.response().data.has_annotations());
}

TEST_F(DataProviderImplTest, GetData_UnknownAllowlistedAttachment) {
  ResetDataProvider(Config{kDefaultAnnotations,
                           /*attachment_allowlist=*/{"unknown.attachment"}});

  DataProvider_GetData_Result result = GetData();
  ASSERT_TRUE(result.is_response());
  EXPECT_FALSE(result.response().data.has_attachments());
}

}  // namespace

// Pretty-prints Attachment in gTest matchers instead of the default byte string in case of failed
// expectations.
void PrintTo(const Attachment& attachment, std::ostream* os) {
  *os << fostr::Indent;
  *os << fostr::NewLine << "key: " << attachment.key;
  *os << fostr::NewLine << "value: ";
  std::string value;
  if (fsl::StringFromVmo(attachment.value, &value)) {
    if (value.size() < 1024) {
      *os << "'" << value << "'";
    } else {
      *os << "(string too long)" << attachment.value;
    }
  } else {
    *os << attachment.value;
  }
  *os << fostr::Outdent;
}

}  // namespace feedback
}  // namespace fuchsia

int main(int argc, char** argv) {
  if (!fxl::SetTestSettings(argc, argv)) {
    return EXIT_FAILURE;
  }

  testing::InitGoogleTest(&argc, argv);
  syslog::InitLogger({"feedback", "test"});
  return RUN_ALL_TESTS();
}
