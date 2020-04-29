// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "log_listener_ostream.h"

#include <lib/gtest/real_loop_fixture.h>

#include "format.h"
#include "log_listener_test_helpers.h"

namespace netemul {
namespace testing {

class LogListenerOStreamImplTest : public gtest::RealLoopFixture {
 protected:
  void Init(std::string env_name) {
    prefix = env_name;

    log_listener.reset(new internal::LogListenerOStreamImpl(
        proxy.NewRequest(dispatcher()), std::move(env_name), &stream, dispatcher()));
  }

  // Generate expected parsed log message for a single log.
  std::string ExpectedLog(std::vector<std::string> tags, std::string message) {
    return ExpectedLogs({tags}, {message});
  }

  // Generate expected parsed log messages for many logs.
  //
  // |tags_list| and |message_list| contain the tags and message for each
  // message. Note, |tags_list[i|] and |message_list[i]| are the tags and
  // message for the ith log, respectively. |tags_list| and |message_list| must
  // have the same size.
  std::string ExpectedLogs(std::vector<std::vector<std::string>> tags_list,
                           std::vector<std::string> message_list) {
    std::stringstream stream;

    assert(message_list.size() == tags_list.size());

    for (size_t i = 0; i < tags_list.size(); ++i) {
      std::vector<std::string> tags = tags_list[i];
      std::string message = message_list[i];

      stream << "[" << prefix << "]";
      internal::FormatTime(&stream, kDummyTime);
      stream << "[" << kDummyPid << "]"
             << "[" << kDummyTid << "]";
      internal::FormatTags(&stream, std::move(tags));
      internal::FormatLogLevel(&stream, kDummySeverity);
      stream << " " << message << std::endl;
    }

    return stream.str();
  }

  std::unique_ptr<internal::LogListenerImpl> log_listener;
  fuchsia::logger::LogListenerSafePtr proxy;
  std::stringstream stream;
  std::string prefix;
};

TEST_F(LogListenerOStreamImplTest, SimpleLog) {
  Init("netemul");

  bool called = false;
  proxy->Log(CreateLogMessage({"tag"}, "Hello"), [&called]() { called = true; });

  auto message = ExpectedLog({"tag"}, "Hello");
  RunLoopUntil([&called]() { return called; });
  EXPECT_EQ(message, stream.str());
}

TEST_F(LogListenerOStreamImplTest, SimpleLogs) {
  Init("netemul");

  int called = 0;
  proxy->Log(CreateLogMessage({"tag1"}, "Hello1"), [&called]() { called++; });
  proxy->Log(CreateLogMessage({"tag2.1", "tag2.2"}, "Hello2"), [&called]() { called++; });

  auto message = ExpectedLogs({{"tag1"}, {"tag2.1", "tag2.2"}}, {"Hello1", "Hello2"});
  RunLoopUntil([&called]() { return called == 2; });
  EXPECT_EQ(message, stream.str());
}

}  // namespace testing
}  // namespace netemul
