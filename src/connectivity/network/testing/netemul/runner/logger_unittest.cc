// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/sys/cpp/testing/test_with_environment.h>
#include <lib/syslog/wire_format.h>

#include "log_listener.h"
#include "log_listener_log_sink.h"
#include "log_listener_test_helpers.h"
#include "src/lib/fxl/strings/join_strings.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/lib/testing/predicates/status.h"

namespace netemul {
namespace testing {

constexpr const char* kLoggerUrl = "fuchsia-pkg://fuchsia.com/archivist#meta/observer.cmx";

class LoggerTest : public sys::testing::TestWithEnvironment {
 protected:
  fuchsia::sys::LaunchInfo MakeLoggerLaunchInfo() {
    fuchsia::sys::LaunchInfo ret;
    ret.url = kLoggerUrl;
    return ret;
  }

  void Init(std::string env_name) {
    auto services = CreateServices();
    services->AddServiceWithLaunchInfo(MakeLoggerLaunchInfo(), fuchsia::logger::LogSink::Name_);
    services->AddServiceWithLaunchInfo(MakeLoggerLaunchInfo(), fuchsia::logger::Log::Name_);
    env = CreateNewEnclosingEnvironment(
        "some_logger", std::move(services),
        fuchsia::sys::EnvironmentOptions{.inherit_parent_services = false,
                                         .use_parent_runners = false,
                                         .kill_on_oom = true,
                                         .delete_storage_on_death = true});
    WaitForEnclosingEnvToStart(env.get());

    fuchsia::logger::LogSinkPtr sink;
    env->ConnectToService(sink.NewRequest(dispatcher()));
    zx::socket mine, remote;
    ASSERT_OK(zx::socket::create(ZX_SOCKET_DATAGRAM, &mine, &remote));
    sink->Connect(std::move(remote));
    log_listener.reset(new internal::LogListenerLogSinkImpl(
        proxy.NewRequest(dispatcher()), std::move(env_name), std::move(mine), dispatcher()));
    fuchsia::logger::LogPtr syslog;
    syslog.set_error_handler([](zx_status_t err) { FAIL() << "Lost connection to syslog"; });

    env->ConnectToService(syslog.NewRequest(dispatcher()));
    fidl::InterfaceHandle<fuchsia::logger::LogListener> test_handle;
    test_listener = std::make_unique<TestListener>(test_handle.NewRequest());
    test_listener->SetObserver([](const fuchsia::logger::LogMessage& log) {
      // shouldn't receive any klog
      ASSERT_EQ(std::find(log.tags.begin(), log.tags.end(), "klog"), log.tags.end());
    });
    syslog->Listen(std::move(test_handle), nullptr);
  }

  void ValidateMessage(const fuchsia::logger::LogMessage& msg, const std::vector<std::string>& tags,
                       const std::string& message) {
    EXPECT_EQ(msg.severity, kDummySeverity);
    EXPECT_EQ(msg.time, kDummyTime);
    EXPECT_EQ(msg.pid, kDummyPid);
    EXPECT_EQ(msg.tid, kDummyTid);
    EXPECT_EQ(msg.dropped_logs, 0ul);
    EXPECT_EQ(msg.msg, message);
    EXPECT_EQ(tags.size(), msg.tags.size());
    for (const auto& t : tags) {
      EXPECT_NE(std::find(msg.tags.begin(), msg.tags.end(), t), msg.tags.end())
          << "Can't find tag " << t << " in " << fxl::JoinStrings(msg.tags, ",");
    }
  }

  std::vector<fuchsia::logger::LogMessage> WaitForMessages() {
    RunLoopUntil([this]() { return !test_listener->messages().empty(); });
    auto ret = std::move(test_listener->messages());
    test_listener->messages().clear();
    return ret;
  }

  std::unique_ptr<sys::testing::EnclosingEnvironment> env;
  std::unique_ptr<internal::LogListenerImpl> log_listener;
  std::unique_ptr<TestListener> test_listener;
  fuchsia::logger::LogListenerPtr proxy;
};

TEST_F(LoggerTest, SyslogRedirect) {
  Init("netemul");
  std::string env_name = "@netemul";

  proxy->Log(CreateLogMessage({"tag"}, "Hello"));
  auto msgs = WaitForMessages();
  ValidateMessage(msgs[0], {"tag", env_name}, "Hello");
}

TEST_F(LoggerTest, TooManyTags) {
  Init("netemul");
  std::string env_name = "@netemul";

  std::vector<std::string> tags;
  tags.reserve(FX_LOG_MAX_TAGS);
  for (int i = 0; i < FX_LOG_MAX_TAGS; i++) {
    tags.emplace_back(fxl::StringPrintf("t%d", i));
  }

  proxy->Log(CreateLogMessage(tags, "Hello"));
  auto msgs = WaitForMessages();
  ValidateMessage(msgs[0], tags, "[@netemul] Hello");
}

TEST_F(LoggerTest, LongEnvironmentName) {
  std::stringstream ss;
  for (int i = 0; i < FX_LOG_MAX_TAG_LEN + 5; i++) {
    ss << static_cast<char>('a' + (i % 26));
  }
  auto env_name = ss.str();
  Init(env_name);
  std::string expect_tag = "@" + env_name.substr(0, FX_LOG_MAX_TAG_LEN - 2);

  proxy->Log(CreateLogMessage({"tag"}, "Hello"));
  auto msgs = WaitForMessages();
  ValidateMessage(msgs[0], {"tag", expect_tag}, "Hello");
}

TEST_F(LoggerTest, VeryLongEnvironmentName) {
  std::stringstream ss;
  for (int i = 0; i < FX_LOG_MAX_DATAGRAM_LEN; i++) {
    ss << static_cast<char>('a' + (i % 26));
  }
  auto env_name = ss.str();
  Init(std::move(env_name));

  std::vector<std::string> tags;
  tags.reserve(FX_LOG_MAX_TAGS);
  for (int i = 0; i < FX_LOG_MAX_TAGS; i++) {
    tags.emplace_back(fxl::StringPrintf("t%d", i));
  }
  // if environment name is too long to fit in message,
  // we'll just not add it.

  proxy->Log(CreateLogMessage(tags, "Hello"));
  auto msgs = WaitForMessages();
  ValidateMessage(msgs[0], tags, "Hello");
}

TEST_F(LoggerTest, LongMessageLongTags) {
  std::stringstream ss;
  for (size_t i = 0; i < sizeof(fx_log_packet_t::data); i++) {
    ss << static_cast<char>('a' + (i % 26));
  }

  auto msg = ss.str();
  Init("netemul");
  std::string prefix = "[@netemul] ";
  std::vector<std::string> tags;
  tags.reserve(FX_LOG_MAX_TAGS);
  size_t tags_len = 1;
  for (int i = 0; i < FX_LOG_MAX_TAGS; i++) {
    auto& t = tags.emplace_back(fxl::StringPrintf("t%d", i));
    tags_len += t.length() + 1;
  }

  // If message is really long, the environment name will
  // take some of the space when tags are full

  proxy->Log(CreateLogMessage(tags, msg));

  auto msgs = WaitForMessages();
  ValidateMessage(
      msgs[0], tags,
      prefix + msg.substr(0, sizeof(fx_log_packet_t::data) - tags_len - prefix.length() - 1));
}

TEST_F(LoggerTest, LongMessage) {
  std::stringstream ss;
  for (size_t i = 0; i < sizeof(fx_log_packet_t::data); i++) {
    ss << static_cast<char>('a' + (i % 26));
  }

  auto msg = ss.str();
  std::string env_name = "@netemul";
  Init("netemul");

  std::string tag = "tag";
  size_t tags_len = 1 + (1 + tag.length()) + (1 + env_name.length());

  proxy->Log(CreateLogMessage({tag}, msg));

  // Since we're adding more tags with environment name,
  // long messages neeed to be trimmed.

  auto msgs = WaitForMessages();
  ValidateMessage(msgs[0], {tag, env_name},
                  msg.substr(0, sizeof(fx_log_packet_t::data) - tags_len - 1));
}

TEST_F(LoggerTest, MultipleMessages) {
  std::string env_name = "@netemul";
  Init("netemul");

  constexpr int messages = 10;
  for (int i = 0; i < messages; i++) {
    proxy->Log(CreateLogMessage({"tag"}, fxl::StringPrintf("Hello%d", i)));
  }

  std::vector<fuchsia::logger::LogMessage> consumed;
  while (consumed.size() < messages) {
    auto msgs = WaitForMessages();
    consumed.insert(consumed.end(), msgs.begin(), msgs.end());
  }

  // sort the received messages by their contents to account for out-of-order delivery by logger
  std::sort(consumed.begin(), consumed.end(), [](auto a, auto b) { return a.msg < b.msg; });

  int i = 0;
  for (const auto& msg : consumed) {
    ValidateMessage(msg, {"tag", env_name}, fxl::StringPrintf("Hello%d", i));
    i++;
  }
}

}  // namespace testing
}  // namespace netemul
