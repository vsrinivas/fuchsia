// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/logger/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/sys/cpp/testing/test_with_environment.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/syslog/wire_format.h>
#include <zircon/syscalls/log.h>
#include <zircon/types.h>

#include <memory>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/lib/fsl/handles/object_info.h"

namespace {

class StubLogListener : public fuchsia::logger::LogListenerSafe {
 public:
  using DoneCallback = fit::function<void()>;
  StubLogListener();
  ~StubLogListener() override;

  void LogMany(::std::vector<fuchsia::logger::LogMessage> Log, LogManyCallback done) override;
  void Log(fuchsia::logger::LogMessage Log, LogCallback done) override;
  void Done() override;

  const std::vector<fuchsia::logger::LogMessage>& GetLogs() { return log_messages_; }

  bool ListenFiltered(const std::shared_ptr<sys::ServiceDirectory>& svc, zx_koid_t pid,
                      const std::string& tag);

  bool DumpLogs(fuchsia::logger::LogPtr log_service, DoneCallback done_callback);

  bool Listen(fuchsia::logger::LogPtr log_service);

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

bool StubLogListener::Listen(fuchsia::logger::LogPtr log_service) {
  if (!log_listener_) {
    return false;
  }
  log_service->ListenSafe(std::move(log_listener_), nullptr);
  return true;
}

bool StubLogListener::ListenFiltered(const std::shared_ptr<sys::ServiceDirectory>& svc,
                                     zx_koid_t pid, const std::string& tag) {
  if (!log_listener_) {
    return false;
  }
  auto log_service = svc->Connect<fuchsia::logger::Log>();
  auto options = fuchsia::logger::LogFilterOptions::New();
  options->filter_by_pid = true;
  options->pid = pid;
  options->verbosity = 0;
  options->min_severity = fuchsia::logger::LogLevelFilter::TRACE;
  options->tags = {tag};
  log_service->ListenSafe(std::move(log_listener_), std::move(options));
  return true;
}

bool StubLogListener::DumpLogs(fuchsia::logger::LogPtr log_service, DoneCallback done_callback) {
  if (!log_listener_) {
    return false;
  }
  auto options = fuchsia::logger::LogFilterOptions::New();
  log_service->DumpLogsSafe(std::move(log_listener_), std::move(options));
  done_callback_ = std::move(done_callback);
  return true;
}

using LoggerIntegrationTest = sys::testing::TestWithEnvironment;

TEST_F(LoggerIntegrationTest, ListenFiltered) {
  // Make sure there is one syslog message coming from that pid and with a tag
  // unique to this test case.

  auto pid = fsl::GetCurrentProcessKoid();
  auto tag = "logger_integration_cpp_test.ListenFiltered";
  auto message = "my message";
  // severities "in the wild" including both those from the
  // legacy syslog severities and the new.
  std::vector<int8_t> severities_in_use = {
      -10,             // Legacy "verbosity" (v=10)
      -5,              // Legacy "verbosity" (v=5)
      -4,              // Legacy "verbosity" (v=4)
      -3,              // Legacy "verbosity" (v=3)
      -2,              // Legacy "verbosity" (v=2)
      -1,              // Legacy "verbosity" (v=1)
      0,               // Legacy severity (INFO)
      1,               // Legacy severity (WARNING)
      2,               // Legacy severity (ERROR)
      FX_LOG_TRACE,    // 0x10
      FX_LOG_DEBUG,    // 0x20
      FX_LOG_INFO,     // 0x30
      FX_LOG_WARNING,  // 0x40
      FX_LOG_ERROR,    // 0x50
  };

  // expected severities (sorted), factoring in legacy transforms
  std::vector<int8_t> expected_severities = {
      FX_LOG_TRACE,      // Legacy "verbosity" (v=2)
      FX_LOG_TRACE,      // 0x10
      FX_LOG_DEBUG,      // 0x20
      FX_LOG_DEBUG,      // Legacy "verbosity" (v=1)
      FX_LOG_INFO - 10,  // Legacy "verbosity" (v=10)
      FX_LOG_INFO - 5,   // Legacy "verbosity" (v=5)
      FX_LOG_INFO - 4,   // Legacy "verbosity" (v=4)
      FX_LOG_INFO - 3,   // Legacy "verbosity" (v=3)
      FX_LOG_INFO,       // Legacy severity (INFO)
      FX_LOG_INFO,       // 0x30
      FX_LOG_WARNING,    // 0x40
      FX_LOG_WARNING,    // Legacy severity (WARNING)
      FX_LOG_ERROR,      // Legacy severity (ERROR)
      FX_LOG_ERROR,      // 0x50
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
    EXPECT_THAT(sorted_by_severity[i].msg, testing::EndsWith(message));
  }
}

TEST_F(LoggerIntegrationTest, NoKlogs) {
  auto svcs = CreateServices();
  fuchsia::sys::LaunchInfo linfo;
  linfo.url = "fuchsia-pkg://fuchsia.com/archivist-for-embedding#meta/archivist-for-embedding.cmx";
  fuchsia::sys::LaunchInfo linfo_dup;
  ASSERT_EQ(ZX_OK, linfo.Clone(&linfo_dup));
  svcs->AddServiceWithLaunchInfo(std::move(linfo), fuchsia::logger::Log::Name_);
  svcs->AddServiceWithLaunchInfo(std::move(linfo_dup), fuchsia::logger::LogSink::Name_);
  auto env = CreateNewEnclosingEnvironment("no_klogs", std::move(svcs));
  WaitForEnclosingEnvToStart(env.get());

  auto logger_sink = env->ConnectToService<fuchsia::logger::LogSink>();
  zx::socket logger_sock, server_end;
  ASSERT_EQ(ZX_OK, zx::socket::create(ZX_SOCKET_DATAGRAM, &logger_sock, &server_end));
  logger_sink->Connect(std::move(server_end));

  const char* tag = "my-tag";
  const char** tags = &tag;

  fx_logger_config_t config = {.min_severity = FX_LOG_INFO,
                               .console_fd = -1,
                               .log_service_channel = logger_sock.release(),
                               .tags = tags,
                               .num_tags = 1};

  fx_logger_t* logger;
  ASSERT_EQ(ZX_OK, fx_logger_create(&config, &logger));
  ASSERT_EQ(ZX_OK, fx_logger_log(logger, FX_LOG_INFO, nullptr, "hello world"));

  StubLogListener log_listener;
  ASSERT_TRUE(log_listener.Listen(env->ConnectToService<fuchsia::logger::Log>()));

  RunLoopUntil([&log_listener]() { return log_listener.GetLogs().size() >= 1; });
  auto& logs = log_listener.GetLogs();
  auto& msg = logs[0];
  ASSERT_EQ(msg.tags.size(), 1u);
  ASSERT_EQ(msg.tags[0], tag);
}

}  // namespace
