// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/sys/cpp/service_directory.h>
#include <zircon/errors.h>

#include <memory>

#include "garnet/public/lib/fostr/fidl/fuchsia/feedback/formatting.h"
#include "third_party/googletest/googlemock/include/gmock/gmock.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace fuchsia {
namespace feedback {
namespace {

// Returns true if gMock |arg|.key matches |expected_key|.
MATCHER_P(MatchesKey, expected_key,
          "matches an element with key '" + std::string(expected_key) + "'") {
  return arg.key == expected_key;
}

// Smoke-tests the real environment service for the fuchsia.feedback.DataProvider FIDL interface,
// connecting through FIDL.
class FeedbackAgentIntegrationTest : public testing::Test {
 public:
  void SetUp() override { environment_services_ = ::sys::ServiceDirectory::CreateFromNamespace(); }

 protected:
  std::shared_ptr<::sys::ServiceDirectory> environment_services_;
};

TEST_F(FeedbackAgentIntegrationTest, InvalidOverrideConfig_SmokeTest) {
  DataProviderSyncPtr data_provider;
  environment_services_->Connect(data_provider.NewRequest());

  DataProvider_GetData_Result out_result;
  ASSERT_EQ(data_provider->GetData(&out_result), ZX_OK);

  ASSERT_TRUE(out_result.is_response());

  // We cannot expect a particular value for each annotation or attachment because values might
  // depend on which device the test runs (e.g., board name) or what happened prior to running this
  // test (e.g., logs). But we should expect the keys to be present.
  //
  // Given that we don't inject an Inspect app nor a fake logger nor a fake channel provider, these
  // keys won't appear in the result either.
  ASSERT_TRUE(out_result.response().data.has_annotations());
  EXPECT_THAT(out_result.response().data.annotations(), testing::UnorderedElementsAreArray({
                                                            MatchesKey("device.board-name"),
                                                            MatchesKey("build.latest-commit-date"),
                                                            MatchesKey("build.version"),
                                                            MatchesKey("build.board"),
                                                            MatchesKey("build.product"),
                                                        }));
  ASSERT_TRUE(out_result.response().data.has_attachments());
  EXPECT_THAT(out_result.response().data.attachments(), testing::UnorderedElementsAreArray({
                                                            MatchesKey("build.snapshot.xml"),
                                                            MatchesKey("log.kernel.txt"),
                                                        }));
}

}  // namespace
}  // namespace feedback
}  // namespace fuchsia
