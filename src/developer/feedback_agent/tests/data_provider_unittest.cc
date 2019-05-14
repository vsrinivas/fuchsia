// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback_agent/data_provider.h"

#include <fuchsia/feedback/cpp/fidl.h>
#include <fuchsia/math/cpp/fidl.h>
#include <lib/fostr/fidl/fuchsia/math/formatting.h>
#include <lib/fostr/fidl/fuchsia/mem/formatting.h>
#include <lib/fostr/indent.h>
#include <lib/fsl/vmo/file.h>
#include <lib/fsl/vmo/sized_vmo.h>
#include <lib/fsl/vmo/strings.h>
#include <lib/fsl/vmo/vector.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/sys/cpp/testing/service_directory_provider.h>
#include <lib/syslog/cpp/logger.h>
#include <lib/syslog/logger.h>
#include <lib/zx/time.h>
#include <zircon/errors.h>

#include <memory>
#include <ostream>
#include <vector>

#include "src/developer/feedback_agent/config.h"
#include "src/developer/feedback_agent/tests/stub_logger.h"
#include "src/developer/feedback_agent/tests/stub_scenic.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "third_party/googletest/googlemock/include/gmock/gmock.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace fuchsia {
namespace feedback {
namespace {

const Config kDefaultConfig = Config{/*attachment_whitelist=*/{
    "build.snapshot",
    "log.kernel",
    "log.system",
}};

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

  // This should be kept in sync with DoGetScreenshotResponseMatch() as we only
  // want to display what we actually compare, for now the presence of a
  // screenshot and its dimensions if present.
  operator std::string() const {
    if (!screenshot) {
      return "no screenshot";
    }
    const fuchsia::math::Size& dimensions_in_px = screenshot->dimensions_in_px;
    return fxl::StringPrintf("a %d x %d screenshot", dimensions_in_px.width,
                             dimensions_in_px.height);
  }

  // This is used by gTest to pretty-prints failed expectations instead of the
  // default byte string.
  friend std::ostream& operator<<(std::ostream& os,
                                  const GetScreenshotResponse& response) {
    return os << std::string(response);
  }
};

// Compares two GetScreenshotResponse.
//
// This should be kept in sync with std::string() as we only want to display
// what we actually compare, for now the presence of a screenshot and its
// dimensions.
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

  if (!fidl::Equals(actual.screenshot->dimensions_in_px,
                    expected.screenshot->dimensions_in_px)) {
    *result_listener << "Expected screenshot dimensions "
                     << expected.screenshot->dimensions_in_px << ", got "
                     << actual.screenshot->dimensions_in_px;
    return false;
  }

  // We do not compare the VMOs.

  return true;
}

// Returns true if gMock |arg| matches |expected|, assuming two
// GetScreenshotResponse.
MATCHER_P(MatchesGetScreenshotResponse, expected,
          "matches " + std::string(expected.get())) {
  return DoGetScreenshotResponseMatch(arg, expected, result_listener);
}

// Compares two Attachment.
template <typename ResultListenerT>
bool DoAttachmentMatch(const Attachment& actual,
                       const std::string& expected_key,
                       const std::string& expected_value,
                       ResultListenerT* result_listener) {
  if (actual.key != expected_key) {
    *result_listener << "Expected key " << expected_key << ", got "
                     << actual.key;
    return false;
  }

  std::string actual_value;
  if (!fsl::StringFromVmo(actual.value, &actual_value)) {
    *result_listener << "Cannot parse actual VMO for key " << actual.key
                     << " to string";
    return false;
  }

  if (actual_value.compare(expected_value) != 0) {
    *result_listener << "Expected value " << expected_value << ", got "
                     << actual_value;
    return false;
  }

  return true;
}

// Returns true if gMock |arg|.key matches |expected_key| and str(|arg|.value)
// matches |expected_value|, assuming two Attachment.
MATCHER_P2(MatchesAttachment, expected_key, expected_value,
           "matches an attachment with key '" + std::string(expected_key) +
               "' and value '" + std::string(expected_value) + "'") {
  return DoAttachmentMatch(arg, expected_key, expected_value, result_listener);
}

// Unit-tests the implementation of the fuchsia.feedback.DataProvider FIDL
// interface.
//
// This does not test the environment service. It directly instantiates the
// class, without connecting through FIDL.
class DataProviderImplTest : public gtest::RealLoopFixture {
 public:
  void SetUp() override {
    stub_scenic_.reset(new StubScenic());
    FXL_CHECK(service_directory_provider_.AddService(
                  stub_scenic_->GetHandler()) == ZX_OK);
    stub_logger_.reset(new StubLogger());
    FXL_CHECK(service_directory_provider_.AddService(
                  stub_logger_->GetHandler(dispatcher())) == ZX_OK);

    ResetDataProvider(kDefaultConfig);
  }

 protected:
  // Resets the underlying |data_provider_| using the given |config|.
  void ResetDataProvider(const Config& config) {
    data_provider_.reset(new DataProviderImpl(
        dispatcher(), service_directory_provider_.service_directory(), config));
  }

  GetScreenshotResponse GetScreenshot() {
    GetScreenshotResponse out_response;
    bool has_out_response = false;
    data_provider_->GetScreenshot(
        ImageEncoding::PNG, [&out_response, &has_out_response](
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
    data_provider_->GetData(
        [&out_result, &has_out_result](DataProvider_GetData_Result result) {
          out_result = std::move(result);
          has_out_result = true;
        });
    RunLoopUntil([&has_out_result] { return has_out_result; });
    return out_result;
  }

  void set_scenic_responses(std::vector<TakeScreenshotResponse> responses) {
    stub_scenic_->set_take_screenshot_responses(std::move(responses));
  }
  const std::vector<TakeScreenshotResponse>& get_scenic_responses() const {
    return stub_scenic_->take_screenshot_responses();
  }

  void set_logger_messages(
      const std::vector<fuchsia::logger::LogMessage>& messages) {
    stub_logger_->set_messages(messages);
  }

  std::unique_ptr<DataProviderImpl> data_provider_;

 private:
  ::sys::testing::ServiceDirectoryProvider service_directory_provider_;

  std::unique_ptr<StubScenic> stub_scenic_;
  std::unique_ptr<StubLogger> stub_logger_;
};

TEST_F(DataProviderImplTest, GetScreenshot_SucceedOnScenicReturningSuccess) {
  const size_t image_dim_in_px = 100;
  std::vector<TakeScreenshotResponse> scenic_responses;
  scenic_responses.emplace_back(CreateCheckerboardScreenshot(image_dim_in_px),
                                kSuccess);
  set_scenic_responses(std::move(scenic_responses));

  GetScreenshotResponse feedback_response = GetScreenshot();

  EXPECT_TRUE(get_scenic_responses().empty());

  ASSERT_NE(feedback_response.screenshot, nullptr);
  EXPECT_EQ((size_t)feedback_response.screenshot->dimensions_in_px.height,
            image_dim_in_px);
  EXPECT_EQ((size_t)feedback_response.screenshot->dimensions_in_px.width,
            image_dim_in_px);
  EXPECT_TRUE(feedback_response.screenshot->image.vmo.is_valid());

  fsl::SizedVmo expected_sized_vmo;
  ASSERT_TRUE(fsl::VmoFromFilename("/pkg/data/checkerboard_100.png",
                                   &expected_sized_vmo));
  std::vector<uint8_t> expected_pixels;
  ASSERT_TRUE(fsl::VectorFromVmo(expected_sized_vmo, &expected_pixels));
  std::vector<uint8_t> actual_pixels;
  ASSERT_TRUE(
      fsl::VectorFromVmo(feedback_response.screenshot->image, &actual_pixels));
  EXPECT_EQ(actual_pixels, expected_pixels);
}

TEST_F(DataProviderImplTest, GetScreenshot_FailOnScenicReturningFailure) {
  std::vector<TakeScreenshotResponse> scenic_responses;
  scenic_responses.emplace_back(CreateEmptyScreenshot(), kFailure);
  set_scenic_responses(std::move(scenic_responses));

  GetScreenshotResponse feedback_response = GetScreenshot();

  EXPECT_TRUE(get_scenic_responses().empty());

  EXPECT_EQ(feedback_response.screenshot, nullptr);
}

TEST_F(DataProviderImplTest,
       GetScreenshot_FailOnScenicReturningNonBGRA8Screenshot) {
  std::vector<TakeScreenshotResponse> scenic_responses;
  scenic_responses.emplace_back(CreateNonBGRA8Screenshot(), kSuccess);
  set_scenic_responses(std::move(scenic_responses));

  GetScreenshotResponse feedback_response = GetScreenshot();

  EXPECT_TRUE(get_scenic_responses().empty());

  EXPECT_EQ(feedback_response.screenshot, nullptr);
}

TEST_F(DataProviderImplTest, GetScreenshot_ParallelRequests) {
  // We simulate three calls to DataProviderImpl::GetScreenshot(): one for which
  // the stub Scenic will return a checkerboard 10x10, one for a 20x20 and one
  // failure.
  const size_t num_calls = 3u;
  const size_t image_dim_in_px_0 = 10u;
  const size_t image_dim_in_px_1 = 20u;
  std::vector<TakeScreenshotResponse> scenic_responses;
  scenic_responses.emplace_back(CreateCheckerboardScreenshot(image_dim_in_px_0),
                                kSuccess);
  scenic_responses.emplace_back(CreateCheckerboardScreenshot(image_dim_in_px_1),
                                kSuccess);
  scenic_responses.emplace_back(CreateEmptyScreenshot(), kFailure);
  ASSERT_EQ(scenic_responses.size(), num_calls);
  set_scenic_responses(std::move(scenic_responses));

  std::vector<GetScreenshotResponse> feedback_responses;
  for (size_t i = 0; i < num_calls; i++) {
    data_provider_->GetScreenshot(
        ImageEncoding::PNG,
        [&feedback_responses](std::unique_ptr<Screenshot> screenshot) {
          feedback_responses.push_back({std::move(screenshot)});
        });
  }
  RunLoopUntil([&feedback_responses, num_calls] {
    return feedback_responses.size() == num_calls;
  });

  EXPECT_TRUE(get_scenic_responses().empty());

  // We cannot assume that the order of the DataProviderImpl::GetScreenshot()
  // calls match the order of the Scenic::TakeScreenshot() callbacks because of
  // the async message loop. Thus we need to match them as sets.
  //
  // We set the expectations in advance and then pass a reference to the gMock
  // matcher using testing::ByRef() because the underlying VMO is not copyable.
  const GetScreenshotResponse expected_0 = {
      MakeUniqueScreenshot(image_dim_in_px_0)};
  const GetScreenshotResponse expected_1 = {
      MakeUniqueScreenshot(image_dim_in_px_1)};
  const GetScreenshotResponse expected_2 = {nullptr};
  EXPECT_THAT(feedback_responses,
              testing::UnorderedElementsAreArray({
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

TEST_F(DataProviderImplTest, GetData_SmokeTest) {
  // CollectSystemLogs() has its own set of unit tests so we only cover one log
  // message here to check that we are attaching the logs.
  set_logger_messages({
      BuildLogMessage(FX_LOG_INFO, "log message",
                      /*timestamp_offset=*/zx::duration(0), {"foo"}),
  });

  DataProvider_GetData_Result result = GetData();

  ASSERT_TRUE(result.is_response());
  // As we control the system log attachment, we can expect it to be present and
  // with a particular value.
  ASSERT_TRUE(result.response().data.has_attachments());
  EXPECT_THAT(
      result.response().data.attachments(),
      testing::Contains(MatchesAttachment(
          "log.system", "[15604.000][07559][07687][foo] INFO: log message\n")));
  // There is nothing else we can assert here as no missing annotation nor
  // attachment is fatal.
}

TEST_F(DataProviderImplTest, GetData_EmptyAttachmentWhitelist) {
  ResetDataProvider(Config{/*attachment_whitelist=*/{}});

  DataProvider_GetData_Result result = GetData();
  ASSERT_TRUE(result.is_response());
  EXPECT_FALSE(result.response().data.has_attachments());
}

TEST_F(DataProviderImplTest, GetData_UnknownWhitelistedAttachment) {
  ResetDataProvider(Config{/*attachment_whitelist=*/{"unknown.attachment"}});

  DataProvider_GetData_Result result = GetData();
  ASSERT_TRUE(result.is_response());
  EXPECT_FALSE(result.response().data.has_attachments());
}

}  // namespace

// Pretty-prints Attachment in gTest matchers instead of the default byte string
// in case of failed expectations.
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
  testing::InitGoogleTest(&argc, argv);
  syslog::InitLogger({"feedback_agent", "test"});
  return RUN_ALL_TESTS();
}
