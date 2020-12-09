// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/attachments/system_log_ptr.h"

#include <fuchsia/mem/cpp/fidl.h>
#include <lib/async/cpp/executor.h>
#include <lib/fit/result.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/zx/time.h>

#include <memory>
#include <string>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/feedback_data/archive_accessor_ptr.h"
#include "src/developer/forensics/feedback_data/attachments/types.h"
#include "src/developer/forensics/testing/gpretty_printers.h"
#include "src/developer/forensics/testing/stubs/diagnostics_archive.h"
#include "src/developer/forensics/testing/stubs/diagnostics_batch_iterator.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"
#include "src/developer/forensics/utils/errors.h"

namespace forensics {
namespace feedback_data {
namespace {

using testing::IsEmpty;
using testing::UnorderedElementsAreArray;

constexpr char kMessage1Json[] = R"JSON(
[
  {
    "metadata": {
      "timestamp": 1234000000000,
      "severity": "Info"
    },
    "payload": {
      "root": {
        "message": "Message 1",
        "pid": 200,
        "tid": 300,
        "tag": "tag_1, tag_a"
      }
    }
  }
]
)JSON";

constexpr char kMessage2Json[] = R"JSON(
[
  {
    "metadata": {
      "timestamp": 1234000000000,
      "severity": "Info"
    },
    "payload": {
      "root": {
        "message": "Message 2",
        "pid": 200,
        "tid": 300,
        "tag": "tag_2"
      }
    }
  }
]
)JSON";

constexpr char kMessage3Json[] = R"JSON(
[
  {
    "metadata": {
      "timestamp": 1234000000000,
      "severity": "Info"
    },
    "payload": {
      "root": {
        "message": "Message 3",
        "pid": 200,
        "tid": 300,
        "tag": "tag_3"
      }
    }
  }
]
)JSON";

class CollectLogDataTest : public UnitTestFixture {
 public:
  CollectLogDataTest() : executor_(dispatcher()) {}

 protected:
  void SetupLogServer(std::unique_ptr<stubs::DiagnosticsArchiveBase> server) {
    log_server_ = std::move(server);
    if (log_server_) {
      InjectServiceProvider(log_server_.get(), kArchiveAccessorName);
    }
  }

  ::fit::result<AttachmentValue> CollectSystemLog(const zx::duration timeout = zx::sec(1)) {
    ::fit::result<AttachmentValue> result;
    executor_.schedule_task(
        feedback_data::CollectSystemLog(dispatcher(), services(),
                                        fit::Timeout(timeout, /*action=*/[] {}))
            .then([&result](::fit::result<AttachmentValue>& res) { result = std::move(res); }));
    RunLoopFor(timeout);
    return result;
  }

  async::Executor executor_;

 private:
  std::unique_ptr<stubs::DiagnosticsArchiveBase> log_server_;
};

TEST_F(CollectLogDataTest, Succeed_AllSystemLogs) {
  SetupLogServer(std::make_unique<stubs::DiagnosticsArchive>(
      std::make_unique<stubs::DiagnosticsBatchIterator>(std::vector<std::vector<std::string>>({
          {kMessage1Json, kMessage2Json},
          {kMessage3Json},
          {},
      }))));

  ::fit::result<AttachmentValue> result = CollectSystemLog();
  ASSERT_TRUE(result.is_ok());

  const AttachmentValue& logs = result.value();
  ASSERT_EQ(logs.State(), AttachmentValue::State::kComplete);
  ASSERT_STREQ(logs.Value().c_str(), R"([01234.000][00200][00300][tag_1, tag_a] INFO: Message 1
[01234.000][00200][00300][tag_2] INFO: Message 2
[01234.000][00200][00300][tag_3] INFO: Message 3
)");
}

TEST_F(CollectLogDataTest, Succeed_PartialSystemLogs) {
  SetupLogServer(std::make_unique<stubs::DiagnosticsArchive>(
      std::make_unique<stubs::DiagnosticsBatchIteratorNeverRespondsAfterOneBatch>(
          std::vector<std::string>({kMessage1Json, kMessage2Json}))));

  ::fit::result<AttachmentValue> result = CollectSystemLog();
  ASSERT_TRUE(result.is_ok());

  const AttachmentValue& logs = result.value();
  ASSERT_EQ(logs.State(), AttachmentValue::State::kPartial);
  ASSERT_STREQ(logs.Value().c_str(), R"([01234.000][00200][00300][tag_1, tag_a] INFO: Message 1
[01234.000][00200][00300][tag_2] INFO: Message 2
)");
  EXPECT_EQ(logs.Error(), Error::kTimeout);
}

TEST_F(CollectLogDataTest, Succeed_FormattingErrors) {
  SetupLogServer(std::make_unique<stubs::DiagnosticsArchive>(
      std::make_unique<stubs::DiagnosticsBatchIterator>(std::vector<std::vector<std::string>>({
          {kMessage1Json, kMessage2Json},
          {kMessage3Json},
          {"foo", "bar"},
          {},
      }))));

  ::fit::result<AttachmentValue> result = CollectSystemLog();
  ASSERT_TRUE(result.is_ok());

  const AttachmentValue& logs = result.value();
  ASSERT_EQ(logs.State(), AttachmentValue::State::kComplete);
  ASSERT_STREQ(logs.Value().c_str(), R"([01234.000][00200][00300][tag_1, tag_a] INFO: Message 1
[01234.000][00200][00300][tag_2] INFO: Message 2
[01234.000][00200][00300][tag_3] INFO: Message 3
!!! Failed to format chunk: Failed to parse content as JSON. Offset 1: Invalid value. !!!
!!! Failed to format chunk: Failed to parse content as JSON. Offset 0: Invalid value. !!!
)");
}

}  // namespace
}  // namespace feedback_data
}  // namespace forensics
