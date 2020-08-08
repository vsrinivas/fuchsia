// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syslog/logger.h>

#include <memory>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/feedback_data/system_log_recorder/encoding/identity_encoder.h"
#include "src/developer/forensics/feedback_data/system_log_recorder/system_log_recorder.h"
#include "src/developer/forensics/testing/stubs/logger.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"
#include "src/developer/forensics/utils/log_format.h"

namespace forensics {
namespace feedback_data {
namespace system_log_recorder {
namespace {

using stubs::BuildLogMessage;

// Only change "X" for one character. i.e. X -> 12 is not allowed.
const size_t kMaxLogLineSize = Format(BuildLogMessage(FX_LOG_INFO, "line X")).size();
// We set the block size to an arbitrary large numbers for test cases where the block logic does
// not matter.
const size_t kVeryLargeBlockSize = kMaxLogLineSize * 100;

std::unique_ptr<Encoder> MakeIdentityEncoder() {
  return std::unique_ptr<Encoder>(new IdentityEncoder());
}

using ListenerTest = UnitTestFixture;

TEST_F(ListenerTest, AddsMessages) {
  const std::vector<std::vector<fuchsia::logger::LogMessage>> dumps({
      {
          BuildLogMessage(FX_LOG_INFO, "line 0"),
          BuildLogMessage(FX_LOG_INFO, "line 1"),
          BuildLogMessage(FX_LOG_INFO, "line 2"),
          BuildLogMessage(FX_LOG_INFO, "line 3"),

      },
      {
          BuildLogMessage(FX_LOG_INFO, "line 4"),
          BuildLogMessage(FX_LOG_INFO, "line 5"),
          BuildLogMessage(FX_LOG_INFO, "line 6"),
          BuildLogMessage(FX_LOG_INFO, "line 7"),
      },

  });

  const std::vector<fuchsia::logger::LogMessage> messages({
      BuildLogMessage(FX_LOG_INFO, "line 8"),
      BuildLogMessage(FX_LOG_INFO, "line 9"),
  });

  stubs::LoggerDelayedResponses logger(dispatcher(), dumps, messages, /*delay=*/zx::msec(5));
  InjectServiceProvider(&logger);

  // Set up the store to hold all of the added messages.
  LogMessageStore store(kVeryLargeBlockSize, /*max_buffer_capacity_bytes=*/1024,
                        MakeIdentityEncoder());

  SystemLogListener listener(services(), &store);
  listener.StartListening();

  // Run the loop for as much time needed to ensure at the stub calls LogMany() and Log() as
  // specified in the constructor.
  RunLoopFor(logger.TotalDelayBetweenDumps() + logger.TotalDelayBetweenMessages());

  bool end_of_block;
  EXPECT_EQ(store.Consume(&end_of_block), R"([15604.000][07559][07687][] INFO: line 0
[15604.000][07559][07687][] INFO: line 1
[15604.000][07559][07687][] INFO: line 2
[15604.000][07559][07687][] INFO: line 3
[15604.000][07559][07687][] INFO: line 4
[15604.000][07559][07687][] INFO: line 5
[15604.000][07559][07687][] INFO: line 6
[15604.000][07559][07687][] INFO: line 7
[15604.000][07559][07687][] INFO: line 8
[15604.000][07559][07687][] INFO: line 9
)");
  EXPECT_FALSE(end_of_block);
}

}  // namespace
}  // namespace system_log_recorder
}  // namespace feedback_data
}  // namespace forensics
