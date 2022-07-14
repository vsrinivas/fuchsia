// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/logger/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/types.h>

#include <memory>
#include <vector>

#include <gtest/gtest.h>
#include <src/lib/fsl/handles/object_info.h>
#include <src/lib/testing/loop_fixture/real_loop_fixture.h>

namespace {

class StubLogListener : public fuchsia::logger::LogListenerSafe {
 public:
  using DoneCallback = fit::function<void()>;
  StubLogListener();
  ~StubLogListener() override;

  const std::vector<fuchsia::logger::LogMessage>& GetLogs() { return log_messages_; }

  bool ListenFiltered(const std::shared_ptr<sys::ServiceDirectory>& svc, zx_koid_t pid,
                      const std::string& tag);
  void LogMany(::std::vector<fuchsia::logger::LogMessage> Log, LogManyCallback done) override;
  void Log(fuchsia::logger::LogMessage Log, LogCallback done) override;
  void Done() override;

 private:
  ::fidl::Binding<fuchsia::logger::LogListenerSafe> binding_;
  fuchsia::logger::LogListenerSafePtr log_listener_;
  std::vector<fuchsia::logger::LogMessage> log_messages_;
  DoneCallback done_callback_;
};

StubLogListener::StubLogListener() : binding_(this) { binding_.Bind(log_listener_.NewRequest()); }

StubLogListener::~StubLogListener() {}

void StubLogListener::LogMany(::std::vector<fuchsia::logger::LogMessage> logs,
                              LogManyCallback done) {
  std::move(logs.begin(), logs.end(), std::back_inserter(log_messages_));
  done();
}

void StubLogListener::Log(fuchsia::logger::LogMessage log, LogCallback done) {
  log_messages_.push_back(std::move(log));
  done();
}

void StubLogListener::Done() {
  if (done_callback_) {
    done_callback_();
  }
}

bool StubLogListener::ListenFiltered(const std::shared_ptr<sys::ServiceDirectory>& svc,
                                     zx_koid_t pid, const std::string& tag) {
  if (!log_listener_) {
    return false;
  }
  auto log_service = svc->Connect<fuchsia::logger::Log>();
  auto options = std::make_unique<fuchsia::logger::LogFilterOptions>();
  options->filter_by_pid = true;
  options->pid = pid;
  options->verbosity = 0;
  options->min_severity = fuchsia::logger::LogLevelFilter::TRACE;
  options->tags = {tag};
  log_service->ListenSafe(std::move(log_listener_), std::move(options));
  return true;
}

using LoggerIntegrationTest = gtest::RealLoopFixture;

TEST_F(LoggerIntegrationTest, ListenFiltered) {
  // Make sure there is one syslog message coming from that pid and with a tag
  // unique to this test case.

  auto pid = fsl::GetCurrentProcessKoid();
  auto tag = "logger_integration_cpp_test.ListenFiltered";
  auto message = "my message";
  // severities "in the wild" including both those from the
  // legacy syslog severities and the new.
  std::vector<int8_t> severities_in_use = {
      -10,                  // Legacy "verbosity" (v=10)
      -5,                   // Legacy "verbosity" (v=5)
      -4,                   // Legacy "verbosity" (v=4)
      -3,                   // Legacy "verbosity" (v=3)
      -2,                   // Legacy "verbosity" (v=2)
      -1,                   // Legacy "verbosity" (v=1)
      0,                    // Legacy severity (INFO)
      1,                    // Legacy severity (WARNING)
      2,                    // Legacy severity (ERROR)
      syslog::LOG_TRACE,    // 0x10
      syslog::LOG_DEBUG,    // 0x20
      syslog::LOG_INFO,     // 0x30
      syslog::LOG_WARNING,  // 0x40
      syslog::LOG_ERROR,    // 0x50
  };

  // expected severities (sorted), factoring in legacy transforms
  std::vector<int8_t> expected_severities = {
      syslog::LOG_TRACE,      // Legacy "verbosity" (v=2)
      syslog::LOG_TRACE,      // 0x10
      syslog::LOG_DEBUG,      // 0x20
      syslog::LOG_DEBUG,      // Legacy "verbosity" (v=1)
      syslog::LOG_INFO - 10,  // Legacy "verbosity" (v=10)
      syslog::LOG_INFO - 5,   // Legacy "verbosity" (v=5)
      syslog::LOG_INFO - 4,   // Legacy "verbosity" (v=4)
      syslog::LOG_INFO - 3,   // Legacy "verbosity" (v=3)
      syslog::LOG_INFO,       // Legacy severity (INFO)
      syslog::LOG_INFO,       // 0x30
      syslog::LOG_WARNING,    // 0x40
      syslog::LOG_WARNING,    // Legacy severity (WARNING)
      syslog::LOG_ERROR,      // Legacy severity (ERROR)
      syslog::LOG_ERROR,      // 0x50
  };

  syslog::LogSettings settings = {.min_log_level = severities_in_use[0]};
  syslog::SetLogSettings(settings, {tag});

  for (auto severity : severities_in_use) {
    FX_LOGS(LEVEL(severity)) << message;
  }

  // Start the log listener and the logger, and wait for the log message to arrive.
  StubLogListener log_listener;
  ASSERT_TRUE(log_listener.ListenFiltered(sys::ServiceDirectory::CreateFromNamespace(), pid, tag));
  auto& logs = log_listener.GetLogs();
  RunLoopWithTimeoutOrUntil(
      [&logs, expected_size = severities_in_use.size()] { return logs.size() >= expected_size; },
      zx::min(2));

  std::vector<fuchsia::logger::LogMessage> sorted_by_severity(logs.begin(), logs.end());
  std::sort(sorted_by_severity.begin(), sorted_by_severity.end(),
            [](auto a, auto b) { return a.severity < b.severity; });

  ASSERT_EQ(sorted_by_severity.size(), expected_severities.size());
  for (auto i = 0ul; i < logs.size(); i++) {
    ASSERT_EQ(sorted_by_severity[i].tags.size(), 1u);
    EXPECT_EQ(sorted_by_severity[i].tags[0], tag);
    EXPECT_EQ(sorted_by_severity[i].severity, expected_severities[i]);
    EXPECT_EQ(sorted_by_severity[i].pid, pid);
    EXPECT_TRUE(sorted_by_severity[i].msg.find(message) != std::string::npos);
  }
}

}  // namespace
