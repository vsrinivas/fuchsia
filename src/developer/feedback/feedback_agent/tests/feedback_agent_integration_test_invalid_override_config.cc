// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/fit/result.h>
#include <lib/sys/cpp/service_directory.h>
#include <zircon/errors.h>

#include <memory>

#include "garnet/public/lib/fostr/fidl/fuchsia/feedback/formatting.h"
#include "src/developer/feedback/feedback_agent/constants.h"
#include "src/developer/feedback/testing/gmatchers.h"
#include "src/developer/feedback/utils/archive.h"
#include "third_party/googletest/googlemock/include/gmock/gmock.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace feedback {
namespace {

using fuchsia::feedback::Attachment;
using fuchsia::feedback::Data;
using fuchsia::feedback::DataProviderSyncPtr;

// Smoke-tests the real environment service for the fuchsia.feedback.DataProvider FIDL interface,
// connecting through FIDL.
class FeedbackAgentIntegrationTest : public testing::Test {
 public:
  void SetUp() override { environment_services_ = sys::ServiceDirectory::CreateFromNamespace(); }

 protected:
  std::shared_ptr<sys::ServiceDirectory> environment_services_;
};

TEST_F(FeedbackAgentIntegrationTest, InvalidOverrideConfig_SmokeTest) {
  DataProviderSyncPtr data_provider;
  environment_services_->Connect(data_provider.NewRequest());

  fuchsia::feedback::DataProvider_GetData_Result out_result;
  ASSERT_EQ(data_provider->GetData(&out_result), ZX_OK);

  fit::result<Data, zx_status_t> result = std::move(out_result);
  ASSERT_TRUE(result.is_ok());

  const Data& data = result.value();

  // We cannot expect a particular value for each annotation or attachment because values might
  // depend on which device the test runs (e.g., board name) or what happened prior to running this
  // test (e.g., logs). But we should expect the keys to be present.
  //
  // Given that we don't inject an Inspect app nor a fake logger nor a fake channel provider, these
  // keys won't appear in the result either.
  ASSERT_TRUE(data.has_annotations());
  EXPECT_THAT(data.annotations(), testing::UnorderedElementsAreArray({
                                      MatchesKey(kAnnotationBuildBoard),
                                      MatchesKey(kAnnotationBuildLatestCommitDate),
                                      MatchesKey(kAnnotationBuildProduct),
                                      MatchesKey(kAnnotationBuildIsDebug),
                                      MatchesKey(kAnnotationBuildVersion),
                                      MatchesKey(kAnnotationDeviceBoardName),
                                      MatchesKey(kAnnotationDeviceUptime),
                                      MatchesKey(kAnnotationDeviceUTCTime),
                                      MatchesKey(kAnnotationHardwareProductSKU),
                                      MatchesKey(kAnnotationHardwareProductLanguage),
                                      MatchesKey(kAnnotationHardwareProductRegulatoryDomain),
                                      MatchesKey(kAnnotationHardwareProductLocaleList),
                                      MatchesKey(kAnnotationHardwareProductName),
                                      MatchesKey(kAnnotationHardwareProductModel),
                                      MatchesKey(kAnnotationHardwareProductManufacturer),
                                  }));

  ASSERT_TRUE(data.has_attachments());
  EXPECT_THAT(data.attachments(), testing::UnorderedElementsAreArray({
                                      MatchesKey(kAttachmentAnnotations),
                                      MatchesKey(kAttachmentBuildSnapshot),
                                      MatchesKey(kAttachmentInspect),
                                      MatchesKey(kAttachmentLogKernel),
                                  }));

  ASSERT_TRUE(data.has_attachment_bundle());
  const auto& attachment_bundle = data.attachment_bundle();
  EXPECT_STREQ(attachment_bundle.key.c_str(), kAttachmentBundle);
  std::vector<Attachment> unpacked_attachments;
  ASSERT_TRUE(Unpack(attachment_bundle.value, &unpacked_attachments));
  EXPECT_THAT(unpacked_attachments, testing::UnorderedElementsAreArray({
                                        MatchesKey(kAttachmentAnnotations),
                                        MatchesKey(kAttachmentBuildSnapshot),
                                        MatchesKey(kAttachmentInspect),
                                        MatchesKey(kAttachmentLogKernel),
                                    }));
}

}  // namespace
}  // namespace feedback
