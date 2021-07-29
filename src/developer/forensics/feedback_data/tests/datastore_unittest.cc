// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/datastore.h"

#include <fuchsia/hwinfo/cpp/fidl.h>
#include <fuchsia/intl/cpp/fidl.h>
#include <lib/async/cpp/executor.h>
#include <lib/fpromise/result.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/syslog/logger.h>
#include <lib/zx/time.h>

#include <cstddef>
#include <memory>
#include <optional>
#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/feedback_data/annotations/types.h"
#include "src/developer/forensics/feedback_data/archive_accessor_ptr.h"
#include "src/developer/forensics/feedback_data/attachments/types.h"
#include "src/developer/forensics/feedback_data/constants.h"
#include "src/developer/forensics/testing/gmatchers.h"
#include "src/developer/forensics/testing/gpretty_printers.h"
#include "src/developer/forensics/testing/log_message.h"
#include "src/developer/forensics/testing/stubs/board_info_provider.h"
#include "src/developer/forensics/testing/stubs/channel_control.h"
#include "src/developer/forensics/testing/stubs/cobalt_logger_factory.h"
#include "src/developer/forensics/testing/stubs/device_id_provider.h"
#include "src/developer/forensics/testing/stubs/diagnostics_archive.h"
#include "src/developer/forensics/testing/stubs/diagnostics_batch_iterator.h"
#include "src/developer/forensics/testing/stubs/product_info_provider.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"
#include "src/developer/forensics/utils/cobalt/logger.h"
#include "src/developer/forensics/utils/cobalt/metrics.h"
#include "src/developer/forensics/utils/time.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/lib/timekeeper/test_clock.h"

namespace forensics {
namespace feedback_data {
namespace {

using testing::BuildLogMessage;
using ::testing::Contains;
using ::testing::ElementsAreArray;
using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::Not;
using ::testing::Pair;
using ::testing::UnorderedElementsAreArray;

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

class DatastoreTest : public UnitTestFixture {
 public:
  DatastoreTest() : executor_(dispatcher()) {}

  void SetUp() override {
    device_id_provider_ =
        std::make_unique<feedback::RemoteDeviceIdProvider>(dispatcher(), services());
    SetUpCobaltServer(std::make_unique<stubs::CobaltLoggerFactory>());
    cobalt_ = std::make_unique<cobalt::Logger>(dispatcher(), services(), &clock_);

    inspect_node_manager_ = std::make_unique<InspectNodeManager>(&InspectRoot());
    inspect_data_budget_ = std::make_unique<InspectDataBudget>(
        "non-existent_path", inspect_node_manager_.get(), cobalt_.get());
  }

  void TearDown() override { FX_CHECK(files::DeletePath(kCurrentLogsDir, /*recursive=*/true)); }

 protected:
  void SetUpDatastore(const AnnotationKeys& annotation_allowlist,
                      const AttachmentKeys& attachment_allowlist) {
    datastore_ = std::make_unique<Datastore>(
        dispatcher(), services(), cobalt_.get(), annotation_allowlist, attachment_allowlist,
        "current_boot_id", "previous_boot_id", "current_build_version", "previous_build_version",
        "last_reboot_reason", "last_reboot_uptime", device_id_provider_.get(),
        inspect_data_budget_.get());
  }

  void SetUpBoardProviderServer(std::unique_ptr<stubs::BoardInfoProviderBase> server) {
    board_provider_server_ = std::move(server);
    if (board_provider_server_) {
      InjectServiceProvider(board_provider_server_.get());
    }
  }

  void SetUpChannelProviderServer(std::unique_ptr<stubs::ChannelControlBase> server) {
    channel_provider_server_ = std::move(server);
    if (channel_provider_server_) {
      InjectServiceProvider(channel_provider_server_.get());
    }
  }

  void SetUpDeviceIdProviderServer(std::unique_ptr<stubs::DeviceIdProviderBase> server) {
    device_id_provider_server_ = std::move(server);
    if (device_id_provider_server_) {
      InjectServiceProvider(device_id_provider_server_.get());
    }
  }

  void SetUpDiagnosticsServer(const std::string& inspect_chunk) {
    diagnostics_server_ = std::make_unique<stubs::DiagnosticsArchive>(
        std::make_unique<stubs::DiagnosticsBatchIterator>(std::vector<std::vector<std::string>>({
            {inspect_chunk},
            {},
        })));
    InjectServiceProvider(diagnostics_server_.get(), kArchiveAccessorName);
  }

  void SetUpDiagnosticsServer(std::unique_ptr<stubs::DiagnosticsArchiveBase> server) {
    diagnostics_server_ = std::move(server);
    if (diagnostics_server_) {
      InjectServiceProvider(diagnostics_server_.get(), kArchiveAccessorName);
    }
  }

  void SetUpProductProviderServer(std::unique_ptr<stubs::ProductInfoProviderBase> server) {
    product_provider_server_ = std::move(server);
    if (product_provider_server_) {
      InjectServiceProvider(product_provider_server_.get());
    }
  }

  void WriteFile(const std::string& filepath, const std::string& content) {
    FX_CHECK(files::WriteFile(filepath, content.c_str(), content.size()));
  }

  ::fpromise::result<Annotations> GetAnnotations() {
    FX_CHECK(datastore_);

    ::fpromise::result<Annotations> result;
    executor_.schedule_task(datastore_->GetAnnotations(kTimeout).then(
        [&result](::fpromise::result<Annotations>& res) { result = std::move(res); }));
    RunLoopFor(kTimeout);
    return result;
  }

  ::fpromise::result<Attachments> GetAttachments() {
    FX_CHECK(datastore_);

    ::fpromise::result<Attachments> result;
    executor_.schedule_task(datastore_->GetAttachments(kTimeout).then(
        [&result](::fpromise::result<Attachments>& res) { result = std::move(res); }));
    RunLoopFor(kTimeout);
    return result;
  }

  bool TrySetNonPlatformAnnotations(const Annotations& non_platform_annotations) {
    return datastore_->TrySetNonPlatformAnnotations(non_platform_annotations);
  }
  Annotations GetStaticAnnotations() { return datastore_->GetStaticAnnotations(); }
  Attachments GetStaticAttachments() { return datastore_->GetStaticAttachments(); }

 private:
  async::Executor executor_;
  timekeeper::TestClock clock_;
  std::unique_ptr<feedback::DeviceIdProvider> device_id_provider_;
  std::unique_ptr<cobalt::Logger> cobalt_;

 protected:
  std::unique_ptr<Datastore> datastore_;

 private:
  std::unique_ptr<InspectNodeManager> inspect_node_manager_;
  std::unique_ptr<InspectDataBudget> inspect_data_budget_;

  // Stubs servers.
  std::unique_ptr<stubs::BoardInfoProviderBase> board_provider_server_;
  std::unique_ptr<stubs::ChannelControlBase> channel_provider_server_;
  std::unique_ptr<stubs::DeviceIdProviderBase> device_id_provider_server_;
  std::unique_ptr<stubs::DiagnosticsArchiveBase> diagnostics_server_;
  std::unique_ptr<stubs::ProductInfoProviderBase> product_provider_server_;
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
          kAnnotationBuildVersionPreviousBoot,
          kAnnotationDeviceBoardName,
          kAnnotationDeviceUptime,
          kAnnotationDeviceUtcTime,
          kAnnotationSystemLastRebootReason,
          kAnnotationSystemLastRebootUptime,
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
  SetUpBoardProviderServer(std::make_unique<stubs::BoardInfoProvider>(std::move(info)));
  SetUpDatastore(
      {
          kAnnotationHardwareBoardName,
          kAnnotationHardwareBoardRevision,
      },
      kDefaultAttachmentsToAvoidSpuriousLogs);

  ::fpromise::result<Annotations> annotations = GetAnnotations();
  ASSERT_TRUE(annotations.is_ok());
  EXPECT_THAT(annotations.take_value(), ElementsAreArray({
                                            Pair(kAnnotationHardwareBoardName, "my-board-name"),
                                            Pair(kAnnotationHardwareBoardRevision, "my-revision"),
                                        }));

  EXPECT_THAT(GetStaticAnnotations(), IsEmpty());
}

TEST_F(DatastoreTest, GetAnnotations_Channels) {
  SetUpChannelProviderServer(
      std::make_unique<stubs::ChannelControl>(stubs::ChannelControlBase::Params({
          .current = "current-channel",
          .target = "target-channel",
      })));
  SetUpDatastore(
      {
          kAnnotationSystemUpdateChannelCurrent,
          kAnnotationSystemUpdateChannelTarget,
      },
      kDefaultAttachmentsToAvoidSpuriousLogs);

  ::fpromise::result<Annotations> annotations = GetAnnotations();
  ASSERT_TRUE(annotations.is_ok());
  EXPECT_THAT(annotations.take_value(),
              ElementsAreArray({
                  Pair(kAnnotationSystemUpdateChannelCurrent, "current-channel"),
                  Pair(kAnnotationSystemUpdateChannelTarget, "target-channel"),
              }));

  EXPECT_THAT(GetStaticAnnotations(), IsEmpty());
}

TEST_F(DatastoreTest, GetAnnotations_DeviceId) {
  SetUpDeviceIdProviderServer(std::make_unique<stubs::DeviceIdProvider>("device-id"));
  SetUpDatastore({kAnnotationDeviceFeedbackId}, kDefaultAttachmentsToAvoidSpuriousLogs);

  ::fpromise::result<Annotations> annotations = GetAnnotations();
  ASSERT_TRUE(annotations.is_ok());
  EXPECT_THAT(annotations.take_value(), ElementsAreArray({
                                            Pair(kAnnotationDeviceFeedbackId, "device-id"),
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
  SetUpProductProviderServer(std::make_unique<stubs::ProductInfoProvider>(std::move(info)));
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

  ::fpromise::result<Annotations> annotations = GetAnnotations();
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
          kAnnotationDeviceUtcTime,
      },
      kDefaultAttachmentsToAvoidSpuriousLogs);

  ::fpromise::result<Annotations> annotations = GetAnnotations();
  ASSERT_TRUE(annotations.is_ok());
  EXPECT_THAT(annotations.take_value(), ElementsAreArray({
                                            Pair(kAnnotationDeviceUptime, HasValue()),
                                            Pair(kAnnotationDeviceUtcTime, HasValue()),
                                        }));

  EXPECT_THAT(GetStaticAnnotations(), IsEmpty());
}

TEST_F(DatastoreTest, GetAnnotations_NonPlatformAnnotations) {
  SetUpDatastore(kDefaultAnnotationsToAvoidSpuriousLogs, kDefaultAttachmentsToAvoidSpuriousLogs);
  EXPECT_TRUE(TrySetNonPlatformAnnotations({{"non-platform.k", "v"}}));

  ::fpromise::result<Annotations> annotations = GetAnnotations();
  ASSERT_TRUE(annotations.is_ok());
  EXPECT_THAT(annotations.take_value(), Contains(Pair("non-platform.k", "v")));
}

TEST_F(DatastoreTest, GetAnnotations_NonPlatformAboveLimit) {
  // We set one platform annotation in the allowlist and we then check that this is the only
  // annotation returned as we inject more non-platform annotations than allowed.
  SetUpDatastore(
      {
          kAnnotationBuildIsDebug,
      },
      kDefaultAttachmentsToAvoidSpuriousLogs);

  // We inject more than the limit in non-platform annotations.
  Annotations non_platform_annotations;
  for (size_t i = 0; i < kMaxNumNonPlatformAnnotations + 1; i++) {
    non_platform_annotations.insert({fxl::StringPrintf("k%lu", i), fxl::StringPrintf("v%lu", i)});
  }
  EXPECT_FALSE(TrySetNonPlatformAnnotations(non_platform_annotations));

  ::fpromise::result<Annotations> annotations = GetAnnotations();
  ASSERT_TRUE(annotations.is_ok());
  EXPECT_THAT(annotations.take_value(), ElementsAreArray({
                                            Pair(kAnnotationBuildIsDebug, HasValue()),
                                        }));
}

TEST_F(DatastoreTest, GetAnnotations_NonPlatformOnEmptyAllowlist) {
  SetUpDatastore({}, kDefaultAttachmentsToAvoidSpuriousLogs);
  EXPECT_TRUE(TrySetNonPlatformAnnotations({{"non-platform.k", "v"}}));

  ::fpromise::result<Annotations> annotations = GetAnnotations();
  ASSERT_TRUE(annotations.is_ok());
  EXPECT_THAT(annotations.take_value(), ElementsAreArray({Pair("non-platform.k", "v")}));
}

TEST_F(DatastoreTest, GetAnnotations_FailOn_EmptyAnnotationAllowlist) {
  SetUpDatastore({}, kDefaultAttachmentsToAvoidSpuriousLogs);

  ::fpromise::result<Annotations> annotations = GetAnnotations();
  ASSERT_TRUE(annotations.is_error());

  EXPECT_THAT(GetStaticAnnotations(), IsEmpty());
}

TEST_F(DatastoreTest, GetAnnotations_FailOn_OnlyUnknownAnnotationInAllowlist) {
  SetUpDatastore({"unknown.annotation"}, kDefaultAttachmentsToAvoidSpuriousLogs);

  ::fpromise::result<Annotations> annotations = GetAnnotations();

  ASSERT_TRUE(annotations.is_ok());
  EXPECT_THAT(annotations.value(), ElementsAreArray({
                                       Pair("unknown.annotation", Error::kMissingValue),
                                   }));

  EXPECT_THAT(GetStaticAnnotations(), IsEmpty());
}

TEST_F(DatastoreTest, GetAttachments_Inspect) {
  // CollectInspectData() has its own set of unit tests so we only cover one chunk of Inspect data
  // here to check that we are attaching the Inspect data.
  SetUpDiagnosticsServer("foo");
  SetUpDatastore(kDefaultAnnotationsToAvoidSpuriousLogs, {kAttachmentInspect});

  ::fpromise::result<Attachments> attachments = GetAttachments();
  ASSERT_TRUE(attachments.is_ok());
  EXPECT_THAT(attachments.take_value(),
              ElementsAreArray({Pair(kAttachmentInspect, AttachmentValue("[\nfoo\n]"))}));

  EXPECT_THAT(GetStaticAttachments(), IsEmpty());
}

TEST_F(DatastoreTest, GetAttachments_PreviousSyslogAlreadyCached) {
  const std::string previous_log_contents = "LAST SYSTEM LOG";
  WriteFile(kPreviousLogsFilePath, previous_log_contents);
  SetUpDatastore(kDefaultAnnotationsToAvoidSpuriousLogs, {kAttachmentLogSystemPrevious});

  ::fpromise::result<Attachments> attachments = GetAttachments();
  ASSERT_TRUE(attachments.is_ok());
  EXPECT_THAT(attachments.take_value(),
              ElementsAreArray(
                  {Pair(kAttachmentLogSystemPrevious, AttachmentValue(previous_log_contents))}));

  EXPECT_THAT(GetStaticAttachments(),
              ElementsAreArray(
                  {Pair(kAttachmentLogSystemPrevious, AttachmentValue(previous_log_contents))}));

  ASSERT_TRUE(files::DeletePath(kPreviousLogsFilePath, /*recursive=*/false));
}

TEST_F(DatastoreTest, GetAttachments_PreviousSyslogIsEmpty) {
  const std::string previous_log_contents = "";
  WriteFile(kPreviousLogsFilePath, previous_log_contents);
  SetUpDatastore(kDefaultAnnotationsToAvoidSpuriousLogs, {kAttachmentLogSystemPrevious});

  ::fpromise::result<Attachments> attachments = GetAttachments();
  ASSERT_TRUE(attachments.is_ok());
  EXPECT_THAT(attachments.take_value(),
              ElementsAreArray(
                  {Pair(kAttachmentLogSystemPrevious, AttachmentValue(Error::kMissingValue))}));

  EXPECT_THAT(GetStaticAttachments(),
              ElementsAreArray(
                  {Pair(kAttachmentLogSystemPrevious, AttachmentValue(Error::kMissingValue))}));

  ASSERT_TRUE(files::DeletePath(kPreviousLogsFilePath, /*recursive=*/false));
}

TEST_F(DatastoreTest, GetAttachments_DropPreviousSyslog) {
  const std::string previous_log_contents = "LAST SYSTEM LOG";
  WriteFile(kPreviousLogsFilePath, previous_log_contents);
  SetUpDatastore(kDefaultAnnotationsToAvoidSpuriousLogs, {kAttachmentLogSystemPrevious});

  datastore_->DropStaticAttachment(kAttachmentLogSystemPrevious, Error::kCustom);

  ::fpromise::result<Attachments> attachments = GetAttachments();
  ASSERT_TRUE(attachments.is_ok());

  EXPECT_THAT(
      GetStaticAttachments(),
      ElementsAreArray({Pair(kAttachmentLogSystemPrevious, AttachmentValue(Error::kCustom))}));
  ASSERT_TRUE(files::DeletePath(kPreviousLogsFilePath, /*recursive=*/false));
}

TEST_F(DatastoreTest, GetAttachments_SysLog) {
  // CollectSystemLogs() has its own set of unit tests so we only cover one log message here to
  // check that we are attaching the logs.
  SetUpDiagnosticsServer(R"JSON(
[
  {
    "metadata": {
      "timestamp": 15604000000000,
      "severity": "INFO",
      "pid": 7559,
      "tid": 7687,
      "tags": ["foo"]
    },
    "payload": {
      "root": {
        "message": {
          "value": "log message"
        }
      }
    }
  }
]
)JSON");
  SetUpDatastore(kDefaultAnnotationsToAvoidSpuriousLogs, {kAttachmentLogSystem});

  ::fpromise::result<Attachments> attachments = GetAttachments();
  ASSERT_TRUE(attachments.is_ok());
  EXPECT_THAT(attachments.take_value(),
              ElementsAreArray(
                  {Pair(kAttachmentLogSystem,
                        AttachmentValue("[15604.000][07559][07687][foo] INFO: log message\n"))}));

  EXPECT_THAT(GetStaticAttachments(), IsEmpty());
}

TEST_F(DatastoreTest, GetAttachments_FailOn_EmptyAttachmentAllowlist) {
  SetUpDatastore(kDefaultAnnotationsToAvoidSpuriousLogs, {});

  ::fpromise::result<Attachments> attachments = GetAttachments();
  ASSERT_TRUE(attachments.is_error());

  EXPECT_THAT(GetStaticAttachments(), IsEmpty());
}

TEST_F(DatastoreTest, GetAttachments_FailOn_OnlyUnknownAttachmentInAllowlist) {
  SetUpDatastore(kDefaultAnnotationsToAvoidSpuriousLogs, {"unknown.attachment"});

  ::fpromise::result<Attachments> attachments = GetAttachments();
  ASSERT_TRUE(attachments.is_error());

  EXPECT_THAT(GetStaticAttachments(), IsEmpty());
}

TEST_F(DatastoreTest, GetAttachments_CobaltLogsTimeouts) {
  // The timeout of the kernel log collection cannot be tested due to the fact that
  // fuchsia::boot::ReadOnlyLog cannot be stubbed and we have no mechanism to set the timeout of
  // the kernel log collection to 0 seconds.
  //
  // Inspect and system log share the same stub server so we only test one of the two (i.e.
  // Inspect).
  SetUpDatastore(kDefaultAnnotationsToAvoidSpuriousLogs, {
                                                             kAttachmentInspect,
                                                         });

  SetUpDiagnosticsServer(std::make_unique<stubs::DiagnosticsArchive>(
      std::make_unique<stubs::DiagnosticsBatchIteratorNeverResponds>()));

  ::fpromise::result<Attachments> attachments = GetAttachments();

  ASSERT_TRUE(attachments.is_ok());
  EXPECT_THAT(attachments.take_value(),
              ElementsAreArray({
                  Pair(kAttachmentInspect, AttachmentValue(Error::kTimeout)),
              }));

  EXPECT_THAT(ReceivedCobaltEvents(), UnorderedElementsAreArray({
                                          cobalt::Event(cobalt::TimedOutData::kInspect),
                                      }));
}

}  // namespace
}  // namespace feedback_data
}  // namespace forensics
