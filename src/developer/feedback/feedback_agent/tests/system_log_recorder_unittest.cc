// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_agent/system_log_recorder.h"

#include <lib/syslog/logger.h>

#include <memory>

#include "src/developer/feedback/testing/stubs/logger.h"
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

// constexpr zx::duration kDelayBetweenResponses = zx::msec(5);

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

  void SetUpLogger(std::unique_ptr<stubs::Logger> logger) {
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
  std::unique_ptr<stubs::Logger> logger_;
  std::unique_ptr<SystemLogRecorder> system_log_recorder_;

 protected:
  std::vector<const std::string> log_file_paths_;
};

// TODO(44891): Add test cases.

}  // namespace
}  // namespace feedback
