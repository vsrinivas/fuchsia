// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_agent/datastore.h"

#include <fuchsia/hwinfo/cpp/fidl.h>
#include <fuchsia/intl/cpp/fidl.h>
#include <lib/async/cpp/executor.h>
#include <lib/fit/result.h>
#include <lib/syslog/logger.h>
#include <lib/zx/time.h>

#include <cstddef>
#include <memory>
#include <string>

#include "src/developer/feedback/feedback_agent/annotations/aliases.h"
#include "src/developer/feedback/feedback_agent/attachments/aliases.h"
#include "src/developer/feedback/feedback_agent/constants.h"
#include "src/developer/feedback/feedback_agent/device_id_provider.h"
#include "src/developer/feedback/feedback_agent/tests/stub_board.h"
#include "src/developer/feedback/feedback_agent/tests/stub_channel_provider.h"
#include "src/developer/feedback/feedback_agent/tests/stub_inspect_archive.h"
#include "src/developer/feedback/feedback_agent/tests/stub_inspect_batch_iterator.h"
#include "src/developer/feedback/feedback_agent/tests/stub_logger.h"
#include "src/developer/feedback/feedback_agent/tests/stub_product.h"
#include "src/developer/feedback/testing/cobalt_test_fixture.h"
#include "src/developer/feedback/testing/stubs/stub_cobalt_logger_factory.h"
#include "src/developer/feedback/testing/unit_test_fixture.h"
#include "src/developer/feedback/utils/cobalt.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/lib/syslog/cpp/logger.h"
#include "third_party/googletest/googlemock/include/gmock/gmock.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace feedback {
namespace {

using testing::Contains;
using testing::ElementsAreArray;
using testing::Eq;
using testing::IsEmpty;
using testing::Not;
using testing::Pair;

// Keep in sync with the internal Datastore value.
constexpr zx::duration kTimeout = zx::sec(30);

// Allowlist to use in test cases where the annotations don't matter, but where we want to avoid
// spurious logs due to empty annotation allowlist.
const AnnotationKeys kDefaultAnnotationsToAvoidSpuriousLogs = {
    kAnnotationBuildIsDebug,
};
// Allowlist to use in test cases where the attachments don't matter, but where we want to avoid
// spurious logs due to empty attachment allowlist.
const AttachmentKeys kDefaultAttachmentsToAvoidSpuriousLogs = {
    kAttachmentBuildSnapshot,
};

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
    datastore_ = std::make_unique<Datastore>(dispatcher(), services(), cobalt_.get(),
                                             annotation_allowlist, attachment_allowlist);
  }

  void SetUpBoardProvider(std::unique_ptr<StubBoard> board_provider) {
    board_provider_ = std::move(board_provider);
    if (board_provider_) {
      InjectServiceProvider(board_provider_.get());
    }
  }

  void SetUpChannelProvider(std::unique_ptr<StubChannelProvider> channel_provider) {
    channel_provider_ = std::move(channel_provider);
    if (channel_provider_) {
      InjectServiceProvider(channel_provider_.get());
    }
  }

  void SetUpInspect(const std::string& inspect_chunk) {
    inspect_archive_ = std::make_unique<StubInspectArchive>(
        std::make_unique<StubInspectBatchIterator>(std::vector<std::vector<std::string>>({
            {inspect_chunk},
            {},
        })));
    InjectServiceProvider(inspect_archive_.get());
  }

  void SetUpLogger(const std::vector<fuchsia::logger::LogMessage>& messages) {
    logger_.reset(new StubLogger());
    logger_->set_messages(messages);
    InjectServiceProvider(logger_.get());
  }

  void SetUpPreviousSystemLog(const std::string& content) {
    ASSERT_TRUE(files::WriteFile(kPreviousLogsFilePath, content.c_str(), content.size()));
  }

  void SetUpProductProvider(std::unique_ptr<StubProduct> product_provider) {
    product_provider_ = std::move(product_provider);
    if (product_provider_) {
      InjectServiceProvider(product_provider_.get());
    }
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

  bool TrySetExtraAnnotations(const Annotations& extra_annotations) {
    return datastore_->TrySetExtraAnnotations(extra_annotations);
  }
  Annotations GetStaticAnnotations() { return datastore_->GetStaticAnnotations(); }
  Attachments GetStaticAttachments() { return datastore_->GetStaticAttachments(); }

 private:
  async::Executor executor_;
  std::unique_ptr<Cobalt> cobalt_;
  std::unique_ptr<Datastore> datastore_;

  // Stubs.
  std::unique_ptr<StubBoard> board_provider_;
  std::unique_ptr<StubChannelProvider> channel_provider_;
  std::unique_ptr<StubInspectArchive> inspect_archive_;
  std::unique_ptr<StubLogger> logger_;
  std::unique_ptr<StubProduct> product_provider_;
};

TEST_F(DatastoreTest, GetAnnotationsAndAttachments_SmokeTest) {
  // We list the annotations and attachments that are likely on every build to minimize the logspam.
  SetUpDatastore(
      {
          kAnnotationBuildBoard,
          kAnnotationBuildIsDebug,
          kAnnotationBuildLatestCommitDate,
          kAnnotationBuildProduct,
          kAnnotationBuildVersion,
          kAnnotationDeviceBoardName,
          kAnnotationDeviceUptime,
          kAnnotationDeviceUTCTime,
      },
      {
          kAttachmentBuildSnapshot,
      });

  // There is not much we can assert here as no missing annotation nor attachment is fatal and we
  // cannot expect annotations or attachments to be present.
  GetStaticAnnotations();
  GetStaticAttachments();
  GetAnnotations();
  GetAttachments();
}

TEST_F(DatastoreTest, GetAnnotations_BoardInfo) {
  fuchsia::hwinfo::BoardInfo info;
  info.set_name("my-board-name");
  info.set_revision("my-revision");
  SetUpBoardProvider(std::make_unique<StubBoard>(std::move(info)));
  SetUpDatastore(
      {
          kAnnotationHardwareBoardName,
          kAnnotationHardwareBoardRevision,
      },
      kDefaultAttachmentsToAvoidSpuriousLogs);

  fit::result<Annotations> annotations = GetAnnotations();
  ASSERT_TRUE(annotations.is_ok());
  EXPECT_THAT(annotations.take_value(), ElementsAreArray({
                                            Pair(kAnnotationHardwareBoardName, "my-board-name"),
                                            Pair(kAnnotationHardwareBoardRevision, "my-revision"),
                                        }));

  EXPECT_THAT(GetStaticAnnotations(), IsEmpty());
}

TEST_F(DatastoreTest, GetAnnotations_Channel) {
  auto channel_provider = std::make_unique<StubChannelProvider>();
  channel_provider->set_channel("my-channel");
  SetUpChannelProvider(std::move(channel_provider));
  SetUpDatastore({kAnnotationChannel}, kDefaultAttachmentsToAvoidSpuriousLogs);

  fit::result<Annotations> annotations = GetAnnotations();
  ASSERT_TRUE(annotations.is_ok());
  EXPECT_THAT(annotations.take_value(), ElementsAreArray({
                                            Pair(kAnnotationChannel, "my-channel"),
                                        }));

  EXPECT_THAT(GetStaticAnnotations(), IsEmpty());
}

TEST_F(DatastoreTest, GetAnnotations_DeviceId) {
  const std::optional<std::string> device_id = DeviceIdProvider(kDeviceIdPath).GetId();
  ASSERT_TRUE(device_id.has_value());
  SetUpDatastore({kAnnotationDeviceFeedbackId}, kDefaultAttachmentsToAvoidSpuriousLogs);

  fit::result<Annotations> annotations = GetAnnotations();
  ASSERT_TRUE(annotations.is_ok());
  EXPECT_THAT(annotations.take_value(), ElementsAreArray({
                                            Pair(kAnnotationDeviceFeedbackId, device_id.value()),
                                        }));

  EXPECT_THAT(GetStaticAnnotations(), ElementsAreArray({
                                          Pair(kAnnotationDeviceFeedbackId, device_id.value()),
                                      }));

  ASSERT_TRUE(files::DeletePath(kDeviceIdPath, /*recursive=*/false));
}

TEST_F(DatastoreTest, GetAnnotations_ProductInfo) {
  fuchsia::hwinfo::ProductInfo info;
  info.set_language("my-language");
  info.set_manufacturer("my-manufacturer");
  info.set_model("my-model");
  info.set_name("my-name");
  info.set_sku("my-sku");

  fuchsia::intl::RegulatoryDomain domain;
  domain.set_country_code("my-regulatory-domain");
  info.set_regulatory_domain(std::move(domain));

  std::vector<fuchsia::intl::LocaleId> locales;
  for (const auto& locale : {"my-locale1", "my-locale2", "my-locale3"}) {
    locales.emplace_back();
    locales.back().id = locale;
  }
  info.set_locale_list(locales);
  SetUpProductProvider(std::make_unique<StubProduct>(std::move(info)));
  SetUpDatastore(
      {
          kAnnotationHardwareProductLanguage,
          kAnnotationHardwareProductLocaleList,
          kAnnotationHardwareProductManufacturer,
          kAnnotationHardwareProductModel,
          kAnnotationHardwareProductName,
          kAnnotationHardwareProductRegulatoryDomain,
          kAnnotationHardwareProductSKU,
      },
      kDefaultAttachmentsToAvoidSpuriousLogs);

  fit::result<Annotations> annotations = GetAnnotations();
  ASSERT_TRUE(annotations.is_ok());
  EXPECT_THAT(annotations.take_value(),
              ElementsAreArray({
                  Pair(kAnnotationHardwareProductLanguage, "my-language"),
                  Pair(kAnnotationHardwareProductLocaleList, "my-locale1, my-locale2, my-locale3"),
                  Pair(kAnnotationHardwareProductManufacturer, "my-manufacturer"),
                  Pair(kAnnotationHardwareProductModel, "my-model"),
                  Pair(kAnnotationHardwareProductName, "my-name"),
                  Pair(kAnnotationHardwareProductRegulatoryDomain, "my-regulatory-domain"),
                  Pair(kAnnotationHardwareProductSKU, "my-sku"),
              }));

  EXPECT_THAT(GetStaticAnnotations(), IsEmpty());
}

TEST_F(DatastoreTest, GetAnnotations_Time) {
  SetUpDatastore(
      {
          kAnnotationDeviceUptime,
          kAnnotationDeviceUTCTime,
      },
      kDefaultAttachmentsToAvoidSpuriousLogs);

  fit::result<Annotations> annotations = GetAnnotations();
  ASSERT_TRUE(annotations.is_ok());
  EXPECT_THAT(annotations.take_value(), ElementsAreArray({
                                            Pair(kAnnotationDeviceUptime, Not(IsEmpty())),
                                            Pair(kAnnotationDeviceUTCTime, Not(IsEmpty())),
                                        }));

  EXPECT_THAT(GetStaticAnnotations(), IsEmpty());
}

TEST_F(DatastoreTest, GetAnnotations_ExtraAnnotations) {
  SetUpDatastore(kDefaultAnnotationsToAvoidSpuriousLogs, kDefaultAttachmentsToAvoidSpuriousLogs);
  EXPECT_TRUE(TrySetExtraAnnotations({{"extra.k", "v"}}));

  fit::result<Annotations> annotations = GetAnnotations();
  ASSERT_TRUE(annotations.is_ok());
  EXPECT_THAT(annotations.take_value(), Contains(Pair("extra.k", "v")));
}

TEST_F(DatastoreTest, GetAnnotations_ExtraAnnotationsAboveLimit) {
  // We set one platform annotation in the allowlist and we then check that this is the only
  // annotation returned as we inject more extra annotations than allowed.
  SetUpDatastore(
      {
          kAnnotationBuildIsDebug,
      },
      kDefaultAttachmentsToAvoidSpuriousLogs);

  // We inject more than the limit in extra annotations.
  Annotations extra_annotations;
  for (size_t i = 0; i < kMaxNumExtraAnnotations + 1; i++) {
    extra_annotations[fxl::StringPrintf("k%lu", i)] = fxl::StringPrintf("v%lu", i);
  }
  EXPECT_FALSE(TrySetExtraAnnotations(extra_annotations));

  fit::result<Annotations> annotations = GetAnnotations();
  ASSERT_TRUE(annotations.is_ok());
  EXPECT_THAT(annotations.take_value(), ElementsAreArray({
                                            Pair(kAnnotationBuildIsDebug, Not(IsEmpty())),
                                        }));
}

TEST_F(DatastoreTest, GetAnnotations_ExtraAnnotationsOnEmptyAllowlist) {
  SetUpDatastore({}, kDefaultAttachmentsToAvoidSpuriousLogs);
  EXPECT_TRUE(TrySetExtraAnnotations({{"extra.k", "v"}}));

  fit::result<Annotations> annotations = GetAnnotations();
  ASSERT_TRUE(annotations.is_ok());
  EXPECT_THAT(annotations.take_value(), ElementsAreArray({Pair("extra.k", "v")}));
}

TEST_F(DatastoreTest, GetAnnotations_FailOn_EmptyAnnotationAllowlist) {
  SetUpDatastore({}, kDefaultAttachmentsToAvoidSpuriousLogs);

  fit::result<Annotations> annotations = GetAnnotations();
  ASSERT_TRUE(annotations.is_error());

  EXPECT_THAT(GetStaticAnnotations(), IsEmpty());
}

TEST_F(DatastoreTest, GetAnnotations_FailOn_OnlyUnknownAnnotationInAllowlist) {
  SetUpDatastore({"unknown.annotation"}, kDefaultAttachmentsToAvoidSpuriousLogs);

  fit::result<Annotations> annotations = GetAnnotations();
  ASSERT_TRUE(annotations.is_error());

  EXPECT_THAT(GetStaticAnnotations(), IsEmpty());
}

TEST_F(DatastoreTest, GetAttachments_Inspect) {
  // CollectInspectData() has its own set of unit tests so we only cover one chunk of Inspect data
  // here to check that we are attaching the Inspect data.
  SetUpInspect("foo");
  SetUpDatastore(kDefaultAnnotationsToAvoidSpuriousLogs, {kAttachmentInspect});

  fit::result<Attachments> attachments = GetAttachments();
  ASSERT_TRUE(attachments.is_ok());
  EXPECT_THAT(attachments.take_value(),
              ElementsAreArray({Pair(kAttachmentInspect, Eq("[\nfoo\n]"))}));

  EXPECT_THAT(GetStaticAttachments(), IsEmpty());
}

TEST_F(DatastoreTest, GetAttachments_PreviousSysLog) {
  const std::string previous_log_contents = "LAST SYSTEM LOG";
  SetUpPreviousSystemLog(previous_log_contents);
  SetUpDatastore(kDefaultAnnotationsToAvoidSpuriousLogs, {kAttachmentLogSystemPrevious});

  fit::result<Attachments> attachments = GetAttachments();
  ASSERT_TRUE(attachments.is_ok());
  EXPECT_THAT(attachments.take_value(),
              ElementsAreArray({Pair(kAttachmentLogSystemPrevious, Eq(previous_log_contents))}));

  EXPECT_THAT(GetStaticAttachments(),
              ElementsAreArray({Pair(kAttachmentLogSystemPrevious, Eq(previous_log_contents))}));
}

TEST_F(DatastoreTest, GetAttachments_SysLog) {
  // CollectSystemLogs() has its own set of unit tests so we only cover one log message here to
  // check that we are attaching the logs.
  SetUpLogger({
      BuildLogMessage(FX_LOG_INFO, "log message",
                      /*timestamp_offset=*/zx::duration(0), {"foo"}),
  });
  SetUpDatastore(kDefaultAnnotationsToAvoidSpuriousLogs, {kAttachmentLogSystem});

  fit::result<Attachments> attachments = GetAttachments();
  ASSERT_TRUE(attachments.is_ok());
  EXPECT_THAT(attachments.take_value(),
              ElementsAreArray({Pair(kAttachmentLogSystem,
                                     Eq("[15604.000][07559][07687][foo] INFO: log message\n"))}));

  EXPECT_THAT(GetStaticAttachments(), IsEmpty());
}

TEST_F(DatastoreTest, GetAttachments_FailOn_EmptyAttachmentAllowlist) {
  SetUpDatastore(kDefaultAnnotationsToAvoidSpuriousLogs, {});

  fit::result<Attachments> attachments = GetAttachments();
  ASSERT_TRUE(attachments.is_error());

  EXPECT_THAT(GetStaticAttachments(), IsEmpty());
}

TEST_F(DatastoreTest, GetAttachments_FailOn_OnlyUnknownAttachmentInAllowlist) {
  SetUpDatastore(kDefaultAnnotationsToAvoidSpuriousLogs, {"unknown.attachment"});

  fit::result<Attachments> attachments = GetAttachments();
  ASSERT_TRUE(attachments.is_error());

  EXPECT_THAT(GetStaticAttachments(), IsEmpty());
}

}  // namespace
}  // namespace feedback
