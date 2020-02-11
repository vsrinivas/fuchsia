// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_agent/system_log_recorder.h"

#include <lib/syslog/logger.h>

#include <memory>

#include "src/developer/feedback/feedback_agent/tests/stub_logger.h"
#include "src/developer/feedback/testing/unit_test_fixture.h"
#include "src/developer/feedback/utils/rotating_file_set.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/files/scoped_temp_dir.h"
#include "third_party/googletest/googlemock/include/gmock/gmock.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace feedback {
namespace {

const std::vector<const std::string> kLogFileNames = {
    "file0.txt",
    "file1.txt",
    "file2.txt",
    "file3.txt",
};

constexpr zx::duration kDelayBetweenResponses = zx::msec(5);

class SystemLogRecorderTest : public UnitTestFixture {
 public:
  SystemLogRecorderTest() {
    for (const auto& file_name : kLogFileNames) {
      log_file_paths_.push_back(files::JoinPath(RootDirectory(), file_name));
    }
  }

 protected:
  void SetUpSystemLogRecorder(const FileSize log_size) {
    system_log_recorder_ =
        std::make_unique<SystemLogRecorder>(services(), log_file_paths_, log_size);
  }

  void SetUpLogger(std::unique_ptr<StubLogger> logger) {
    logger_ = std::move(logger);
    if (logger_) {
      InjectServiceProvider(logger_.get());
    }
  }

  std::string RootDirectory() { return temp_dir_.path(); }

  void StartRecording() {
    ASSERT_TRUE(system_log_recorder_);
    system_log_recorder_->StartRecording();
  }

 private:
  files::ScopedTempDir temp_dir_;
  std::unique_ptr<StubLogger> logger_;
  std::unique_ptr<SystemLogRecorder> system_log_recorder_;

 protected:
  std::vector<const std::string> log_file_paths_;
};

TEST_F(SystemLogRecorderTest, Check_RecordsLogsCorrectly) {
  // This constant needs to be kept in sync with the messages that are logged by the stub. If a
  // message is larger that 42 bytes, the values needs to increase to accomodate that message.
  constexpr FileSize kMaxLogLineSize = FileSize::Bytes(42);

  const std::vector<std::vector<fuchsia::logger::LogMessage>> dumps({
      {
          BuildLogMessage(FX_LOG_INFO, "line 1"),
          BuildLogMessage(FX_LOG_INFO, "line 2"),
          BuildLogMessage(FX_LOG_INFO, "line 3"),
          BuildLogMessage(FX_LOG_INFO, "line 4"),

      },
      {
          BuildLogMessage(FX_LOG_INFO, "line 5"),
          BuildLogMessage(FX_LOG_INFO, "line 6"),
          BuildLogMessage(FX_LOG_INFO, "line 7"),
          BuildLogMessage(FX_LOG_INFO, "line 8"),
      },

  });

  const std::vector<fuchsia::logger::LogMessage> messages({
      BuildLogMessage(FX_LOG_INFO, "line 9"),
      BuildLogMessage(FX_LOG_INFO, "line 10"),

  });

  auto logger = std::make_unique<StubLoggerDelayedResponses>(dispatcher(), dumps, messages,
                                                             kDelayBetweenResponses);

  const zx::duration total_dump_delays = logger->TotalDelayBetweenDumps();
  const zx::duration total_message_delays = logger->TotalDelayBetweenMessages();

  SetUpLogger(std::move(logger));

  // Set up the system log recorder to hold up to |log_file_paths_.size()| lines at a time.
  SetUpSystemLogRecorder(kMaxLogLineSize * log_file_paths_.size());

  StartRecording();

  // Run the loop for as much time needed to ensure at the stub calls LogMany() and Log() as
  // specified in the constructor.
  RunLoopFor(total_dump_delays + total_message_delays);

  const std::string output_path = files::JoinPath(RootDirectory(), "output.txt");

  RotatingFileSetReader reader(log_file_paths_);
  reader.Concatenate(output_path);

  std::string contents;
  ASSERT_TRUE(files::ReadFileToString(output_path, &contents));

  EXPECT_EQ(contents,
            R"([15604.000][07559][07687][] INFO: line 7
[15604.000][07559][07687][] INFO: line 8
[15604.000][07559][07687][] INFO: line 9
[15604.000][07559][07687][] INFO: line 10
)");
}

}  // namespace
}  // namespace feedback
