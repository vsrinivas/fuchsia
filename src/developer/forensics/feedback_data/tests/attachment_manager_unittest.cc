// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/attachment_manager.h"

#include <fuchsia/hwinfo/cpp/fidl.h>
#include <fuchsia/intl/cpp/fidl.h>
#include <lib/async/cpp/executor.h>
#include <lib/fpromise/result.h>
#include <lib/inspect/cpp/vmo/types.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/syslog/logger.h>
#include <lib/zx/time.h>

#include <cstddef>
#include <memory>
#include <optional>
#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/feedback_data/attachments/types.h"
#include "src/developer/forensics/feedback_data/constants.h"
#include "src/developer/forensics/testing/gmatchers.h"
#include "src/developer/forensics/testing/gpretty_printers.h"
#include "src/developer/forensics/testing/log_message.h"
#include "src/developer/forensics/testing/stubs/channel_control.h"
#include "src/developer/forensics/testing/stubs/cobalt_logger_factory.h"
#include "src/developer/forensics/testing/stubs/diagnostics_archive.h"
#include "src/developer/forensics/testing/stubs/diagnostics_batch_iterator.h"
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

// Allowlist to use in test cases where the attachments don't matter, but where we want to avoid
// spurious logs due to empty attachment allowlist.
const AttachmentKeys kDefaultAttachmentsToAvoidSpuriousLogs = {
    kAttachmentBuildSnapshot,
};

class AttachmentManagerTest : public UnitTestFixture {
 public:
  AttachmentManagerTest() : executor_(dispatcher()) {}

  void SetUp() override {
    SetUpCobaltServer(std::make_unique<stubs::CobaltLoggerFactory>());
    cobalt_ = std::make_unique<cobalt::Logger>(dispatcher(), services(), &clock_);

    inspect_node_manager_ = std::make_unique<InspectNodeManager>(&InspectRoot());
    inspect_data_budget_ = std::make_unique<InspectDataBudget>(
        "non-existent_path", inspect_node_manager_.get(), cobalt_.get());
  }

  void TearDown() override { FX_CHECK(files::DeletePath(kCurrentLogsDir, /*recursive=*/true)); }

 protected:
  void SetUpAttachmentManager(
      const AttachmentKeys& attachment_allowlist,
      const std::map<std::string, ErrorOr<std::string>>& startup_annotations = {}) {
    std::set<std::string> allowlist;
    for (const auto& [k, _] : startup_annotations) {
      allowlist.insert(k);
    }
    attachment_manager_ =
        std::make_unique<AttachmentManager>(dispatcher(), services(), cobalt_.get(), &redactor_,
                                            attachment_allowlist, inspect_data_budget_.get());
  }

  void SetUpDiagnosticsServer(const std::string& inspect_chunk) {
    diagnostics_server_ = std::make_unique<stubs::DiagnosticsArchive>(
        std::make_unique<stubs::DiagnosticsBatchIterator>(std::vector<std::vector<std::string>>({
            {inspect_chunk},
            {},
        })));
    InjectServiceProvider(diagnostics_server_.get(), kArchiveAccessorName);
  }

  void SetUpLogServer(const std::string& inspect_chunk) {
    diagnostics_server_ = std::make_unique<stubs::DiagnosticsArchive>(
        std::make_unique<stubs::DiagnosticsBatchIteratorNeverRespondsAfterOneBatch>(
            std::vector<std::string>({
                {inspect_chunk},
            })));
    InjectServiceProvider(diagnostics_server_.get(), kArchiveAccessorName);
  }

  void SetUpDiagnosticsServer(std::unique_ptr<stubs::DiagnosticsArchiveBase> server) {
    diagnostics_server_ = std::move(server);
    if (diagnostics_server_) {
      InjectServiceProvider(diagnostics_server_.get(), kArchiveAccessorName);
    }
  }

  void WriteFile(const std::string& filepath, const std::string& content) {
    FX_CHECK(files::WriteFile(filepath, content.c_str(), content.size()));
  }

  ::fpromise::result<Attachments> GetAttachments() {
    FX_CHECK(attachment_manager_);

    ::fpromise::result<Attachments> result;
    executor_.schedule_task(attachment_manager_->GetAttachments(kTimeout).then(
        [&result](::fpromise::result<Attachments>& res) { result = std::move(res); }));
    RunLoopFor(kTimeout);
    return result;
  }

  Attachments GetStaticAttachments() { return attachment_manager_->GetStaticAttachments(); }

 private:
  async::Executor executor_;
  timekeeper::TestClock clock_;
  std::unique_ptr<cobalt::Logger> cobalt_;
  IdentityRedactor redactor_{inspect::BoolProperty()};

 protected:
  std::unique_ptr<AttachmentManager> attachment_manager_;

 private:
  std::unique_ptr<InspectNodeManager> inspect_node_manager_;
  std::unique_ptr<InspectDataBudget> inspect_data_budget_;

  // Stubs servers.
  std::unique_ptr<stubs::DiagnosticsArchiveBase> diagnostics_server_;
};

TEST_F(AttachmentManagerTest, GetAttachments_Inspect) {
  // CollectInspectData() has its own set of unit tests so we only cover one chunk of Inspect data
  // here to check that we are attaching the Inspect data.
  SetUpDiagnosticsServer("foo");
  SetUpAttachmentManager({kAttachmentInspect});

  ::fpromise::result<Attachments> attachments = GetAttachments();
  ASSERT_TRUE(attachments.is_ok());
  EXPECT_THAT(attachments.take_value(),
              ElementsAreArray({Pair(kAttachmentInspect, AttachmentValue("[\nfoo\n]"))}));

  EXPECT_THAT(GetStaticAttachments(), IsEmpty());
}

TEST_F(AttachmentManagerTest, GetAttachments_PreviousSyslogAlreadyCached) {
  const std::string previous_log_contents = "LAST SYSTEM LOG";
  WriteFile(kPreviousLogsFilePath, previous_log_contents);
  SetUpAttachmentManager({kAttachmentLogSystemPrevious});

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

TEST_F(AttachmentManagerTest, GetAttachments_PreviousSyslogIsEmpty) {
  const std::string previous_log_contents = "";
  WriteFile(kPreviousLogsFilePath, previous_log_contents);
  SetUpAttachmentManager({kAttachmentLogSystemPrevious});

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

TEST_F(AttachmentManagerTest, GetAttachments_DropPreviousSyslog) {
  const std::string previous_log_contents = "LAST SYSTEM LOG";
  WriteFile(kPreviousLogsFilePath, previous_log_contents);
  SetUpAttachmentManager({kAttachmentLogSystemPrevious});

  attachment_manager_->DropStaticAttachment(kAttachmentLogSystemPrevious, Error::kCustom);

  ::fpromise::result<Attachments> attachments = GetAttachments();
  ASSERT_TRUE(attachments.is_ok());

  EXPECT_THAT(
      GetStaticAttachments(),
      ElementsAreArray({Pair(kAttachmentLogSystemPrevious, AttachmentValue(Error::kCustom))}));
  ASSERT_TRUE(files::DeletePath(kPreviousLogsFilePath, /*recursive=*/false));
}

TEST_F(AttachmentManagerTest, GetAttachments_SysLog) {
  // CollectSystemLogs() has its own set of unit tests so we only cover one log message here to
  // check that we are attaching the logs.
  SetUpLogServer(R"JSON(
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
  SetUpAttachmentManager({kAttachmentLogSystem});

  ::fpromise::result<Attachments> attachments = GetAttachments();
  ASSERT_TRUE(attachments.is_ok());
  EXPECT_THAT(attachments.take_value(),
              ElementsAreArray(
                  {Pair(kAttachmentLogSystem,
                        AttachmentValue("[15604.000][07559][07687][foo] INFO: log message\n"))}));

  EXPECT_THAT(GetStaticAttachments(), IsEmpty());
}

TEST_F(AttachmentManagerTest, GetAttachments_FailOn_EmptyAttachmentAllowlist) {
  SetUpAttachmentManager({});

  ::fpromise::result<Attachments> attachments = GetAttachments();
  ASSERT_TRUE(attachments.is_error());

  EXPECT_THAT(GetStaticAttachments(), IsEmpty());
}

TEST_F(AttachmentManagerTest, GetAttachments_FailOn_OnlyUnknownAttachmentInAllowlist) {
  SetUpAttachmentManager({"unknown.attachment"});

  ::fpromise::result<Attachments> attachments = GetAttachments();
  ASSERT_TRUE(attachments.is_error());

  EXPECT_THAT(GetStaticAttachments(), IsEmpty());
}

TEST_F(AttachmentManagerTest, GetAttachments_CobaltLogsTimeouts) {
  // The timeout of the kernel log collection cannot be tested due to the fact that
  // fuchsia::boot::ReadOnlyLog cannot be stubbed and we have no mechanism to set the timeout of
  // the kernel log collection to 0 seconds.
  //
  // Inspect and system log share the same stub server so we only test one of the two (i.e.
  // Inspect).
  SetUpAttachmentManager({
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
