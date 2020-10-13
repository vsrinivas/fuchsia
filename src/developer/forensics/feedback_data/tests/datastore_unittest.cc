// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/datastore.h"

#include <fuchsia/hwinfo/cpp/fidl.h>
#include <fuchsia/intl/cpp/fidl.h>
#include <lib/async/cpp/executor.h>
#include <lib/fit/result.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/syslog/logger.h>
#include <lib/zx/time.h>

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/feedback_data/annotations/types.h"
#include "src/developer/forensics/feedback_data/attachments/inspect_ptr.h"
#include "src/developer/forensics/feedback_data/attachments/types.h"
#include "src/developer/forensics/feedback_data/constants.h"
#include "src/developer/forensics/feedback_data/system_log_recorder/encoding/production_encoding.h"
#include "src/developer/forensics/feedback_data/system_log_recorder/encoding/version.h"
#include "src/developer/forensics/feedback_data/system_log_recorder/reader.h"
#include "src/developer/forensics/testing/gmatchers.h"
#include "src/developer/forensics/testing/gpretty_printers.h"
#include "src/developer/forensics/testing/stubs/board_info_provider.h"
#include "src/developer/forensics/testing/stubs/channel_provider.h"
#include "src/developer/forensics/testing/stubs/cobalt_logger_factory.h"
#include "src/developer/forensics/testing/stubs/device_id_provider.h"
#include "src/developer/forensics/testing/stubs/inspect_archive.h"
#include "src/developer/forensics/testing/stubs/inspect_batch_iterator.h"
#include "src/developer/forensics/testing/stubs/last_reboot_info_provider.h"
#include "src/developer/forensics/testing/stubs/logger.h"
#include "src/developer/forensics/testing/stubs/product_info_provider.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"
#include "src/developer/forensics/utils/cobalt/logger.h"
#include "src/developer/forensics/utils/cobalt/metrics.h"
#include "src/developer/forensics/utils/log_format.h"
#include "src/developer/forensics/utils/time.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace forensics {
namespace feedback_data {
namespace {

using stubs::BuildLogMessage;
using testing::Contains;
using testing::ElementsAreArray;
using testing::Eq;
using testing::IsEmpty;
using testing::Not;
using testing::Pair;
using testing::UnorderedElementsAreArray;

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

std::string MakeFilepath(const std::string& dir, const size_t file_num) {
  return files::JoinPath(dir, std::to_string(file_num));
}

const std::vector<std::string> kCurrentLogFilePaths = {
    MakeFilepath(kCurrentLogsDir, 0), MakeFilepath(kCurrentLogsDir, 1),
    MakeFilepath(kCurrentLogsDir, 2), MakeFilepath(kCurrentLogsDir, 3),
    MakeFilepath(kCurrentLogsDir, 4), MakeFilepath(kCurrentLogsDir, 5),
    MakeFilepath(kCurrentLogsDir, 6), MakeFilepath(kCurrentLogsDir, 7),
};

class DatastoreTest : public UnitTestFixture {
 public:
  DatastoreTest() : executor_(dispatcher()) {}

  void SetUp() override {
    SetUpCobaltServer(std::make_unique<stubs::CobaltLoggerFactory>());
    cobalt_ = std::make_unique<cobalt::Logger>(dispatcher(), services());
    FX_CHECK(files::CreateDirectory(kCurrentLogsDir));
  }

  void TearDown() override { FX_CHECK(files::DeletePath(kCurrentLogsDir, /*recursive=*/true)); }

 protected:
  void SetUpDatastore(const AnnotationKeys& annotation_allowlist,
                      const AttachmentKeys& attachment_allowlist,
                      const bool is_first_instance = true) {
    datastore_ =
        std::make_unique<Datastore>(dispatcher(), services(), cobalt_.get(), annotation_allowlist,
                                    attachment_allowlist, is_first_instance);
  }

  void SetUpBoardProviderServer(std::unique_ptr<stubs::BoardInfoProviderBase> server) {
    board_provider_server_ = std::move(server);
    if (board_provider_server_) {
      InjectServiceProvider(board_provider_server_.get());
    }
  }

  void SetUpChannelProviderServer(std::unique_ptr<stubs::ChannelProviderBase> server) {
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

  void SetUpInspectServer(const std::string& inspect_chunk) {
    inspect_server_ = std::make_unique<stubs::InspectArchive>(
        std::make_unique<stubs::InspectBatchIterator>(std::vector<std::vector<std::string>>({
            {inspect_chunk},
            {},
        })));
    InjectServiceProvider(inspect_server_.get(), kArchiveAccessorName);
  }

  void SetUpInspectServer(std::unique_ptr<stubs::InspectArchiveBase> server) {
    inspect_server_ = std::move(server);
    if (inspect_server_) {
      InjectServiceProvider(inspect_server_.get(), kArchiveAccessorName);
    }
  }

  void SetUpLastRebootInfoProviderServer(
      std::unique_ptr<stubs::LastRebootInfoProviderBase> server) {
    last_reboot_info_provider_server_ = std::move(server);
    if (last_reboot_info_provider_server_) {
      InjectServiceProvider(last_reboot_info_provider_server_.get());
    }
  }

  void SetUpLoggerServer(const std::vector<fuchsia::logger::LogMessage>& messages) {
    auto new_logger = std::make_unique<stubs::Logger>();
    new_logger->set_messages(messages);
    logger_server_ = std::move(new_logger);
    InjectServiceProvider(logger_server_.get());
  }

  void SetUpLoggerServer(std::unique_ptr<stubs::LoggerBase> server) {
    logger_server_ = std::move(server);
    if (logger_server_) {
      InjectServiceProvider(logger_server_.get());
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

  ::fit::result<Annotations> GetAnnotations() {
    FX_CHECK(datastore_);

    ::fit::result<Annotations> result;
    executor_.schedule_task(datastore_->GetAnnotations(kTimeout).then(
        [&result](::fit::result<Annotations>& res) { result = std::move(res); }));
    RunLoopFor(kTimeout);
    return result;
  }

  ::fit::result<Attachments> GetAttachments() {
    FX_CHECK(datastore_);

    ::fit::result<Attachments> result;
    executor_.schedule_task(datastore_->GetAttachments(kTimeout).then(
        [&result](::fit::result<Attachments>& res) { result = std::move(res); }));
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
  std::unique_ptr<cobalt::Logger> cobalt_;
  std::unique_ptr<Datastore> datastore_;

  // Stubs servers.
  std::unique_ptr<stubs::BoardInfoProviderBase> board_provider_server_;
  std::unique_ptr<stubs::ChannelProviderBase> channel_provider_server_;
  std::unique_ptr<stubs::DeviceIdProviderBase> device_id_provider_server_;
  std::unique_ptr<stubs::InspectArchiveBase> inspect_server_;
  std::unique_ptr<stubs::LastRebootInfoProviderBase> last_reboot_info_provider_server_;
  std::unique_ptr<stubs::LoggerBase> logger_server_;
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

  ::fit::result<Annotations> annotations = GetAnnotations();
  ASSERT_TRUE(annotations.is_ok());
  EXPECT_THAT(annotations.take_value(),
              ElementsAreArray({
                  Pair(kAnnotationHardwareBoardName, AnnotationOr("my-board-name")),
                  Pair(kAnnotationHardwareBoardRevision, AnnotationOr("my-revision")),
              }));

  EXPECT_THAT(GetStaticAnnotations(), IsEmpty());
}

TEST_F(DatastoreTest, GetAnnotations_Channel) {
  SetUpChannelProviderServer(std::make_unique<stubs::ChannelProvider>("my-channel"));
  SetUpDatastore({kAnnotationSystemUpdateChannelCurrent}, kDefaultAttachmentsToAvoidSpuriousLogs);

  ::fit::result<Annotations> annotations = GetAnnotations();
  ASSERT_TRUE(annotations.is_ok());
  EXPECT_THAT(annotations.take_value(),
              ElementsAreArray({
                  Pair(kAnnotationSystemUpdateChannelCurrent, AnnotationOr("my-channel")),
              }));

  EXPECT_THAT(GetStaticAnnotations(), IsEmpty());
}

TEST_F(DatastoreTest, GetAnnotations_DeviceId) {
  SetUpDeviceIdProviderServer(std::make_unique<stubs::DeviceIdProvider>("device-id"));
  SetUpDatastore({kAnnotationDeviceFeedbackId}, kDefaultAttachmentsToAvoidSpuriousLogs);

  ::fit::result<Annotations> annotations = GetAnnotations();
  ASSERT_TRUE(annotations.is_ok());
  EXPECT_THAT(annotations.take_value(),
              ElementsAreArray({
                  Pair(kAnnotationDeviceFeedbackId, AnnotationOr("device-id")),
              }));

  ASSERT_TRUE(files::DeletePath(kDeviceIdPath, /*recursive=*/false));
}

TEST_F(DatastoreTest, GetAnnotations_LastRebootInfo) {
  const zx::duration uptime = zx::hour(10);
  const auto uptime_str = FormatDuration(uptime);
  ASSERT_TRUE(uptime_str.has_value());

  fuchsia::feedback::LastReboot last_reboot;
  last_reboot.set_graceful(true).set_uptime(uptime.get());
  SetUpLastRebootInfoProviderServer(
      std::make_unique<stubs::LastRebootInfoProvider>(std::move(last_reboot)));
  SetUpDatastore(
      {
          kAnnotationSystemLastRebootReason,
          kAnnotationSystemLastRebootUptime,
      },
      kDefaultAttachmentsToAvoidSpuriousLogs);

  ::fit::result<Annotations> annotations = GetAnnotations();
  ASSERT_TRUE(annotations.is_ok());
  EXPECT_THAT(annotations.take_value(),
              ElementsAreArray({
                  Pair(kAnnotationSystemLastRebootReason, AnnotationOr("graceful")),
                  Pair(kAnnotationSystemLastRebootUptime, AnnotationOr(uptime_str.value())),
              }));

  EXPECT_THAT(GetStaticAnnotations(), IsEmpty());
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

  ::fit::result<Annotations> annotations = GetAnnotations();
  ASSERT_TRUE(annotations.is_ok());
  EXPECT_THAT(
      annotations.take_value(),
      ElementsAreArray({
          Pair(kAnnotationHardwareProductLanguage, AnnotationOr("my-language")),
          Pair(kAnnotationHardwareProductLocaleList,
               AnnotationOr("my-locale1, my-locale2, my-locale3")),
          Pair(kAnnotationHardwareProductManufacturer, AnnotationOr("my-manufacturer")),
          Pair(kAnnotationHardwareProductModel, AnnotationOr("my-model")),
          Pair(kAnnotationHardwareProductName, AnnotationOr("my-name")),
          Pair(kAnnotationHardwareProductRegulatoryDomain, AnnotationOr("my-regulatory-domain")),
          Pair(kAnnotationHardwareProductSKU, AnnotationOr("my-sku")),
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

  ::fit::result<Annotations> annotations = GetAnnotations();
  ASSERT_TRUE(annotations.is_ok());
  EXPECT_THAT(annotations.take_value(), ElementsAreArray({
                                            Pair(kAnnotationDeviceUptime, HasValue()),
                                            Pair(kAnnotationDeviceUtcTime, HasValue()),
                                        }));

  EXPECT_THAT(GetStaticAnnotations(), IsEmpty());
}

TEST_F(DatastoreTest, GetAnnotations_NonPlatformAnnotations) {
  SetUpDatastore(kDefaultAnnotationsToAvoidSpuriousLogs, kDefaultAttachmentsToAvoidSpuriousLogs);
  EXPECT_TRUE(TrySetNonPlatformAnnotations({{"non-platform.k", AnnotationOr("v")}}));

  ::fit::result<Annotations> annotations = GetAnnotations();
  ASSERT_TRUE(annotations.is_ok());
  EXPECT_THAT(annotations.take_value(), Contains(Pair("non-platform.k", AnnotationOr("v"))));
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
    non_platform_annotations.insert(
        {fxl::StringPrintf("k%lu", i), AnnotationOr(fxl::StringPrintf("v%lu", i))});
  }
  EXPECT_FALSE(TrySetNonPlatformAnnotations(non_platform_annotations));

  ::fit::result<Annotations> annotations = GetAnnotations();
  ASSERT_TRUE(annotations.is_ok());
  EXPECT_THAT(annotations.take_value(), ElementsAreArray({
                                            Pair(kAnnotationBuildIsDebug, HasValue()),
                                        }));
}

TEST_F(DatastoreTest, GetAnnotations_NonPlatformOnEmptyAllowlist) {
  SetUpDatastore({}, kDefaultAttachmentsToAvoidSpuriousLogs);
  EXPECT_TRUE(TrySetNonPlatformAnnotations({{"non-platform.k", AnnotationOr("v")}}));

  ::fit::result<Annotations> annotations = GetAnnotations();
  ASSERT_TRUE(annotations.is_ok());
  EXPECT_THAT(annotations.take_value(),
              ElementsAreArray({Pair("non-platform.k", AnnotationOr("v"))}));
}

TEST_F(DatastoreTest, GetAnnotations_FailOn_EmptyAnnotationAllowlist) {
  SetUpDatastore({}, kDefaultAttachmentsToAvoidSpuriousLogs);

  ::fit::result<Annotations> annotations = GetAnnotations();
  ASSERT_TRUE(annotations.is_error());

  EXPECT_THAT(GetStaticAnnotations(), IsEmpty());
}

TEST_F(DatastoreTest, GetAnnotations_FailOn_OnlyUnknownAnnotationInAllowlist) {
  SetUpDatastore({"unknown.annotation"}, kDefaultAttachmentsToAvoidSpuriousLogs);

  ::fit::result<Annotations> annotations = GetAnnotations();

  ASSERT_TRUE(annotations.is_ok());
  EXPECT_THAT(annotations.value(),
              ElementsAreArray({
                  Pair("unknown.annotation", AnnotationOr(Error::kMissingValue)),
              }));

  EXPECT_THAT(GetStaticAnnotations(), IsEmpty());
}

TEST_F(DatastoreTest, GetAttachments_Inspect) {
  // CollectInspectData() has its own set of unit tests so we only cover one chunk of Inspect data
  // here to check that we are attaching the Inspect data.
  SetUpInspectServer("foo");
  SetUpDatastore(kDefaultAnnotationsToAvoidSpuriousLogs, {kAttachmentInspect});

  ::fit::result<Attachments> attachments = GetAttachments();
  ASSERT_TRUE(attachments.is_ok());
  EXPECT_THAT(attachments.take_value(),
              ElementsAreArray({Pair(kAttachmentInspect, AttachmentValue("[\nfoo\n]"))}));

  EXPECT_THAT(GetStaticAttachments(), IsEmpty());
}

MATCHER_P2(MatchesCobaltEvent, expected_type, expected_metric_id, "") {
  return arg.type == expected_type && arg.metric_id == expected_metric_id;
}

TEST_F(DatastoreTest, GetAttachments_PreviousSyslog) {
  std::string previous_log_contents = "";
  for (const auto& filepath : kCurrentLogFilePaths) {
    auto encoder = system_log_recorder::ProductionEncoder();
    const std::string str = Format(BuildLogMessage(FX_LOG_INFO, "Log for file: " + filepath));
    previous_log_contents = previous_log_contents + str;
    WriteFile(filepath, encoder.Encode(str));
  }
  SetUpDatastore(kDefaultAnnotationsToAvoidSpuriousLogs, {kAttachmentLogSystemPrevious});

  ::fit::result<Attachments> attachments = GetAttachments();
  ASSERT_TRUE(attachments.is_ok());
  EXPECT_THAT(attachments.value(),
              ElementsAreArray(
                  {Pair(kAttachmentLogSystemPrevious, AttachmentValue(previous_log_contents))}));

  EXPECT_THAT(GetStaticAttachments(),
              ElementsAreArray(
                  {Pair(kAttachmentLogSystemPrevious, AttachmentValue(previous_log_contents))}));

  ASSERT_TRUE(files::DeletePath(kPreviousLogsFilePath, /*recursive=*/false));
  for (const auto& file : kCurrentLogFilePaths) {
    ASSERT_TRUE(files::DeletePath(file, /*recursive=*/false));
  }

  // Verify the event type and metric_id.
  EXPECT_THAT(ReceivedCobaltEvents(),
              UnorderedElementsAreArray({
                  MatchesCobaltEvent(cobalt::EventType::kCount,
                                     cobalt_registry::kPreviousBootLogCompressionRatioMetricId),
              }));
}

TEST_F(DatastoreTest, GetAttachments_PreviousSyslogAlreadyCached) {
  const std::string previous_log_contents = "LAST SYSTEM LOG";
  WriteFile(kPreviousLogsFilePath, previous_log_contents);
  SetUpDatastore(kDefaultAnnotationsToAvoidSpuriousLogs, {kAttachmentLogSystemPrevious},
                 /*is_first_instance=*/false);

  ::fit::result<Attachments> attachments = GetAttachments();
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
  SetUpDatastore(kDefaultAnnotationsToAvoidSpuriousLogs, {kAttachmentLogSystemPrevious},
                 /*is_first_instance=*/false);

  ::fit::result<Attachments> attachments = GetAttachments();
  ASSERT_TRUE(attachments.is_ok());
  EXPECT_THAT(attachments.take_value(),
              ElementsAreArray(
                  {Pair(kAttachmentLogSystemPrevious, AttachmentValue(Error::kMissingValue))}));

  EXPECT_THAT(GetStaticAttachments(),
              ElementsAreArray(
                  {Pair(kAttachmentLogSystemPrevious, AttachmentValue(Error::kMissingValue))}));

  ASSERT_TRUE(files::DeletePath(kPreviousLogsFilePath, /*recursive=*/false));
}

TEST_F(DatastoreTest, GetAttachments_PreviousSyslogNotFirstInstance) {
  // Simulate a case where there is no logs from the previous boot cycle and then a restart during
  // the current boot cycle. We want to make sure that we are not including the logs for the
  // current boot cycle as "previous boot logs".
  for (const auto& filepath : kCurrentLogFilePaths) {
    WriteFile(filepath, "Test data.");
  }
  SetUpDatastore(kDefaultAnnotationsToAvoidSpuriousLogs, {kAttachmentLogSystemPrevious},
                 /*is_first_instance=*/false);

  ::fit::result<Attachments> attachments = GetAttachments();
  ASSERT_TRUE(attachments.is_ok());
  EXPECT_THAT(attachments.value(),
              ElementsAreArray(
                  {Pair(kAttachmentLogSystemPrevious, AttachmentValue(Error::kFileReadFailure))}));

  EXPECT_THAT(GetStaticAttachments(),
              ElementsAreArray(
                  {Pair(kAttachmentLogSystemPrevious, AttachmentValue(Error::kFileReadFailure))}));

  ASSERT_TRUE(files::DeletePath(kPreviousLogsFilePath, /*recursive=*/false));
  for (const auto& file : kCurrentLogFilePaths) {
    ASSERT_TRUE(files::DeletePath(file, /*recursive=*/false));
  }
}

TEST_F(DatastoreTest, GetAttachments_SysLog) {
  // CollectSystemLogs() has its own set of unit tests so we only cover one log message here to
  // check that we are attaching the logs.
  SetUpLoggerServer({
      stubs::BuildLogMessage(FX_LOG_INFO, "log message",
                             /*timestamp_offset=*/zx::duration(0), {"foo"}),
  });
  SetUpDatastore(kDefaultAnnotationsToAvoidSpuriousLogs, {kAttachmentLogSystem});

  ::fit::result<Attachments> attachments = GetAttachments();
  ASSERT_TRUE(attachments.is_ok());
  EXPECT_THAT(attachments.take_value(),
              ElementsAreArray(
                  {Pair(kAttachmentLogSystem,
                        AttachmentValue("[15604.000][07559][07687][foo] INFO: log message\n"))}));

  EXPECT_THAT(GetStaticAttachments(), IsEmpty());
}

TEST_F(DatastoreTest, GetAttachments_FailOn_EmptyAttachmentAllowlist) {
  SetUpDatastore(kDefaultAnnotationsToAvoidSpuriousLogs, {});

  ::fit::result<Attachments> attachments = GetAttachments();
  ASSERT_TRUE(attachments.is_error());

  EXPECT_THAT(GetStaticAttachments(), IsEmpty());
}

TEST_F(DatastoreTest, GetAttachments_FailOn_OnlyUnknownAttachmentInAllowlist) {
  SetUpDatastore(kDefaultAnnotationsToAvoidSpuriousLogs, {"unknown.attachment"});

  ::fit::result<Attachments> attachments = GetAttachments();
  ASSERT_TRUE(attachments.is_error());

  EXPECT_THAT(GetStaticAttachments(), IsEmpty());
}

TEST_F(DatastoreTest, GetAttachments_CobaltLogsTimeouts) {
  // The timeout of the kernel log collection cannot be tested due to the fact that
  // fuchsia::boot::ReadOnlyLog cannot be stubbed and we have no mechanism to set the timeout of the
  // kernel log collection to 0 seconds.
  SetUpDatastore(kDefaultAnnotationsToAvoidSpuriousLogs, {
                                                             kAttachmentInspect,
                                                             kAttachmentLogSystem,
                                                         });

  SetUpInspectServer(std::make_unique<stubs::InspectArchive>(
      std::make_unique<stubs::InspectBatchIteratorNeverResponds>()));
  SetUpLoggerServer(std::make_unique<stubs::LoggerBindsToLogListenerButNeverCalls>());

  ::fit::result<Attachments> attachments = GetAttachments();

  ASSERT_TRUE(attachments.is_ok());
  EXPECT_THAT(attachments.take_value(),
              ElementsAreArray({
                  Pair(kAttachmentInspect, AttachmentValue(Error::kTimeout)),
                  Pair(kAttachmentLogSystem, AttachmentValue(Error::kTimeout)),
              }));

  EXPECT_THAT(ReceivedCobaltEvents(), UnorderedElementsAreArray({
                                          cobalt::Event(cobalt::TimedOutData::kInspect),
                                          cobalt::Event(cobalt::TimedOutData::kSystemLog),
                                      }));
}

}  // namespace
}  // namespace feedback_data
}  // namespace forensics
